#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "world_internal.h"
#include "sim_internal.h"
#include "worldgen.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#endif

static FILE *open_backing_file(void)
{
#ifdef _WIN32
    char directory[MAX_PATH], path[MAX_PATH];
    DWORD length = GetTempPathA(MAX_PATH, directory);
    if (!length || length >= MAX_PATH || !GetTempFileNameA(directory, "mcw", 0, path)) return NULL;
    HANDLE handle = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                               FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (handle == INVALID_HANDLE_VALUE) { DeleteFileA(path); return NULL; }
    int fd = _open_osfhandle((intptr_t)handle, _O_RDWR | _O_BINARY);
    if (fd < 0) { CloseHandle(handle); return NULL; }
    FILE *file = _fdopen(fd, "w+b");
    if (!file) _close(fd);
    return file;
#else
    FILE *file = tmpfile();
    if (file) return file;
    /* Some restricted environments have no writable system temp directory. */
    char path[] = ".machinecraft-cache-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return NULL;
    unlink(path);
    file = fdopen(fd, "w+b");
    if (!file) close(fd);
    return file;
#endif
}

WorldConfig world_config_default(void) { return (WorldConfig){.ground_layers = 1}; }
static void invalidate_reads(World *w, bool wire_properties_only)
{
    if (!++w->read_stamp) {
        memset(w->reads, 0, sizeof(w->reads));
        memset(w->wire_powers, 0, sizeof(w->wire_powers));
        w->read_stamp = 1;
    }
    /* SIGNAL_WITHOUT_WIRE cannot depend on dust power or arm properties.
     * Keep these query results across dust-only writes, never across changes
     * to an emitter, conductor, block type, or newly inserted chunk. */
    if (!wire_properties_only && !++w->external_power_stamp) {
        memset(w->wire_powers, 0, sizeof(w->wire_powers));
        w->external_power_stamp = 1;
    }
}
static BlockPos chunk_key(BlockPos p) { return (BlockPos){floor_div16(p.x), 0, floor_div16(p.z)}; }
Chunk *world_find_chunk(const World *w, BlockPos key)
{
    size_t slot; return posmap_get(&w->chunk_map, key, &slot) ? w->chunks[slot] : NULL;
}
bool world_insert_chunk(World *w, Chunk *c)
{
    if (world_find_chunk(w, c->key)) return false;
    if (w->chunk_count == w->chunk_capacity) {
        size_t capacity = w->chunk_capacity ? w->chunk_capacity * 2 : 16;
        if (capacity < w->chunk_capacity || capacity > SIZE_MAX / sizeof(Chunk *)) return false;
        Chunk **next = realloc(w->chunks, capacity * sizeof(Chunk *));
        if (!next) return false;
        w->chunks = next; w->chunk_capacity = capacity;
    }
    if (!posmap_set(&w->chunk_map, c->key, w->chunk_count)) return false;
    w->chunks[w->chunk_count++] = c;
    invalidate_reads(w, false);
    if (c->resident) ++w->loaded_count;
    ++w->topology_revision; return true;
}
static bool restore_chunk(World *w, Chunk *c)
{
    ByteBuffer b; bytes_init(&b);
    if (!w->backing || !c->archive_size || fsetpos(w->backing, &c->archive_position) != 0) return false;
    b.data = malloc(c->archive_size);
    if (!b.data) return false;
    b.size = b.capacity = c->archive_size;
    bool read = fread(b.data, 1, b.size, w->backing) == b.size;
    Chunk *next = read ? chunk_decode(&b) : NULL;
    bool ok = next && b.ok && b.cursor == b.size && pos_equal(next->key, c->key);
    if (ok) {
        memcpy(c->sections, next->sections, sizeof(c->sections));
        memset(next->sections, 0, sizeof(next->sections));
        c->resident = true; ++w->loaded_count; ++w->topology_revision;
    }
    if (next) { chunk_release_sections(next); free(next); }
    bytes_destroy(&b); return ok;
}
Chunk *world_ensure_chunk(World *w, BlockPos key)
{
    Chunk *c = world_find_chunk(w, key);
    if (c) {
        if (!c->resident && !restore_chunk(w, c)) { w->sim->error = RS_INTERNAL_ERROR; return NULL; }
        return c;
    }
    c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->key = key;
    if (!world_generate_chunk(w, c) || !world_insert_chunk(w, c)) {
        chunk_release_sections(c); free(c); return NULL;
    }
    return c;
}
bool world_chunk_snapshot(World *w, Chunk *c, ByteBuffer *b)
{
    if (c->resident) return chunk_encode(c, b);
    if (!w->backing || !c->archive_size || fsetpos(w->backing, &c->archive_position) != 0) return false;
    unsigned char block[4096]; size_t remaining = c->archive_size;
    while (remaining) {
        size_t count = remaining < sizeof(block) ? remaining : sizeof(block);
        if (fread(block, 1, count, w->backing) != count) return false;
        bytes_write(b, block, count);
        if (!b->ok) return false;
        remaining -= count;
    }
    return true;
}
bool world_archive_chunk(World *w, Chunk *c)
{
    if (!c->resident) return true;
    if (c->modified && !c->archive_size) {
        ByteBuffer b; bytes_init(&b);
        bool ok = chunk_encode(c, &b);
        if (ok && !w->backing) w->backing = open_backing_file();
        fpos_t position;
        ok = ok && w->backing && fseek(w->backing, 0, SEEK_END) == 0 &&
             fgetpos(w->backing, &position) == 0 && fwrite(b.data, 1, b.size, w->backing) == b.size &&
             fflush(w->backing) == 0;
        if (ok) { c->archive_position = position; c->archive_size = b.size; }
        bytes_destroy(&b);
        if (!ok) return false;
    }
    chunk_release_sections(c); --w->loaded_count; ++w->topology_revision; return true;
}
World *world_create(WorldConfig config)
{
    if (config.ground_layers < 0 || config.ground_layers > 65) return NULL;
    World *w = calloc(1, sizeof(*w));
    if (!w) return NULL;
    w->config = config; w->view_radius = -1;
    w->read_stamp = 1;
    w->external_power_stamp = 1;
    if (!sim_init_internal(w)) { world_destroy(w); return NULL; }
    return w;
}
void world_destroy(World *w)
{
    if (!w) return;
    sim_destroy_internal(w);
    for (size_t i = 0; i < w->chunk_count; ++i) { chunk_release_sections(w->chunks[i]); free(w->chunks[i]); }
    free(w->chunks); posmap_destroy(&w->chunk_map);
    if (w->backing) fclose(w->backing);
    free(w);
}
bool world_in_bounds(const World *w, BlockPos p)
{
    /* Leave one chunk of headroom for neighbor arithmetic at integer limits. */
    return w && p.y >= WORLD_MIN_Y && p.y <= WORLD_MAX_Y &&
           p.x >= INT32_MIN + 16 && p.x <= INT32_MAX - 16 &&
           p.z >= INT32_MIN + 16 && p.z <= INT32_MAX - 16;
}
BlockState world_get(const World *w, BlockPos p)
{
    if (!world_in_bounds(w, p)) return block_default(BLOCK_AIR);
    uint32_t hash = (uint32_t)p.x*0x9e3779b1u ^ (uint32_t)p.y*0x85ebca6bu ^
                    (uint32_t)p.z*0xc2b2ae35u;
    /* A per-world value cache, invalidated before notification callbacks on
     * every stored-state write. It never retains chunk/section pointers. */
    WorldReadEntry *entry = &((World *)w)->reads[hash & (WORLD_READ_CACHE_SIZE - 1)];
    if (entry->stamp == w->read_stamp && pos_equal(entry->pos, p)) return entry->state;
    Chunk *c = world_find_chunk(w, chunk_key(p));
    if (!c) {
        BlockState state = world_generated_state(w, p);
        *entry = (WorldReadEntry){p, state, w->read_stamp};
        return state;
    }
    /* Restoring storage is logically const: blocks and simulation time do not change. */
    if (!c->resident && !restore_chunk((World *)w, c)) {
        w->sim->error = RS_INTERNAL_ERROR; return block_default(BLOCK_AIR);
    }
    StateId id = section_get(c->sections[(p.y - WORLD_MIN_Y) / 16], section_index(p));
    BlockState state;
    if (!state_decode(id, &state)) { w->sim->error = RS_INTERNAL_ERROR; return block_default(BLOCK_AIR); }
    *entry = (WorldReadEntry){p, state, w->read_stamp};
    return state;
}
bool world_get_piston_motion(const World *w, BlockPos p,
                             WorldPistonMotion *out)
{
    if (!w || !out) return false;
    const PistonMotion *motion = sim_piston_motion(w, p);
    if (!motion || world_get(w, p).id != BLOCK_MOVING_PISTON) return false;
    *out = (WorldPistonMotion){
        .moved_state = motion->moved_state,
        .last_ticked = motion->last_ticked,
        .direction = (Direction)motion->direction,
        .progress = motion->progress,
        .previous_progress = motion->previous_progress,
        .extending = motion->extending,
        .source = motion->source
    };
    return true;
}
static bool world_write_state(World *w, BlockPos p, BlockState state,
                               BlockState *before, bool *changed)
{
    StateId id;
    if (!world_in_bounds(w, p) || !state_encode(state, &id)) { w->sim->error = RS_INTERNAL_ERROR; return false; }
    BlockState previous = world_get(w, p); StateId old;
    if (!state_encode(previous, &old)) { w->sim->error = RS_INTERNAL_ERROR; return false; }
    if (before) *before = previous;
    if (changed) *changed = old != id;
    if (old == id) return true;
    Chunk *c = world_ensure_chunk(w, chunk_key(p));
    if (!c) { w->sim->error = RS_OUT_OF_MEMORY; return false; }
    unsigned section = (unsigned)(p.y - WORLD_MIN_Y) / 16;
    if (!c->sections[section]) {
        c->sections[section] = section_create();
        if (!c->sections[section]) { w->sim->error = RS_OUT_OF_MEMORY; return false; }
        ++w->topology_revision;
    }
    if (!section_set(c->sections[section], section_index(p), id)) { w->sim->error = RS_OUT_OF_MEMORY; return false; }
    if (!c->sections[section]->block_count) {
        section_destroy(c->sections[section]); c->sections[section] = NULL; ++w->topology_revision;
    }
    c->modified = true; c->archive_size = 0; ++w->revision;
    invalidate_reads(w, previous.id == BLOCK_REDSTONE_WIRE &&
                        state.id == BLOCK_REDSTONE_WIRE);
    if (w->observer) w->observer(w->observer_context, p, previous, state);
    return true;
}
bool world_write_derived(World *w, BlockPos p, BlockState state)
{
    return world_set_block(w, p, state, UPDATE_CLIENTS);
}
bool world_write_visual(World *w, BlockPos p, BlockState state)
{
    return world_write_state(w, p, state, NULL, NULL);
}
bool world_set_block_depth(World *w, BlockPos p, BlockState state,
                           unsigned flags, unsigned remaining_shapes)
{
    BlockState previous;
    bool changed;
    if (!world_write_state(w, p, state, &previous, &changed)) return false;
    if (!changed) return true;
    if (block_states_equal(previous, state)) return true;
    if (scheduler_block_type(previous) != scheduler_block_type(state)) {
        sim_forget_cell(w, p);
        if (state.id == BLOCK_REDSTONE_TORCH && !sim_attach_torch(w, p)) {
            w->sim->error = RS_OUT_OF_MEMORY;
            return false;
        }
        if (container_is_block((BlockId)state.id) && !container_attach(w, p, state)) {
            w->sim->error = RS_OUT_OF_MEMORY;
            return false;
        }
    }
    StateChange change = {p, previous, state, flags, remaining_shapes};
    if (w->sim->hold_updates) return sim_hold_change(w, change);
    sim_state_changed(w, change);
    return w->sim->error == RS_OK;
}
bool world_set_block(World *w, BlockPos p, BlockState state, unsigned flags)
{
    return world_set_block_depth(w, p, state, flags, UPDATE_SHAPE_LIMIT);
}
void world_remove_internal(World *w, BlockPos p)
{
    BlockState removed = world_get(w, p);
    if (removed.id == BLOCK_AIR) return;
    if (block_is_piston_base((BlockId)removed.id) &&
        removed.data.piston.extended) {
        Direction facing = (Direction)removed.data.piston.facing;
        BlockPos head_pos = block_offset(p, facing);
        BlockState head = world_get(w, head_pos);
        if (block_piston_pair((BlockId)removed.id, (BlockId)head.id) &&
            head.data.piston.facing == removed.data.piston.facing) {
            if (!world_set_block(w, head_pos, block_default(BLOCK_AIR), 3))
                return;
        }
    } else if (block_is_piston_head((BlockId)removed.id)) {
        Direction facing = (Direction)removed.data.piston.facing;
        BlockPos base_pos = block_offset(p, direction_opposite(facing));
        BlockState base = world_get(w, base_pos);
        if (block_piston_pair((BlockId)base.id, (BlockId)removed.id) &&
            base.data.piston.extended &&
            base.data.piston.facing == removed.data.piston.facing) {
            if (!world_set_block(w, base_pos, block_default(BLOCK_AIR), 3))
                return;
        }
    }
    (void)world_set_block(w, p, block_default(BLOCK_AIR), 3);
}
RsResult world_place(World *w, BlockPos p, BlockId id, Direction support)
{
    if (!w) return RS_INVALID_ARGUMENT;
    if (!world_in_bounds(w, p)) return RS_OUT_OF_BOUNDS;
    if (id <= BLOCK_AIR || id >= BLOCK_INTERNAL_FIRST)
        return RS_INVALID_ARGUMENT;
    if (block_is_rail(id) && direction_horizontal(support))
        support = support/2 == DIR_EAST/2 ? DIR_EAST : DIR_NORTH;
    if (id == BLOCK_HOPPER) {
        if ((unsigned)support >= DIR_COUNT) return RS_INVALID_ARGUMENT;
        if (support == DIR_UP) support = DIR_DOWN;
    }
    if (id == BLOCK_REDSTONE_WIRE && !wire_supported(w, p)) return RS_UNSUPPORTED;
    if (id == BLOCK_REDSTONE_TORCH) {
        if (support != DIR_DOWN && !direction_horizontal(support)) return RS_INVALID_ARGUMENT;
        if (!torch_supported(w, p, support)) return RS_UNSUPPORTED;
    } else if (id == BLOCK_REDSTONE_REPEATER ||
               id == BLOCK_REDSTONE_COMPARATOR || block_is_rail(id) ||
               id == BLOCK_OBSERVER || block_is_piston_base(id) ||
               id == BLOCK_BARREL || id == BLOCK_DROPPER) {
        if (id != BLOCK_OBSERVER && id != BLOCK_BARREL && id != BLOCK_DROPPER &&
            !block_is_piston_base(id) &&
            !direction_horizontal(support))
            return RS_INVALID_ARGUMENT;
        if ((id == BLOCK_OBSERVER || block_is_piston_base(id) ||
             id == BLOCK_BARREL || id == BLOCK_DROPPER) &&
            (unsigned)support >= DIR_COUNT)
            return RS_INVALID_ARGUMENT;
        if (id == BLOCK_REDSTONE_REPEATER && !repeater_supported(w, p))
            return RS_UNSUPPORTED;
        if (id == BLOCK_REDSTONE_COMPARATOR && !comparator_supported(w, p))
            return RS_UNSUPPORTED;
        if (block_is_rail(id) && !rail_supported(w, p))
            return RS_UNSUPPORTED;
    }
    BlockState previous = world_get(w, p);
    if (previous.id == id &&
        (id != BLOCK_REDSTONE_TORCH || previous.data.torch.support_dir == support) &&
        (id != BLOCK_REDSTONE_REPEATER || previous.data.repeater.facing == support) &&
        (id != BLOCK_REDSTONE_COMPARATOR ||
         previous.data.comparator.facing == support) &&
        (!block_is_rail(id) || previous.data.rail.facing/2 == support/2) &&
        (id != BLOCK_OBSERVER ||
         previous.data.observer.facing == support) &&
        (!block_is_piston_base(id) ||
         previous.data.piston.facing == support) &&
        (id != BLOCK_BARREL || previous.data.barrel.facing == support) &&
        (id != BLOCK_HOPPER || previous.data.hopper.facing == support) &&
        (id != BLOCK_DROPPER || previous.data.dropper.facing == support))
        return RS_UNCHANGED;
    if (id == BLOCK_REDSTONE_TORCH && sim_prepare_torch(w) != RS_OK) return RS_OUT_OF_MEMORY;
    BlockState state = block_default(id);
    if (id == BLOCK_REDSTONE_TORCH) state.data.torch.support_dir = (uint8_t)support;
    if (id == BLOCK_REDSTONE_REPEATER) state.data.repeater.facing = (uint8_t)support;
    if (id == BLOCK_REDSTONE_COMPARATOR)
        state.data.comparator.facing = (uint8_t)support;
    if (block_is_rail(id)) state.data.rail.facing = (uint8_t)support;
    if (id == BLOCK_OBSERVER)
        state.data.observer.facing = (uint8_t)support;
    if (block_is_piston_base(id))
        state.data.piston.facing = (uint8_t)support;
    if (id == BLOCK_BARREL)
        state.data.barrel.facing = (uint8_t)support;
    if (id == BLOCK_HOPPER)
        state.data.hopper.facing = (uint8_t)support;
    if (id == BLOCK_DROPPER)
        state.data.dropper.facing = (uint8_t)support;
    if (id == BLOCK_REDSTONE_LAMP)
        state.data.lamp.lit = power_received(w, p, SIGNAL_ALL) > 0;
    if (id == BLOCK_REDSTONE_WIRE) {
        state.data.wire.connections = 0x33;
        state = wire_connection_state(w, p, state);
    }
    if (id == BLOCK_REDSTONE_REPEATER)
        state.data.repeater.locked = repeater_is_locked(w, p, state);
    if ((block_is_piston_base((BlockId)previous.id) &&
         previous.data.piston.extended) ||
        block_is_piston_head((BlockId)previous.id)) {
        world_remove_internal(w, p);
        if (w->sim->error != RS_OK) return w->sim->error;
    }
    if (!world_set_block(w, p, state, 3)) return w->sim->error;
    if (id == BLOCK_REDSTONE_REPEATER || id == BLOCK_REDSTONE_COMPARATOR) {
        Direction rear = direction_opposite((Direction)(id == BLOCK_REDSTONE_REPEATER ?
            state.data.repeater.facing : state.data.comparator.facing));
        BlockPos input = block_offset(p, rear);
        BlockState source = world_get(w, input);
        bool powered = id == BLOCK_REDSTONE_COMPARATOR ?
            comparator_should_turn_on(w, p, state) :
            power_signal(w, input, direction_opposite(rear), SIGNAL_ALL) > 0 ||
            (source.id == BLOCK_REDSTONE_WIRE && source.data.wire.power > 0);
        if (powered) sim_schedule(w, p, 1);
    }
    return w->sim->error;
}
RsResult world_remove(World *w, BlockPos p)
{
    if (!w) return RS_INVALID_ARGUMENT;
    if (!world_in_bounds(w, p)) return RS_OUT_OF_BOUNDS;
    if (world_get(w, p).id == BLOCK_AIR) return RS_UNCHANGED;
    world_remove_internal(w, p); return w->sim->error;
}
RsResult world_toggle_wire_dot(World *w, BlockPos p)
{
    if (!w) return RS_INVALID_ARGUMENT;
    if (!world_in_bounds(w, p)) return RS_OUT_OF_BOUNDS;
    RsResult r = sim_flush(w); if (r != RS_OK) return r;
    BlockState state = world_get(w, p);
    if (state.id != BLOCK_REDSTONE_WIRE) return RS_INVALID_ARGUMENT;
    if (state.data.wire.connections != 0 && state.data.wire.connections != 0x33)
        return RS_UNCHANGED;
    BlockState previous = state;
    state.data.wire.connections = state.data.wire.connections ? 0 : 0x33;
    state = wire_connection_state(w, p, state);
    if (block_states_equal(previous, state)) return RS_UNCHANGED;
    if (!world_set_block(w, p, state, 3)) return w->sim->error;
    static const Direction horizontal[] = {DIR_NORTH, DIR_EAST, DIR_SOUTH, DIR_WEST};
    for (unsigned i = 0; i < 4; ++i) {
        Direction d = horizontal[i];
        BlockPos next = block_offset(p, d);
        if (((previous.data.wire.connections ^ state.data.wire.connections) & (1u << d)) &&
            block_is_conductor(w, next))
            sim_update_neighbors_at(w, next, direction_opposite(d));
    }
    return w->sim->error;
}
RsResult world_cycle_repeater_delay(World *w, BlockPos p)
{
    if (!w) return RS_INVALID_ARGUMENT;
    if (!world_in_bounds(w, p)) return RS_OUT_OF_BOUNDS;
    RsResult result = sim_flush(w); if (result != RS_OK) return result;
    BlockState state = world_get(w, p);
    if (state.id != BLOCK_REDSTONE_REPEATER) return RS_INVALID_ARGUMENT;
    state.data.repeater.delay = (uint8_t)(state.data.repeater.delay % 4 + 1);
    if (!world_set_block(w, p, state, 3)) return w->sim->error;
    return w->sim->error;
}
RsResult world_toggle_comparator_mode(World *w, BlockPos p)
{
    if (!w) return RS_INVALID_ARGUMENT;
    if (!world_in_bounds(w, p)) return RS_OUT_OF_BOUNDS;
    RsResult result = sim_flush(w); if (result != RS_OK) return result;
    BlockState state = world_get(w, p);
    if (state.id != BLOCK_REDSTONE_COMPARATOR) return RS_INVALID_ARGUMENT;
    state.data.comparator.subtract = !state.data.comparator.subtract;
    if (!world_write_derived(w, p, state)) return w->sim->error;
    comparator_refresh_output(w, p); return w->sim->error;
}
RsResult world_set_barrel_level(World *w, BlockPos p, uint8_t level)
{
    if (!w || level > 15) return RS_INVALID_ARGUMENT;
    if (!world_in_bounds(w, p)) return RS_OUT_OF_BOUNDS;
    RsResult result = sim_flush(w); if (result != RS_OK) return result;
    BlockState state = world_get(w, p);
    if (state.id != BLOCK_BARREL) return RS_INVALID_ARGUMENT;
    return container_set_barrel_level(w, p, level);
}
static bool in_view(const World *w, BlockPos key)
{
    if (w->view_radius < 0) return true;
    int64_t dx = (int64_t)key.x - w->view_center.x, dz = (int64_t)key.z - w->view_center.z;
    return dx >= -w->view_radius && dx <= w->view_radius && dz >= -w->view_radius && dz <= w->view_radius;
}
RsResult world_stream(World *w, BlockPos focus, int radius)
{
    if (!w || radius < 0 || radius > 32) return RS_INVALID_ARGUMENT;
    if (!world_in_bounds(w, (BlockPos){focus.x, 0, focus.z})) return RS_OUT_OF_BOUNDS;
    RsResult result = sim_flush(w); if (result != RS_OK) return result;
    BlockPos center = chunk_key(focus);
    for (int dz = -radius; dz <= radius; ++dz) for (int dx = -radius; dx <= radius; ++dx) {
        BlockPos key = {center.x + dx, 0, center.z + dz};
        int64_t x = (int64_t)key.x * 16, z = (int64_t)key.z * 16;
        if (x < INT32_MIN + 16 || x + 15 > INT32_MAX - 16 || z < INT32_MIN + 16 || z + 15 > INT32_MAX - 16) continue;
        if (!world_ensure_chunk(w, key)) return RS_OUT_OF_MEMORY;
    }
    if (!pos_equal(w->view_center, center) || w->view_radius != radius) ++w->topology_revision;
    w->view_center = center; w->view_radius = radius;
    for (size_t i = 0; i < w->chunk_count;) {
        Chunk *c = w->chunks[i];
        if (c->resident && !in_view(w, c->key) && !sim_chunk_pinned(w, c->key)) {
            if (!world_archive_chunk(w, c)) return RS_INTERNAL_ERROR;
            if (!c->modified) {
                posmap_remove(&w->chunk_map, c->key); free(c);
                --w->chunk_count;
                if (i < w->chunk_count) {
                    w->chunks[i] = w->chunks[w->chunk_count];
                    (void)posmap_set(&w->chunk_map, w->chunks[i]->key, i);
                }
                continue;
            }
        }
        ++i;
    }
    return w->sim->error;
}
void world_visit_sections(const World *w, WorldSectionVisitor visitor, void *context)
{
    if (!w || !visitor) return;
    for (size_t i = 0; i < w->chunk_count; ++i) {
        Chunk *c = w->chunks[i];
        if (!c->resident || !in_view(w, c->key)) continue;
        for (int s = 0; s < SECTION_COUNT; ++s) if (c->sections[s] && c->sections[s]->block_count)
            visitor(context, (BlockPos){c->key.x * 16, WORLD_MIN_Y + s * 16, c->key.z * 16});
    }
}
uint64_t world_tick(const World *w) { return w ? w->tick : 0; }
uint64_t world_revision(const World *w) { return w ? w->revision : 0; }
uint64_t world_topology_revision(const World *w) { return w ? w->topology_revision : 0; }
size_t world_volume(const World *w) { return w ? w->loaded_count * 16 * 384 * 16 : 0; }
size_t world_loaded_chunks(const World *w) { return w ? w->loaded_count : 0; }
size_t world_stored_chunks(const World *w) { return w ? w->chunk_count : 0; }
WorldConfig world_config(const World *w) { return w ? w->config : (WorldConfig){0}; }
void world_set_change_observer(World *w, WorldChangeObserver observer, void *context)
{
    if (!w) return;
    w->observer = observer; w->observer_context = observer ? context : NULL;
}
const char *rs_result_name(RsResult result)
{
    static const char *const names[] = {"ok", "unchanged", "out of bounds", "unsupported", "invalid argument", "out of memory", "internal error"};
    return (unsigned)result < sizeof(names) / sizeof(names[0]) ? names[result] : "unknown";
}
