#include "container.h"
#include "sim_internal.h"
#include <stdlib.h>
#include <string.h>

bool container_is_block(BlockId id)
{
    return id == BLOCK_HOPPER || id == BLOCK_BARREL || id == BLOCK_DROPPER;
}

size_t container_slot_count(BlockId id)
{
    return id == BLOCK_HOPPER ? 5 : id == BLOCK_DROPPER ? 9 :
           id == BLOCK_BARREL ? 27 : 0;
}

#define JAVA_RANDOM_MULTIPLIER UINT64_C(0x5deece66d)
#define JAVA_RANDOM_MASK ((UINT64_C(1) << 48) - 1)

RsResult world_set_dropper_seed(World *w, uint64_t seed)
{
    if (!w) return RS_INVALID_ARGUMENT;
    RsResult result = sim_flush(w);
    if (result != RS_OK) return result;
    uint64_t state = (seed ^ JAVA_RANDOM_MULTIPLIER) & JAVA_RANDOM_MASK;
    if (w->sim->containers.dropper_random == state) return RS_UNCHANGED;
    w->sim->containers.dropper_random = state;
    ++w->revision;
    return RS_OK;
}

static uint32_t dropper_random_next(World *w, uint32_t bound)
{
    uint64_t *state = &w->sim->containers.dropper_random;
    uint32_t bits, value;
    do {
        *state = (*state * JAVA_RANDOM_MULTIPLIER + 11) & JAVA_RANDOM_MASK;
        bits = (uint32_t)(*state >> 17);
        if (!(bound & (bound - 1)))
            return (uint32_t)(((uint64_t)bound * bits) >> 31);
        value = bits % bound;
    } while (bits - value + (bound - 1) > INT32_MAX);
    return value;
}

bool container_stack_valid(ContainerStack s)
{
    return item_stack_valid(s);
}

ContainerRuntime *container_runtime(const World *w, BlockPos pos)
{
    size_t index;
    return w && posmap_get(&w->sim->containers.positions, pos, &index) ?
           &w->sim->containers.items[index] : NULL;
}

uint8_t container_level(const ContainerRuntime *c)
{
    if (!c) return 0;
    float fullness = 0.0f;
    bool nonempty = false;
    for (size_t i = 0; i < container_slot_count(c->id); ++i) {
        ContainerStack s = c->slots[i];
        if (!s.count) continue;
        fullness += (float)s.count / s.limit;
        nonempty = true;
    }
    fullness /= (float)container_slot_count(c->id);
    return (uint8_t)((unsigned)(fullness * 14.0f) + (nonempty ? 1u : 0u));
}

static void fill_barrel(ContainerRuntime *c, uint8_t level)
{
    memset(c->slots, 0, 27 * sizeof(*c->slots));
    unsigned count = level ? ((unsigned)(level - 1) * (27u * 64u) + 13u) / 14u : 0;
    if (level && !count) count = 1;
    for (size_t i = 0; count && i < 27; ++i) {
        unsigned amount = count > 64 ? 64 : count;
        c->slots[i] = (ContainerStack){ITEM_POISONOUS_POTATO,
                                      (uint8_t)amount,
                                      POISONOUS_POTATO_STACK_LIMIT};
        count -= amount;
    }
}

bool container_attach(World *w, BlockPos pos, BlockState state)
{
    if (!container_is_block((BlockId)state.id)) return false;
    if (container_runtime(w, pos)) return true;
    if (state.id == BLOCK_HOPPER && !sim_prepare_block_entities(w, 1)) return false;
    ContainerTable *t = &w->sim->containers;
    if (t->count == t->capacity) {
        size_t capacity = t->capacity ? t->capacity * 2 : 16;
        if (capacity < t->capacity || capacity > SIZE_MAX / sizeof(*t->items)) return false;
        ContainerRuntime *items = realloc(t->items, capacity * sizeof(*items));
        if (!items) return false;
        t->items = items;
        t->capacity = capacity;
    }
    ContainerStack *slots = calloc(container_slot_count(state.id), sizeof(*slots));
    if (!slots) return false;
    if (!posmap_set(&t->positions, pos, t->count)) { free(slots); return false; }
    ContainerRuntime *c = &t->items[t->count++];
    *c = (ContainerRuntime){.pos = pos, .slots = slots, .id = state.id,
                            .tick_slot = SIZE_MAX};
    if (state.id == BLOCK_BARREL) fill_barrel(c, state.data.barrel.level);
    else if (state.id == BLOCK_HOPPER) {
        c->facing = state.data.hopper.facing;
        c->enabled = state.data.hopper.enabled;
        if (!sim_add_block_entity(w, pos, BLOCK_HOPPER, &c->tick_slot)) {
            free(slots); posmap_remove(&t->positions, pos); --t->count;
            return false;
        }
    }
    return true;
}

void container_forget(World *w, BlockPos pos)
{
    ContainerTable *t = &w->sim->containers;
    size_t index;
    if (!posmap_get(&t->positions, pos, &index)) return;
    if (t->items[index].id == BLOCK_HOPPER)
        sim_discard_block_entity(w, t->items[index].tick_slot);
    free(t->items[index].slots);
    posmap_remove(&t->positions, pos);
    --t->count;
    memmove(t->items + index, t->items + index + 1,
            (t->count - index) * sizeof(*t->items));
    for (size_t i = index; i < t->count; ++i)
        (void)posmap_set(&t->positions, t->items[i].pos, i);
}

void container_destroy(World *w)
{
    ContainerTable *t = &w->sim->containers;
    for (size_t i = 0; i < t->count; ++i) free(t->items[i].slots);
    free(t->items);
    posmap_destroy(&t->positions);
    *t = (ContainerTable){0};
}

void container_sync_hopper(World *w, BlockPos pos, BlockState state)
{
    ContainerRuntime *c = container_runtime(w, pos);
    if (c) { c->enabled = state.data.hopper.enabled; c->facing = state.data.hopper.facing; }
}

static void contents_changed(World *w, ContainerRuntime *c)
{
    BlockState state = world_get(w, c->pos);
    uint8_t level = container_level(c);
    if (c->id == BLOCK_HOPPER) state.data.hopper.level = level;
    else if (c->id == BLOCK_DROPPER) state.data.dropper.level = level;
    else state.data.barrel.level = level;
    ++w->revision; /* Contents can change while their comparator level stays equal. */
    (void)world_write_visual(w, c->pos, state);
    sim_notify_analog(w, c->pos); /* Contents do not wake observers or BUD latches. */
}

size_t world_container_size(const World *w, BlockPos pos)
{
    ContainerRuntime *c = container_runtime(w, pos);
    return c ? container_slot_count(c->id) : 0;
}

RsResult world_get_container_slot(const World *w, BlockPos pos, size_t slot, ContainerStack *out)
{
    if (!w || !out) return RS_INVALID_ARGUMENT;
    if (!world_in_bounds(w, pos)) return RS_OUT_OF_BOUNDS;
    ContainerRuntime *c = container_runtime(w, pos);
    if (!c || slot >= container_slot_count(c->id)) return RS_INVALID_ARGUMENT;
    *out = c->slots[slot];
    return RS_OK;
}

RsResult world_set_container_slot(World *w, BlockPos pos, size_t slot, ContainerStack stack)
{
    if (!w || !container_stack_valid(stack)) return RS_INVALID_ARGUMENT;
    if (!world_in_bounds(w, pos)) return RS_OUT_OF_BOUNDS;
    RsResult result = sim_flush(w);
    if (result != RS_OK) return result;
    ContainerRuntime *c = container_runtime(w, pos);
    if (!c || slot >= container_slot_count(c->id)) return RS_INVALID_ARGUMENT;
    ContainerStack old = c->slots[slot];
    if (old.item == stack.item && old.count == stack.count && old.limit == stack.limit)
        return RS_UNCHANGED;
    c->slots[slot] = stack;
    contents_changed(w, c);
    return w->sim->error;
}

RsResult container_set_barrel_level(World *w, BlockPos pos, uint8_t level)
{
    ContainerRuntime *c = container_runtime(w, pos);
    if (!c || c->id != BLOCK_BARREL || level > 15) return RS_INVALID_ARGUMENT;
    if (container_level(c) == level) return RS_UNCHANGED;
    fill_barrel(c, level);
    contents_changed(w, c);
    return w->sim->error;
}

uint8_t world_hopper_cooldown(const World *w, BlockPos pos)
{
    ContainerRuntime *c = container_runtime(w, pos);
    return c && c->id == BLOCK_HOPPER ? c->cooldown : 0;
}

static bool empty(const ContainerRuntime *c)
{
    if (!c) return true;
    for (size_t i = 0; i < container_slot_count(c->id); ++i)
        if (c->slots[i].count) return false;
    return true;
}

static bool full(const ContainerRuntime *c)
{
    if (!c) return true;
    for (size_t i = 0; i < container_slot_count(c->id); ++i)
        if (!c->slots[i].count || c->slots[i].count < c->slots[i].limit) return false;
    return true;
}

static bool move_slot(World *w, ContainerRuntime *source, ContainerRuntime *dest,
                       size_t slot)
{
    if (!source || !dest || source == dest) return false;
    ContainerStack *from = &source->slots[slot];
    if (!from->count) return false;
    for (size_t j = 0; j < container_slot_count(dest->id); ++j) {
        ContainerStack *to = &dest->slots[j];
        if (to->count && (to->item != from->item || to->limit != from->limit ||
                          to->count == to->limit)) continue;
        bool was_empty = empty(dest);
        if (!to->count) { *to = *from; to->count = 0; }
        ++to->count;
        if (!--from->count) *from = (ContainerStack){0};
        if (was_empty && dest->id == BLOCK_HOPPER) {
            unsigned delay = source->id == BLOCK_HOPPER &&
                             dest->last_tick >= source->last_tick ? 7 : 8;
            dest->cooldown = (uint8_t)delay;
        }
        contents_changed(w, source);
        contents_changed(w, dest);
        return true;
    }
    return false;
}

static bool move_one(World *w, ContainerRuntime *source, ContainerRuntime *dest)
{
    if (!source || !dest) return false;
    for (size_t i = 0; i < container_slot_count(source->id); ++i)
        if (move_slot(w, source, dest, i)) return true;
    return false;
}

void container_dispense(World *w, BlockPos pos)
{
    ContainerRuntime *source = container_runtime(w, pos);
    if (!source || source->id != BLOCK_DROPPER) return;
    size_t slot = SIZE_MAX;
    uint32_t seen = 0;
    for (size_t i = 0; i < 9; ++i) {
        if (source->slots[i].count && dropper_random_next(w, ++seen) == 0)
            slot = i;
    }
    if (slot == SIZE_MAX) return;
    BlockState state = world_get(w, pos);
    ContainerRuntime *dest = container_runtime(w,
        block_offset(pos, (Direction)state.data.dropper.facing));
    if (dest) {
        (void)move_slot(w, source, dest, slot);
        return; /* A full or incompatible container never falls back to ejecting. */
    }
    ContainerStack *stack = &source->slots[slot];
    if (!--stack->count) *stack = (ContainerStack){0};
    contents_changed(w, source); /* No entity system: discard the ejected item. */
}

static bool can_work(const World *w, const ContainerRuntime *c)
{
    if (!c->enabled) return false;
    ContainerRuntime *dest = container_runtime(w, block_offset(c->pos, (Direction)c->facing));
    ContainerRuntime *above = container_runtime(w, block_offset(c->pos, DIR_UP));
    return (!empty(c) && !full(dest)) || (!full(c) && !empty(above));
}

void container_tick_hopper(World *w, BlockPos pos)
{
    ContainerRuntime *c = container_runtime(w, pos);
    if (!c || c->id != BLOCK_HOPPER) return;
    if (c->cooldown) --c->cooldown;
    c->last_tick = w->tick;
    if (c->cooldown || !can_work(w, c)) return;
    ContainerRuntime *dest = container_runtime(w, block_offset(c->pos, (Direction)c->facing));
    bool moved = !empty(c) && move_one(w, c, dest);
    if (!full(c)) {
        ContainerRuntime *above = container_runtime(w, block_offset(c->pos, DIR_UP));
        moved = move_one(w, above, c) || moved;
    }
    if (moved) c->cooldown = 8;
}

bool container_chunk_pinned(const World *w, BlockPos chunk)
{
    const ContainerTable *t = &w->sim->containers;
    for (size_t i = 0; i < t->count; ++i) {
        const ContainerRuntime *c = &t->items[i];
        if (c->id != BLOCK_HOPPER) continue;
        int64_t dx = (int64_t)floor_div16(c->pos.x) - chunk.x;
        int64_t dz = (int64_t)floor_div16(c->pos.z) - chunk.z;
        if (dx < -1 || dx > 1 || dz < -1 || dz > 1) continue;
        if (c->cooldown || can_work(w, c)) return true;
    }
    return false;
}
