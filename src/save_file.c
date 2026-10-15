#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "save_file.h"

#include "blocks/block_internal.h"
#include "sim_internal.h"
#include "world_internal.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

enum {
    SAVE_VERSION = 1,
    SAVE_HEADER_SIZE = 56,
    SAVE_CRC_OFFSET = 44,
    TORCH_HISTORY_COUNT = 8,
    TORCH_HISTORY_EXPIRED_AGE = 61
};

static const unsigned char save_magic[8] = {
    'M', 'C', 'W', 'O', 'R', 'L', 'D', '\0'
};

typedef struct {
    FILE *file;
    uint32_t crc;
    uint64_t size;
    bool ok;
} PayloadWriter;

typedef struct {
    const unsigned char *bytes;
    size_t size;
    size_t offset;
    bool ok;
} BufferReader;

typedef struct {
    uint32_t index;
    uint64_t remaining;
} StoredEvent;

static uint32_t crc32_update(uint32_t crc, const unsigned char *bytes, size_t size)
{
    for (size_t index = 0; index < size; ++index) {
        crc ^= bytes[index];
        for (int bit = 0; bit < 8; ++bit) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xedb88320u & mask);
        }
    }
    return crc;
}

static uint32_t crc32_bytes(const unsigned char *bytes, size_t size)
{
    return ~crc32_update(UINT32_MAX, bytes, size);
}

static bool file_write(FILE *file, const void *bytes, size_t size)
{
    return size == 0 || fwrite(bytes, 1, size, file) == size;
}

static bool file_write_u16(FILE *file, uint16_t value)
{
    unsigned char bytes[2] = {
        (unsigned char)value,
        (unsigned char)(value >> 8)
    };
    return file_write(file, bytes, sizeof(bytes));
}

static bool file_write_u32(FILE *file, uint32_t value)
{
    unsigned char bytes[4] = {
        (unsigned char)value,
        (unsigned char)(value >> 8),
        (unsigned char)(value >> 16),
        (unsigned char)(value >> 24)
    };
    return file_write(file, bytes, sizeof(bytes));
}

static bool file_write_u64(FILE *file, uint64_t value)
{
    unsigned char bytes[8];
    for (int index = 0; index < 8; ++index) {
        bytes[index] = (unsigned char)(value >> (index * 8));
    }
    return file_write(file, bytes, sizeof(bytes));
}

static void payload_write(PayloadWriter *writer, const void *bytes, size_t size)
{
    if (!writer->ok || UINT64_MAX - writer->size < size) {
        writer->ok = false;
        return;
    }
    if (!file_write(writer->file, bytes, size)) {
        writer->ok = false;
        return;
    }
    writer->crc = crc32_update(writer->crc, bytes, size);
    writer->size += size;
}

static void payload_u8(PayloadWriter *writer, uint8_t value)
{
    payload_write(writer, &value, sizeof(value));
}

static void payload_u16(PayloadWriter *writer, uint16_t value)
{
    unsigned char bytes[2] = {
        (unsigned char)value,
        (unsigned char)(value >> 8)
    };
    payload_write(writer, bytes, sizeof(bytes));
}

static void payload_u32(PayloadWriter *writer, uint32_t value)
{
    unsigned char bytes[4] = {
        (unsigned char)value,
        (unsigned char)(value >> 8),
        (unsigned char)(value >> 16),
        (unsigned char)(value >> 24)
    };
    payload_write(writer, bytes, sizeof(bytes));
}

static void payload_u64(PayloadWriter *writer, uint64_t value)
{
    unsigned char bytes[8];
    for (int index = 0; index < 8; ++index) {
        bytes[index] = (unsigned char)(value >> (index * 8));
    }
    payload_write(writer, bytes, sizeof(bytes));
}

static uint8_t reader_u8(BufferReader *reader)
{
    if (!reader->ok || reader->offset == reader->size) {
        reader->ok = false;
        return 0;
    }
    return reader->bytes[reader->offset++];
}

static uint16_t reader_u16(BufferReader *reader)
{
    uint16_t value = reader_u8(reader);
    value |= (uint16_t)reader_u8(reader) << 8;
    return value;
}

static uint32_t reader_u32(BufferReader *reader)
{
    uint32_t value = reader_u8(reader);
    value |= (uint32_t)reader_u8(reader) << 8;
    value |= (uint32_t)reader_u8(reader) << 16;
    value |= (uint32_t)reader_u8(reader) << 24;
    return value;
}

static uint64_t reader_u64(BufferReader *reader)
{
    uint64_t value = 0;
    for (int index = 0; index < 8; ++index) {
        value |= (uint64_t)reader_u8(reader) << (index * 8);
    }
    return value;
}

static BlockId generated_id(const World *world, uint32_t index)
{
    BlockPos position = world_position(world, index);
    return position.y < world->config.ground_layers ? BLOCK_IRON : BLOCK_AIR;
}

static bool cell_differs_from_generated(const World *world, uint32_t index)
{
    return world->cells[index].id != generated_id(world, index);
}

static bool serializable_state(BlockState state)
{
    switch ((BlockId)state.id) {
    case BLOCK_AIR:
    case BLOCK_IRON:
    case BLOCK_REDSTONE_BLOCK:
        return true;
    case BLOCK_REDSTONE_WIRE: {
        unsigned horizontal = (1u << DIR_EAST) | (1u << DIR_WEST) |
                              (1u << DIR_SOUTH) | (1u << DIR_NORTH);
        return state.data.wire.power <= 15 &&
               !(state.data.wire.connections & ~horizontal) &&
               !(state.data.wire.up_connections & ~state.data.wire.connections);
    }
    case BLOCK_REDSTONE_TORCH:
        return (state.data.torch.support_dir == DIR_DOWN ||
                direction_horizontal((Direction)state.data.torch.support_dir));
    default:
        return false;
    }
}

static bool runtime_has_state(const World *world, uint32_t index)
{
    TorchRuntime *runtime = sim_torch_runtime(world, index);
    return runtime && (runtime->off_count || runtime->burned_until > world->tick);
}

static bool valid_runtime(const World *world, uint32_t index)
{
    TorchRuntime *runtime = sim_torch_runtime(world, index);
    if (!runtime || runtime->off_count > TORCH_HISTORY_COUNT ||
        runtime->off_head >= TORCH_HISTORY_COUNT ||
        (runtime->off_count < TORCH_HISTORY_COUNT &&
         runtime->off_head != runtime->off_count)) return false;
    return runtime->burned_until <= world->tick ||
           runtime->burned_until - world->tick <= UINT16_MAX;
}

static int compare_events(const void *left, const void *right)
{
    const StoredEvent *first = left;
    const StoredEvent *second = right;
    if (first->index < second->index) return -1;
    if (first->index > second->index) return 1;
    return 0;
}

static bool sync_file(FILE *file)
{
    if (fflush(file) != 0) return false;
#ifdef _WIN32
    return _commit(_fileno(file)) == 0;
#else
    return fsync(fileno(file)) == 0;
#endif
}

static bool replace_file(const char *temporary, const char *destination)
{
#ifdef _WIN32
    return MoveFileExA(temporary, destination,
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return rename(temporary, destination) == 0;
#endif
}

static GameSessionResult write_save_contents(const World *world, FILE *file)
{
    uint32_t block_count = 0;
    uint32_t runtime_count = 0;
    Simulator *sim = world->sim;

    for (uint32_t index = 0; index < (uint32_t)world->volume; ++index) {
        BlockState state = world->cells[index];
        if (!serializable_state(state)) return GAME_SESSION_SIMULATION_ERROR;
        if (state.id == BLOCK_REDSTONE_TORCH &&
            !valid_runtime(world, index)) return GAME_SESSION_SIMULATION_ERROR;
        if (cell_differs_from_generated(world, index)) ++block_count;
        if (state.id == BLOCK_REDSTONE_TORCH && runtime_has_state(world, index))
            ++runtime_count;
    }

    if (sim->scheduler.count > UINT32_MAX)
        return GAME_SESSION_SIMULATION_ERROR;
    uint32_t event_count = (uint32_t)sim->scheduler.count;

    bool header_ok =
        file_write(file, save_magic, sizeof(save_magic)) &&
        file_write_u16(file, SAVE_VERSION) &&
        file_write_u16(file, SAVE_HEADER_SIZE) &&
        file_write_u32(file, 0) &&
        file_write_u32(file, (uint32_t)world->config.size_x) &&
        file_write_u32(file, (uint32_t)world->config.size_y) &&
        file_write_u32(file, (uint32_t)world->config.size_z) &&
        file_write_u32(file, (uint32_t)world->config.ground_layers) &&
        file_write_u32(file, block_count) &&
        file_write_u32(file, runtime_count) &&
        file_write_u32(file, event_count) &&
        file_write_u32(file, 0) &&
        file_write_u64(file, 0);
    if (!header_ok) return GAME_SESSION_IO_ERROR;

    PayloadWriter writer = {file, UINT32_MAX, 0, true};
    for (uint32_t index = 0; index < (uint32_t)world->volume; ++index) {
        if (!cell_differs_from_generated(world, index)) continue;
        BlockState state = world->cells[index];
        payload_u32(&writer, index);
        payload_u8(&writer, state.id);
        if (state.id == BLOCK_REDSTONE_WIRE) {
            payload_u8(&writer, state.data.wire.power);
            payload_u8(&writer, state.data.wire.connections);
            payload_u8(&writer, state.data.wire.up_connections);
            payload_u8(&writer, state.data.wire.prefer_dot ? 1u : 0u);
        } else if (state.id == BLOCK_REDSTONE_TORCH) {
            payload_u8(&writer, state.data.torch.support_dir);
            payload_u8(&writer, state.data.torch.lit ? 1u : 0u);
        }
    }

    for (uint32_t index = 0; index < (uint32_t)world->volume; ++index) {
        if (world->cells[index].id != BLOCK_REDSTONE_TORCH ||
            !runtime_has_state(world, index)) continue;
        TorchRuntime *runtime = sim_torch_runtime(world, index);
        uint64_t remaining = runtime->burned_until > world->tick ?
                             runtime->burned_until - world->tick : 0;
        if (remaining > UINT16_MAX) return GAME_SESSION_SIMULATION_ERROR;

        payload_u32(&writer, index);
        payload_u8(&writer, runtime->off_count);
        payload_u8(&writer, runtime->off_head);
        payload_u16(&writer, (uint16_t)remaining);
        for (int history = 0; history < TORCH_HISTORY_COUNT; ++history) {
            uint64_t age = world->tick - runtime->off_times[history];
            if (runtime->off_count < TORCH_HISTORY_COUNT &&
                history >= runtime->off_count) age = 0;
            if (age > TORCH_HISTORY_EXPIRED_AGE)
                age = TORCH_HISTORY_EXPIRED_AGE;
            payload_u8(&writer, (uint8_t)age);
        }
    }

    StoredEvent *events = NULL;
    if (event_count) {
        events = malloc((size_t)event_count * sizeof(*events));
        if (!events) return GAME_SESSION_OUT_OF_MEMORY;
        for (uint32_t cursor = 0; cursor < event_count; ++cursor) {
            ScheduledEvent event = sim->scheduler.heap[cursor];
            if (event.index >= world->volume || event.due <= world->tick ||
                !block_definition((BlockId)world->cells[event.index].id)->scheduled_tick) {
                free(events);
                return GAME_SESSION_SIMULATION_ERROR;
            }
            events[cursor] = (StoredEvent){event.index, event.due - world->tick};
        }
        qsort(events, event_count, sizeof(*events), compare_events);
        for (uint32_t cursor = 0; cursor < event_count; ++cursor) {
            if (cursor && events[cursor - 1].index == events[cursor].index) {
                free(events);
                return GAME_SESSION_SIMULATION_ERROR;
            }
            payload_u32(&writer, events[cursor].index);
            payload_u64(&writer, events[cursor].remaining);
        }
        free(events);
    }

    if (!writer.ok) return GAME_SESSION_IO_ERROR;
    uint32_t crc = ~writer.crc;
    if (fseek(file, SAVE_CRC_OFFSET, SEEK_SET) != 0 ||
        !file_write_u32(file, crc) || !file_write_u64(file, writer.size))
        return GAME_SESSION_IO_ERROR;
    return GAME_SESSION_OK;
}

GameSessionResult save_file_write(const World *world, const char *path)
{
    if (!world || !path || !*path) return GAME_SESSION_INVALID_ARGUMENT;
    size_t length = strlen(path);
    if (length > SIZE_MAX - 5) return GAME_SESSION_PATH_TOO_LONG;
    char *temporary = malloc(length + 5);
    if (!temporary) return GAME_SESSION_OUT_OF_MEMORY;
    memcpy(temporary, path, length);
    memcpy(temporary + length, ".tmp", 5);

    FILE *file = fopen(temporary, "wb");
    if (!file) {
        free(temporary);
        return GAME_SESSION_IO_ERROR;
    }

    GameSessionResult result = write_save_contents(world, file);
    if (result == GAME_SESSION_OK && !sync_file(file))
        result = GAME_SESSION_IO_ERROR;
    if (fclose(file) != 0 && result == GAME_SESSION_OK)
        result = GAME_SESSION_IO_ERROR;

    if (result == GAME_SESSION_OK && !replace_file(temporary, path))
        result = GAME_SESSION_IO_ERROR;
    if (result != GAME_SESSION_OK) remove(temporary);
    free(temporary);
    return result;
}

static unsigned char *read_whole_file(const char *path, size_t *size,
                                      GameSessionResult *result)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        *result = GAME_SESSION_IO_ERROR;
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        *result = GAME_SESSION_IO_ERROR;
        return NULL;
    }
    long end = ftell(file);
    if (end < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        *result = GAME_SESSION_IO_ERROR;
        return NULL;
    }
    *size = (size_t)end;
    unsigned char *bytes = malloc(*size ? *size : 1);
    if (!bytes) {
        fclose(file);
        *result = GAME_SESSION_OUT_OF_MEMORY;
        return NULL;
    }
    bool read_ok = !*size || fread(bytes, 1, *size, file) == *size;
    bool close_ok = fclose(file) == 0;
    if (!read_ok || !close_ok) {
        free(bytes);
        *result = GAME_SESSION_IO_ERROR;
        return NULL;
    }
    return bytes;
}

static bool valid_volume(uint32_t x, uint32_t y, uint32_t z)
{
    if (!x || !y || !z || x > INT_MAX || y > INT_MAX || z > INT_MAX)
        return false;
    uint64_t cells = x;
    if (cells > UINT64_MAX / y) return false;
    cells *= y;
    if (cells > UINT64_MAX / z) return false;
    cells *= z;
    return cells < UINT32_MAX && cells <= SIZE_MAX / sizeof(BlockState) &&
           cells <= SIZE_MAX / sizeof(ScheduledEvent);
}

static bool validate_loaded_world(const World *world)
{
    for (uint32_t index = 0; index < (uint32_t)world->volume; ++index) {
        BlockState state = world->cells[index];
        BlockPos position = world_position(world, index);
        if (!serializable_state(state)) return false;
        if (state.id == BLOCK_REDSTONE_WIRE && !wire_supported(world, position))
            return false;
        if (state.id == BLOCK_REDSTONE_TORCH) {
            Direction support = (Direction)state.data.torch.support_dir;
            if (!torch_supported(world, position, support) ||
                !sim_torch_runtime(world, index)) return false;
        }
    }
    return true;
}

World *save_file_read(const char *path, GameSessionResult *result)
{
    GameSessionResult local_result = GAME_SESSION_CORRUPT_SAVE;
    if (!result) result = &local_result;
    *result = GAME_SESSION_CORRUPT_SAVE;
    if (!path || !*path) {
        *result = GAME_SESSION_INVALID_ARGUMENT;
        return NULL;
    }

    size_t file_size = 0;
    unsigned char *bytes = read_whole_file(path, &file_size, result);
    if (!bytes) return NULL;
    if (file_size < SAVE_HEADER_SIZE ||
        memcmp(bytes, save_magic, sizeof(save_magic)) != 0) {
        free(bytes);
        return NULL;
    }

    BufferReader header = {bytes, file_size, 8, true};
    uint16_t version = reader_u16(&header);
    uint16_t header_size = reader_u16(&header);
    uint32_t flags = reader_u32(&header);
    uint32_t size_x = reader_u32(&header);
    uint32_t size_y = reader_u32(&header);
    uint32_t size_z = reader_u32(&header);
    uint32_t ground_layers = reader_u32(&header);
    uint32_t block_count = reader_u32(&header);
    uint32_t runtime_count = reader_u32(&header);
    uint32_t event_count = reader_u32(&header);
    uint32_t expected_crc = reader_u32(&header);
    uint64_t payload_size = reader_u64(&header);

    if (version != SAVE_VERSION) {
        *result = GAME_SESSION_UNSUPPORTED_SAVE;
        free(bytes);
        return NULL;
    }
    if (!header.ok || header_size != SAVE_HEADER_SIZE || flags != 0 ||
        !valid_volume(size_x, size_y, size_z) || ground_layers > size_y ||
        payload_size != file_size - SAVE_HEADER_SIZE ||
        crc32_bytes(bytes + SAVE_HEADER_SIZE, (size_t)payload_size) != expected_crc) {
        free(bytes);
        return NULL;
    }

    uint64_t volume = (uint64_t)size_x * size_y * size_z;
    if (block_count > volume || runtime_count > volume || event_count > volume ||
        block_count > payload_size / 5 || runtime_count > payload_size / 16 ||
        event_count > payload_size / 12) {
        free(bytes);
        return NULL;
    }

    WorldConfig config = {
        (int)size_x, (int)size_y, (int)size_z, (int)ground_layers
    };
    World *world = world_create(config);
    if (!world) {
        *result = GAME_SESSION_OUT_OF_MEMORY;
        free(bytes);
        return NULL;
    }

    BufferReader payload = {
        bytes + SAVE_HEADER_SIZE, (size_t)payload_size, 0, true
    };
    uint32_t previous = 0;
    for (uint32_t record = 0; record < block_count; ++record) {
        uint32_t index = reader_u32(&payload);
        uint8_t id = reader_u8(&payload);
        if (!payload.ok || index >= world->volume || id >= BLOCK_COUNT ||
            (record && index <= previous)) goto corrupt;
        previous = index;

        BlockState state = block_default((BlockId)id);
        if (id == BLOCK_REDSTONE_WIRE) {
            state.data.wire.power = reader_u8(&payload);
            state.data.wire.connections = reader_u8(&payload);
            state.data.wire.up_connections = reader_u8(&payload);
            uint8_t prefer_dot = reader_u8(&payload);
            if (prefer_dot > 1) goto corrupt;
            state.data.wire.prefer_dot = prefer_dot != 0;
        } else if (id == BLOCK_REDSTONE_TORCH) {
            state.data.torch.support_dir = reader_u8(&payload);
            uint8_t lit = reader_u8(&payload);
            if (lit > 1 || sim_prepare_torch(world) != RS_OK) goto out_of_memory;
            state.data.torch.lit = lit != 0;
            sim_attach_torch(world, index);
        }
        if (!serializable_state(state)) goto corrupt;
        world->cells[index] = state;
    }

    previous = 0;
    for (uint32_t record = 0; record < runtime_count; ++record) {
        uint32_t index = reader_u32(&payload);
        uint8_t off_count = reader_u8(&payload);
        uint8_t off_head = reader_u8(&payload);
        uint16_t burned_remaining = reader_u16(&payload);
        if (!payload.ok || index >= world->volume ||
            world->cells[index].id != BLOCK_REDSTONE_TORCH ||
            (record && index <= previous) || off_count > TORCH_HISTORY_COUNT ||
            off_head >= TORCH_HISTORY_COUNT ||
            (off_count < TORCH_HISTORY_COUNT && off_head != off_count)) goto corrupt;
        previous = index;
        TorchRuntime *runtime = sim_torch_runtime(world, index);
        if (!runtime) goto corrupt;
        runtime->off_count = off_count;
        runtime->off_head = off_head;
        runtime->burned_until = burned_remaining;
        for (int history = 0; history < TORCH_HISTORY_COUNT; ++history) {
            uint8_t age = reader_u8(&payload);
            if (age > TORCH_HISTORY_EXPIRED_AGE) goto corrupt;
            runtime->off_times[history] = (uint64_t)0 - age;
        }
    }

    if (event_count && !scheduler_reserve(&world->sim->scheduler, event_count))
        goto out_of_memory;
    previous = 0;
    for (uint32_t record = 0; record < event_count; ++record) {
        uint32_t index = reader_u32(&payload);
        uint64_t remaining = reader_u64(&payload);
        if (!payload.ok || index >= world->volume || !remaining ||
            (record && index <= previous) ||
            !block_definition((BlockId)world->cells[index].id)->scheduled_tick ||
            !scheduler_add(&world->sim->scheduler, index, remaining)) goto corrupt;
        previous = index;
    }

    if (!payload.ok || payload.offset != payload.size ||
        !validate_loaded_world(world)) goto corrupt;

    world->tick = 0;
    world->revision = 0;
    *result = GAME_SESSION_OK;
    free(bytes);
    return world;

out_of_memory:
    *result = GAME_SESSION_OUT_OF_MEMORY;
    world_destroy(world);
    free(bytes);
    return NULL;

corrupt:
    *result = GAME_SESSION_CORRUPT_SAVE;
    world_destroy(world);
    free(bytes);
    return NULL;
}
