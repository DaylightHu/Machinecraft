#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "save_file.h"
#include "sim_internal.h"
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
    SAVE_VERSION = 8,
    SAVE_VERSION_LEGACY = 2,
    SAVE_HEADER_SIZE = 60,
    SAVE_HEADER_V6_SIZE = 56,
    SAVE_HEADER_V5_SIZE = 52,
    SAVE_HEADER_V4_SIZE = 44,
    SAVE_HEADER_OLD_SIZE = 40,
    SAVE_CRC_OFFSET = 36,
    LEGACY_V6_TICK_WALL_TORCH = 23
};
static const unsigned char save_magic[8] = {'M','C','W','O','R','L','D',0};
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


static int compare_chunks(const void *a, const void *b)
{
    const Chunk *first = *(Chunk *const *)a, *second = *(Chunk *const *)b;
    return pos_compare(first->key, second->key);
}
static int compare_torches(const void *a, const void *b)
{
    const TorchRuntime *first = *(TorchRuntime *const *)a, *second = *(TorchRuntime *const *)b;
    return pos_compare(first->pos, second->pos);
}
static int compare_events(const void *a, const void *b)
{
    const ScheduledEvent *first = a, *second = b;
    if (first->due != second->due)
        return first->due < second->due ? -1 : 1;
    if (first->priority != second->priority)
        return first->priority < second->priority ? -1 : 1;
    if (first->sequence != second->sequence)
        return first->sequence < second->sequence ? -1 : 1;
    return 0;
}
static bool runtime_valid(const World *w, const TorchRuntime *t)
{
    if (!t || !t->used || t->off_count > 8 || t->off_head >= 8 ||
        (t->off_count < 8 && t->off_head != t->off_count)) return false;
    for (unsigned i = 0; i < t->off_count; ++i) if (t->off_times[i] > w->tick) return false;
    return t->burned_until <= w->tick || t->burned_until - w->tick <= 160;
}
static bool motion_valid(const World *w, const PistonMotion *motion)
{
    StateId moved_id;
    BlockState placeholder;
    if (!motion || !motion->used ||
        (unsigned)motion->direction >= DIR_COUNT ||
        motion->moved_state.id == BLOCK_AIR ||
        motion->moved_state.id == BLOCK_MOVING_PISTON ||
        (!motion->source &&
         motion->moved_state.id >= BLOCK_INTERNAL_FIRST) ||
        motion->progress > 2 ||
        motion->previous_progress > motion->progress ||
        motion->progress - motion->previous_progress > 1 ||
        !state_encode(motion->moved_state, &moved_id))
        return false;
    placeholder = world_get(w, motion->pos);
    return placeholder.id == BLOCK_MOVING_PISTON &&
           placeholder.data.moving_piston.facing == motion->direction;
}
static bool tick_type_valid(uint8_t type)
{
    BlockId id = type == TICK_WALL_TORCH ? BLOCK_REDSTONE_TORCH : (BlockId)type;
    return type > BLOCK_AIR && type < TICK_BLOCK_TYPES &&
           block_definition(id)->scheduled_tick;
}
static bool piston_links_valid(const World *w)
{
    const PistonMotions *motions = &w->sim->piston_motions;
    for (size_t i = 0; i < motions->count; ++i) {
        const PistonMotion *motion = &motions->items[i];
        if (!motion->used) continue;
        BlockState placeholder = world_get(w, motion->pos);
        bool sticky_payload = motion->moved_state.id == BLOCK_STICKY_PISTON ||
                              motion->moved_state.id == BLOCK_STICKY_PISTON_HEAD;
        if (!motion_valid(w, motion) ||
            (!motion->source && placeholder.data.moving_piston.sticky) ||
            (motion->source &&
             placeholder.data.moving_piston.sticky != sticky_payload))
            return false;
        if (motion->source &&
            (motion->extending ?
                 (!block_is_piston_head((BlockId)motion->moved_state.id) ||
                  motion->moved_state.data.piston.facing != motion->direction) :
                 (!block_is_piston_base((BlockId)motion->moved_state.id) ||
                  motion->moved_state.data.piston.facing != motion->direction ||
                  motion->moved_state.data.piston.extended)))
            return false;
    }

    size_t moving_count = 0;
    for (size_t i = 0; i < w->chunk_count; ++i) {
        const Chunk *chunk = w->chunks[i];
        for (unsigned section = 0; section < SECTION_COUNT; ++section) {
            if (!chunk->sections[section]) continue;
            for (size_t index = 0; index < SECTION_VOLUME; ++index) {
                BlockState state;
                if (!state_decode(section_get(chunk->sections[section], index),
                                  &state))
                    return false;
                BlockPos position = {
                    chunk->key.x*16 + (int)(index & 15),
                    WORLD_MIN_Y + (int)section*16 + (int)(index >> 8),
                    chunk->key.z*16 + (int)((index >> 4) & 15)
                };
                if (state.id == BLOCK_MOVING_PISTON) {
                    ++moving_count;
                    if (!sim_piston_motion(w, position)) return false;
                }
                if (block_is_piston_base((BlockId)state.id) &&
                    state.data.piston.extended) {
                    Direction facing = (Direction)state.data.piston.facing;
                    BlockPos head_position = block_offset(position, facing);
                    BlockState head = world_get(w, head_position);
                    if (block_piston_pair((BlockId)state.id,
                                          (BlockId)head.id) &&
                        head.data.piston.facing == state.data.piston.facing)
                        continue;
                    const PistonMotion *motion =
                        sim_piston_motion(w, head_position);
                    if (head.id != BLOCK_MOVING_PISTON || !motion ||
                        !motion->source || !motion->extending ||
                        motion->direction != facing ||
                        !block_piston_pair((BlockId)state.id,
                                           (BlockId)motion->moved_state.id) ||
                        motion->moved_state.data.piston.facing != facing)
                        return false;
                }
                if (block_is_piston_head((BlockId)state.id)) {
                    Direction facing = (Direction)state.data.piston.facing;
                    BlockState base = world_get(
                        w, block_offset(position, direction_opposite(facing)));
                    if (!block_piston_pair((BlockId)base.id,
                                           (BlockId)state.id) ||
                        !base.data.piston.extended ||
                        base.data.piston.facing != state.data.piston.facing)
                        return false;
                }
            }
        }
    }
    return moving_count == motions->active;
}
static GameSessionResult write_save_contents(const World *world, FILE *file)
{
    World *w = (World *)world;
    Simulator *s = w->sim;
    if (s->error != RS_OK || s->rail_roots.count || s->hold_updates || s->held_count ||
        s->scheduler.ticking_head != s->scheduler.ticking_count ||
        !piston_links_valid(w))
        return GAME_SESSION_SIMULATION_ERROR;
    size_t piston_count = s->piston_events.count - s->piston_events.head;
    size_t motion_count = s->piston_motions.active;
    if (w->chunk_count > UINT32_MAX || s->torch_slots.count > UINT32_MAX || s->torch_used > UINT32_MAX || s->scheduler.count > UINT32_MAX || s->containers.count > UINT32_MAX || piston_count > UINT32_MAX || motion_count > UINT32_MAX)
        return GAME_SESSION_SIMULATION_ERROR;
    Chunk **chunks = w->chunk_count ? malloc(w->chunk_count * sizeof(Chunk *)) : NULL;
    TorchRuntime **torches = s->torch_used ? malloc(s->torch_used * sizeof(TorchRuntime *)) : NULL;
    ScheduledEvent *events = s->scheduler.count ? malloc(s->scheduler.count * sizeof(ScheduledEvent)) : NULL;
    if ((w->chunk_count && !chunks) || (s->torch_used && !torches) || (s->scheduler.count && !events)) {
        free(chunks); free(torches); free(events); return GAME_SESSION_OUT_OF_MEMORY;
    }
    size_t chunk_count = 0, torch_count = 0;
    for (size_t i = 0; i < w->chunk_count; ++i) if (w->chunks[i]->modified) chunks[chunk_count++] = w->chunks[i];
    for (size_t i = 0; i < s->torch_used; ++i) if (s->torches[i].used) torches[torch_count++] = &s->torches[i];
    if (chunk_count > 1) qsort(chunks, chunk_count, sizeof(Chunk *), compare_chunks);
    if (torch_count > 1) qsort(torches, torch_count, sizeof(TorchRuntime *), compare_torches);
    if (s->scheduler.count) memcpy(events, s->scheduler.heap, s->scheduler.count * sizeof(ScheduledEvent));
    if (s->scheduler.count > 1) qsort(events, s->scheduler.count, sizeof(ScheduledEvent), compare_events);
    ByteBuffer b; bytes_init(&b);
    bytes_write(&b, save_magic, 8); bytes_u16(&b, SAVE_VERSION); bytes_u16(&b, SAVE_HEADER_SIZE);
    bytes_u32(&b, (uint32_t)w->config.ground_layers); bytes_u64(&b, w->tick);
    bytes_u32(&b, (uint32_t)chunk_count); bytes_u32(&b, (uint32_t)torch_count);
    bytes_u32(&b, (uint32_t)s->scheduler.count); bytes_u32(&b, 0);
    bytes_u32(&b, (uint32_t)s->containers.count);
    bytes_u64(&b, s->containers.dropper_random);
    bytes_u32(&b, (uint32_t)piston_count);
    bytes_u32(&b, (uint32_t)motion_count);
    GameSessionResult result = GAME_SESSION_OK;
    for (size_t i = 0; i < chunk_count; ++i) {
        ByteBuffer snapshot; bytes_init(&snapshot);
        if (!world_chunk_snapshot(w, chunks[i], &snapshot) || snapshot.size > UINT32_MAX) {
            bytes_destroy(&snapshot); result = GAME_SESSION_IO_ERROR; goto cleanup;
        }
        bytes_u32(&b, (uint32_t)snapshot.size); bytes_write(&b, snapshot.data, snapshot.size);
        bytes_destroy(&snapshot);
    }
    for (size_t i = 0; i < torch_count; ++i) {
        const TorchRuntime *t = torches[i];
        if (!runtime_valid(w, t)) { result = GAME_SESSION_SIMULATION_ERROR; goto cleanup; }
        bytes_pos(&b, t->pos); bytes_u8(&b, t->off_count); bytes_u8(&b, t->off_head);
        bytes_u64(&b, t->burned_until);
        for (unsigned j = 0; j < 8; ++j) bytes_u64(&b, t->off_times[j]);
    }
    for (size_t i = 0; i < s->scheduler.count; ++i) {
        ScheduledEvent e = events[i];
        if (!world_in_bounds(w, e.pos) ||
            (unsigned)e.priority > TICK_EXTREMELY_LOW || !tick_type_valid(e.block)) {
            result = GAME_SESSION_SIMULATION_ERROR; goto cleanup;
        }
        bytes_pos(&b, e.pos);
        /* Absolute times preserve overdue ordering when the per-step tick
         * limit leaves part of a due batch queued for a later game tick. */
        bytes_u64(&b, e.due);
        bytes_u8(&b, (uint8_t)e.priority);
        bytes_u8(&b, e.block);
    }
    for (size_t i = 0; i < s->containers.count; ++i) {
        const ContainerRuntime *c = &s->containers.items[i];
        size_t count = world_container_size(w, c->pos);
        uint64_t cooldown = c->cooldown_until > w->tick ? c->cooldown_until - w->tick : 0;
        if (cooldown > 8 || c->last_tick > w->tick) {
            result = GAME_SESSION_SIMULATION_ERROR; goto cleanup;
        }
        bytes_pos(&b, c->pos); bytes_u8(&b, (uint8_t)count);
        bytes_u8(&b, (uint8_t)cooldown); bytes_u64(&b, c->last_tick);
        for (size_t j = 0; j < count; ++j) {
            ContainerStack stack = c->slots[j];
            if (!container_stack_valid(stack)) {
                result = GAME_SESSION_SIMULATION_ERROR; goto cleanup;
            }
            bytes_u32(&b, stack.item); bytes_u8(&b, stack.count); bytes_u8(&b, stack.limit);
        }
    }
    for (size_t i = s->piston_events.head; i < s->piston_events.count; ++i) {
        PistonEvent e = s->piston_events.items[i];
        if (!world_in_bounds(w, e.pos) || !block_is_piston_base((BlockId)e.block) ||
            e.type > 2 || e.facing >= DIR_COUNT) {
            result = GAME_SESSION_SIMULATION_ERROR; goto cleanup;
        }
        bytes_pos(&b, e.pos); bytes_u8(&b, e.block);
        bytes_u8(&b, e.type); bytes_u8(&b, e.facing);
    }
    for (size_t i = 0; i < s->piston_motions.count; ++i) {
        const PistonMotion *motion = &s->piston_motions.items[i];
        if (!motion->used) continue;
        StateId moved_id;
        if (!motion_valid(w, motion) ||
            !state_encode(motion->moved_state, &moved_id)) {
            result = GAME_SESSION_SIMULATION_ERROR;
            goto cleanup;
        }
        bytes_pos(&b, motion->pos);
        bytes_u16(&b, moved_id);
        bytes_u8(&b, motion->direction);
        /* Vanilla writes progressO and restores both progress fields from it. */
        bytes_u8(&b, motion->previous_progress);
        bytes_u8(&b, (uint8_t)((motion->extending ? 1u : 0u) |
                               (motion->source ? 2u : 0u)));
    }
    if (!b.ok) { result = GAME_SESSION_OUT_OF_MEMORY; goto cleanup; }
    uint32_t crc = crc32_bytes(b.data, b.size);
    for (unsigned i = 0; i < 4; ++i) b.data[SAVE_CRC_OFFSET + i] = (unsigned char)(crc >> (i * 8));
    if (fwrite(b.data, 1, b.size, file) != b.size) result = GAME_SESSION_IO_ERROR;
cleanup:
    bytes_destroy(&b); free(chunks); free(torches); free(events); return result;
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


World *save_file_read(const char *path, GameSessionResult *result)
{
    GameSessionResult local_result;
    if (!result) result = &local_result;
    *result = GAME_SESSION_CORRUPT_SAVE;
    if (!path || !*path) { *result = GAME_SESSION_INVALID_ARGUMENT; return NULL; }
    ByteBuffer b; bytes_init(&b);
    b.data = read_whole_file(path, &b.size, result);
    if (!b.data) return NULL;
    b.capacity = b.size;
    World *w = NULL;
    if (b.size < 12 || memcmp(b.data, save_magic, 8)) goto fail;
    b.cursor = 8;
    unsigned version = bytes_read16(&b), header = bytes_read16(&b);
    if (version != SAVE_VERSION && version != 7 && version != 6 &&
        version != 5 && version != 4 && version != 3 &&
        version != SAVE_VERSION_LEGACY) {
        *result = GAME_SESSION_UNSUPPORTED_SAVE;
        goto fail;
    }
    unsigned expected_header = version >= 7 ? SAVE_HEADER_SIZE :
                               version == 6 ? SAVE_HEADER_V6_SIZE :
                               version == 5 ? SAVE_HEADER_V5_SIZE :
                               version == 4 ? SAVE_HEADER_V4_SIZE : SAVE_HEADER_OLD_SIZE;
    if (header != expected_header || b.size < header) goto fail;
    uint32_t ground = bytes_read32(&b); uint64_t tick = bytes_read64(&b);
    uint32_t chunk_count = bytes_read32(&b), torch_count = bytes_read32(&b), event_count = bytes_read32(&b);
    uint32_t crc = bytes_read32(&b);
    uint32_t container_count = version >= 4 ? bytes_read32(&b) : 0;
    uint64_t dropper_random = version >= 5 ? bytes_read64(&b) : UINT64_C(0x5deece66d);
    uint32_t piston_count = version >= 6 ? bytes_read32(&b) : 0;
    uint32_t motion_count = version >= 7 ? bytes_read32(&b) : 0;
    if (dropper_random >> 48) goto fail;
    memset(b.data + SAVE_CRC_OFFSET, 0, 4);
    uint64_t event_size = version >= 6 ? 22 : version >= 3 ? 21 : 20;
    if (crc32_bytes(b.data, b.size) != crc || ground > 65 ||
        (uint64_t)chunk_count * 20 + (uint64_t)torch_count * 86 +
        (uint64_t)event_count * event_size + (uint64_t)container_count * 52 +
        (uint64_t)piston_count * 15 + (uint64_t)motion_count * 17 >
        b.size - header)
        goto fail;
    w = world_create((WorldConfig){(int)ground});
    if (!w) { *result = GAME_SESSION_OUT_OF_MEMORY; goto fail; }
    w->tick = tick;
    w->sim->containers.dropper_random = dropper_random;
    for (uint32_t i = 0; i < chunk_count; ++i) {
        uint32_t size = bytes_read32(&b);
        if (!b.ok || size > b.size - b.cursor) goto fail;
        ByteBuffer view = {.data = b.data + b.cursor, .size = size, .ok = true};
        Chunk *c = chunk_decode(&view);
        if (!c) goto fail;
        if (!view.ok || view.cursor != view.size || !world_insert_chunk(w, c)) {
            chunk_release_sections(c); free(c); goto fail;
        }
        b.cursor += size;
    }
    size_t actual_torches = 0, actual_containers = 0, actual_motions = 0;
    for (size_t i = 0; i < w->chunk_count; ++i) {
        Chunk *c = w->chunks[i];
        for (unsigned sec = 0; sec < SECTION_COUNT; ++sec) if (c->sections[sec])
            for (size_t j = 0; j < SECTION_VOLUME; ++j) {
                BlockState state;
                if (!state_decode(section_get(c->sections[sec], j), &state)) goto fail;
                if (state.id == BLOCK_DROPPER && version < 5) goto fail;
                if (state.id == BLOCK_MOVING_PISTON) {
                    if (version < 7) goto fail;
                    ++actual_motions;
                }
                BlockPos p = {c->key.x * 16 + (int)(j & 15), WORLD_MIN_Y + (int)sec * 16 + (int)(j >> 8),
                              c->key.z * 16 + (int)((j >> 4) & 15)};
                if (container_is_block((BlockId)state.id)) {
                    ++actual_containers;
                    if (version < 4) {
                        if (state.id == BLOCK_HOPPER) goto fail;
                        if (!container_attach(w, p, state)) { *result = GAME_SESSION_OUT_OF_MEMORY; goto fail; }
                    }
                }
                if (state.id == BLOCK_REDSTONE_WIRE && !wire_supported(w, p)) goto fail;
                if (state.id == BLOCK_REDSTONE_REPEATER && !repeater_supported(w, p))
                    goto fail;
                if (state.id == BLOCK_REDSTONE_COMPARATOR &&
                    !comparator_supported(w, p)) goto fail;
                if (block_is_rail((BlockId)state.id) && !rail_supported(w, p))
                    goto fail;
                if (state.id == BLOCK_REDSTONE_TORCH) {
                    if (!torch_supported(w, p, (Direction)state.data.torch.support_dir)) goto fail;
                    ++actual_torches;
                }
            }
    }
    if (actual_torches != torch_count) goto fail;
    for (uint32_t i = 0; i < torch_count; ++i) {
        BlockPos p = bytes_read_pos(&b);
        TorchRuntime runtime = {.pos = p, .used = true};
        runtime.off_count = bytes_read8(&b); runtime.off_head = bytes_read8(&b);
        runtime.burned_until = bytes_read64(&b);
        for (unsigned j = 0; j < 8; ++j) runtime.off_times[j] = bytes_read64(&b);
        if (!b.ok || world_get(w, p).id != BLOCK_REDSTONE_TORCH ||
            sim_torch_runtime(w, p) || !runtime_valid(w, &runtime)) goto fail;
        if (!sim_attach_torch(w, p)) { *result = GAME_SESSION_OUT_OF_MEMORY; goto fail; }
        *sim_torch_runtime(w, p) = runtime;
    }
    for (uint32_t i = 0; i < event_count; ++i) {
        BlockPos p = bytes_read_pos(&b); uint64_t time = bytes_read64(&b);
        TickPriority priority = version >= 3 ?
            (TickPriority)bytes_read8(&b) : TICK_NORMAL;
        BlockId event_block = (BlockId)world_get(w, p).id;
        uint8_t type = version >= 6 ? bytes_read8(&b) :
                       scheduler_block_type(world_get(w, p));
        if (version == 6 && type == LEGACY_V6_TICK_WALL_TORCH)
            type = TICK_WALL_TORCH;
        if (!b.ok || !world_in_bounds(w, p) ||
            (version < 6 && (!time || time > UINT64_MAX - tick)) ||
            (unsigned)priority > TICK_EXTREMELY_LOW ||
            !tick_type_valid(type) ||
            scheduler_pending(&w->sim->scheduler, p, type)) goto fail;
        if (version == SAVE_VERSION_LEGACY &&
            block_is_piston_base(event_block)) {
            /* Version 2 delayed the whole piston action. Re-evaluate it as a
             * block-event request rather than a motion-completion marker. */
            sim_mark_input(w, p);
            continue;
        }
        if (!scheduler_add_priority(&w->sim->scheduler, p,
                                    type, version >= 6 ? time : tick + time, priority)) {
            *result = GAME_SESSION_OUT_OF_MEMORY;
            goto fail;
        }
    }
    if (version >= 4 && actual_containers != container_count) goto fail;
    for (uint32_t i = 0; i < container_count; ++i) {
        BlockPos p = bytes_read_pos(&b);
        unsigned count = bytes_read8(&b), cooldown = bytes_read8(&b);
        uint64_t last_tick = bytes_read64(&b);
        BlockState state = world_get(w, p);
        if (!b.ok || !container_is_block((BlockId)state.id) || container_runtime(w, p) ||
            count != container_slot_count((BlockId)state.id) || cooldown > 8 ||
            cooldown > UINT64_MAX - tick || last_tick > tick ||
            (state.id != BLOCK_HOPPER && (cooldown || last_tick))) goto fail;
        if (!container_attach(w, p, state)) { *result = GAME_SESSION_OUT_OF_MEMORY; goto fail; }
        ContainerRuntime *c = container_runtime(w, p);
        c->cooldown_until = tick + cooldown; c->last_tick = last_tick;
        for (unsigned j = 0; j < count; ++j) {
            ContainerStack stack;
            stack.item = bytes_read32(&b); stack.count = bytes_read8(&b); stack.limit = bytes_read8(&b);
            if (!b.ok || !container_stack_valid(stack)) goto fail;
            c->slots[j] = stack;
        }
        uint8_t level = state.id == BLOCK_HOPPER ? state.data.hopper.level :
                        state.id == BLOCK_DROPPER ? state.data.dropper.level :
                                                   state.data.barrel.level;
        if (container_level(c) != level) goto fail;
    }
    for (uint32_t i = 0; i < piston_count; ++i) {
        PistonEvent e;
        e.pos = bytes_read_pos(&b); e.block = bytes_read8(&b);
        e.type = bytes_read8(&b); e.facing = bytes_read8(&b);
        if (!b.ok || !world_in_bounds(w, e.pos) ||
            !block_is_piston_base((BlockId)e.block) ||
            e.type > 2 || e.facing >= DIR_COUNT) goto fail;
        size_t count = w->sim->piston_events.pending.count;
        if (!sim_queue_piston_event(w, e)) {
            *result = GAME_SESSION_OUT_OF_MEMORY; goto fail;
        }
        if (w->sim->piston_events.pending.count != count + 1) goto fail;
    }
    if (actual_motions != motion_count) goto fail;
    if (!sim_prepare_piston_motions(w, motion_count)) {
        *result = GAME_SESSION_OUT_OF_MEMORY;
        goto fail;
    }
    for (uint32_t i = 0; i < motion_count; ++i) {
        PistonMotion motion = {0};
        motion.pos = bytes_read_pos(&b);
        StateId moved_id = bytes_read16(&b);
        motion.direction = bytes_read8(&b);
        motion.previous_progress = bytes_read8(&b);
        motion.progress = motion.previous_progress;
        uint8_t flags = bytes_read8(&b);
        motion.extending = (flags & 1u) != 0;
        motion.source = (flags & 2u) != 0;
        if (!b.ok || (flags & ~3u) ||
            !state_decode(moved_id, &motion.moved_state) ||
            !sim_add_piston_motion(w, motion))
            goto fail;
    }
    if (!b.ok || b.cursor != b.size || w->sim->error != RS_OK ||
        w->sim->piston_motions.active != motion_count ||
        !piston_links_valid(w))
        goto fail;
    /* Keep only regions with pending simulation work resident until a view is set. */
    for (size_t i = 0; i < w->chunk_count; ++i)
        if (!sim_chunk_pinned(w, w->chunks[i]->key) && !world_archive_chunk(w, w->chunks[i])) {
            *result = GAME_SESSION_IO_ERROR; goto fail;
        }
    bytes_destroy(&b); *result = GAME_SESSION_OK; return w;
fail:
    if (*result == GAME_SESSION_OK) *result = GAME_SESSION_CORRUPT_SAVE;
    world_destroy(w); bytes_destroy(&b); return NULL;
}
