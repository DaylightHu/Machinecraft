#include "posmap.h"
#include <stdlib.h>
#include <string.h>

bool pos_equal(BlockPos a, BlockPos b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
int pos_compare(BlockPos a, BlockPos b)
{
    if (a.y != b.y) return a.y < b.y ? -1 : 1;
    if (a.z != b.z) return a.z < b.z ? -1 : 1;
    return (a.x > b.x) - (a.x < b.x);
}
static uint64_t mix(uint64_t x)
{
    x ^= x >> 30; x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27; x *= UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}
static size_t hash(BlockPos p)
{
    return (size_t)mix(mix((uint32_t)p.x) ^ mix((uint32_t)p.y + UINT64_C(0x100000000)) ^
                       mix((uint32_t)p.z + UINT64_C(0x200000000)));
}
static size_t find(const PosMap *m, BlockPos key)
{
    size_t i = hash(key) & (m->capacity - 1);
    while (m->entries[i].status &&
           !(m->entries[i].status == 1 && pos_equal(m->entries[i].key, key)))
        i = (i + 1) & (m->capacity - 1);
    return i;
}
bool posmap_get(const PosMap *m, BlockPos key, size_t *value)
{
    if (!m->capacity) return false;
    size_t i = find(m, key);
    if (m->entries[i].status != 1) return false;
    if (value) *value = m->entries[i].value;
    return true;
}
bool posmap_reserve(PosMap *m, size_t count)
{
    if (count < m->count) count = m->count;
    size_t capacity = m->capacity ? m->capacity : 16;
    while (count >= capacity / 2) {
        if (capacity > SIZE_MAX / 2) return false;
        capacity *= 2;
    }
    if (capacity == m->capacity && m->used < capacity * 3 / 4) return true;
    if (capacity > SIZE_MAX / sizeof(PosEntry)) return false;
    PosMap next = {0};
    next.entries = calloc(capacity, sizeof(PosEntry));
    if (!next.entries) return false;
    next.capacity = capacity;
    for (size_t i = 0; i < m->capacity; ++i) if (m->entries[i].status == 1) {
        size_t slot = find(&next, m->entries[i].key);
        next.entries[slot] = m->entries[i];
        ++next.count; ++next.used;
    }
    free(m->entries); *m = next;
    return true;
}
bool posmap_set(PosMap *m, BlockPos key, size_t value)
{
    if (m->capacity) {
        size_t i = find(m, key);
        if (m->entries[i].status == 1) { m->entries[i].value = value; return true; }
    }
    if (!posmap_reserve(m, m->count + 1)) return false;
    size_t i = hash(key) & (m->capacity - 1);
    while (m->entries[i].status == 1) i = (i + 1) & (m->capacity - 1);
    if (!m->entries[i].status) ++m->used;
    m->entries[i] = (PosEntry){key, value, 1}; ++m->count;
    return true;
}
void posmap_remove(PosMap *m, BlockPos key)
{
    if (!m->capacity) return;
    size_t i = find(m, key);
    if (m->entries[i].status == 1) { m->entries[i].status = 2; --m->count; }
}
void posmap_clear(PosMap *m)
{
    if (m->capacity) memset(m->entries, 0, m->capacity * sizeof(PosEntry));
    m->count = m->used = 0;
}
void posmap_destroy(PosMap *m) { free(m->entries); *m = (PosMap){0}; }
