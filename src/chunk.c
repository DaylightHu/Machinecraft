#include "chunk.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

void bytes_init(ByteBuffer *b) { *b = (ByteBuffer){.ok = true}; }
void bytes_destroy(ByteBuffer *b) { free(b->data); *b = (ByteBuffer){0}; }
void bytes_write(ByteBuffer *b, const void *data, size_t size)
{
    if (!b->ok || size > SIZE_MAX - b->size) { b->ok = false; return; }
    size_t needed = b->size + size;
    if (needed > b->capacity) {
        size_t capacity = b->capacity ? b->capacity : 256;
        while (capacity < needed) {
            if (capacity > SIZE_MAX / 2) { capacity = needed; break; }
            capacity *= 2;
        }
        unsigned char *next = realloc(b->data, capacity);
        if (!next) { b->ok = false; return; }
        b->data = next; b->capacity = capacity;
    }
    if (size) memcpy(b->data + b->size, data, size);
    b->size = needed;
}
void bytes_u8(ByteBuffer *b, uint8_t v) { bytes_write(b, &v, 1); }
void bytes_u16(ByteBuffer *b, uint16_t v) { bytes_u8(b, (uint8_t)v); bytes_u8(b, (uint8_t)(v >> 8)); }
void bytes_u32(ByteBuffer *b, uint32_t v) { bytes_u16(b, (uint16_t)v); bytes_u16(b, (uint16_t)(v >> 16)); }
void bytes_u64(ByteBuffer *b, uint64_t v) { bytes_u32(b, (uint32_t)v); bytes_u32(b, (uint32_t)(v >> 32)); }
uint8_t bytes_read8(ByteBuffer *b)
{
    if (!b->ok || b->cursor >= b->size) { b->ok = false; return 0; }
    return b->data[b->cursor++];
}
uint16_t bytes_read16(ByteBuffer *b) { uint16_t v = bytes_read8(b); return (uint16_t)(v | ((uint16_t)bytes_read8(b) << 8)); }
uint32_t bytes_read32(ByteBuffer *b) { uint32_t v = bytes_read16(b); return v | ((uint32_t)bytes_read16(b) << 16); }
uint64_t bytes_read64(ByteBuffer *b) { uint64_t v = bytes_read32(b); return v | ((uint64_t)bytes_read32(b) << 32); }
void bytes_pos(ByteBuffer *b, BlockPos p) { bytes_u32(b, (uint32_t)p.x); bytes_u32(b, (uint32_t)p.y); bytes_u32(b, (uint32_t)p.z); }
static int signed32(uint32_t v) { return v <= INT32_MAX ? (int)v : -1 - (int)(UINT32_MAX - v); }
BlockPos bytes_read_pos(ByteBuffer *b)
{
    BlockPos p; p.x = signed32(bytes_read32(b)); p.y = signed32(bytes_read32(b)); p.z = signed32(bytes_read32(b)); return p;
}
void chunk_release_sections(Chunk *c)
{
    for (size_t i = 0; i < SECTION_COUNT; ++i) { section_destroy(c->sections[i]); c->sections[i] = NULL; }
    c->resident = false;
}
bool chunk_encode(Chunk *c, ByteBuffer *b)
{
    bytes_pos(b, c->key);
    uint32_t mask = 0;
    for (unsigned i = 0; i < SECTION_COUNT; ++i) if (c->sections[i] && c->sections[i]->block_count) mask |= 1u << i;
    bytes_u32(b, mask);
    for (unsigned i = 0; i < SECTION_COUNT; ++i) if (mask & (1u << i)) {
        ChunkSection *s = c->sections[i];
        if (!section_compact(s)) { b->ok = false; return false; }
        PalettedBitBuffer *p = &s->blocks;
        bytes_u8(b, p->bits); bytes_u16(b, s->block_count);
        bytes_u16(b, (uint16_t)p->palette_count);
        for (size_t j = 0; j < p->palette_count; ++j) bytes_u16(b, p->palette[j]);
        for (size_t j = 0; j < palette_word_count(p->bits); ++j) bytes_u64(b, p->data[j]);
    }
    return b->ok;
}
Chunk *chunk_decode(ByteBuffer *b)
{
    Chunk *c = calloc(1, sizeof(*c));
    if (!c) { b->ok = false; return NULL; }
    c->key = bytes_read_pos(b);
    uint32_t mask = bytes_read32(b);
    if (!b->ok || c->key.y || c->key.x < floor_div16(INT32_MIN + 16) ||
        c->key.x > floor_div16(INT32_MAX - 16) ||
        c->key.z < floor_div16(INT32_MIN + 16) || c->key.z > floor_div16(INT32_MAX - 16) ||
        (mask >> SECTION_COUNT)) goto invalid;
    for (unsigned i = 0; i < SECTION_COUNT; ++i) if (mask & (1u << i)) {
        unsigned bits = bytes_read8(b);
        uint16_t count = bytes_read16(b), palette_count = bytes_read16(b);
        if (!b->ok || !count || count > SECTION_VOLUME ||
            !((bits >= 4 && bits <= 8) || bits == 15) ||
            (bits == 15 ? palette_count != 0 : (!palette_count || palette_count > (1u << bits)))) goto invalid;
        size_t words = palette_word_count(bits);
        if ((size_t)palette_count * 2 + words * 8 > b->size - b->cursor) goto invalid;
        ChunkSection *s = calloc(1, sizeof(*s));
        if (!s) goto invalid;
        c->sections[i] = s; s->blocks.bits = (uint8_t)bits; s->blocks.palette_count = palette_count;
        s->blocks.data = malloc(words * sizeof(uint64_t));
        if (bits != 15) s->blocks.palette = malloc((size_t)(1u << bits) * sizeof(StateId));
        if (!s->blocks.data || (bits != 15 && !s->blocks.palette)) goto invalid;
        for (size_t j = 0; j < palette_count; ++j) {
            StateId id = bytes_read16(b); BlockState state;
            if (!state_decode(id, &state)) goto invalid;
            for (size_t k = 0; k < j; ++k) if (s->blocks.palette[k] == id) goto invalid;
            s->blocks.palette[j] = id;
        }
        for (size_t j = 0; j < words; ++j) s->blocks.data[j] = bytes_read64(b);
        for (size_t j = 0; j < SECTION_VOLUME; ++j) {
            size_t per_word = 64 / bits;
            unsigned shift = (unsigned)(j % per_word) * bits;
            uint64_t v = (s->blocks.data[j / per_word] >> shift) & ((1u << bits) - 1);
            if (bits != 15 && v >= palette_count) goto invalid;
            BlockState state;
            if (!state_decode(palette_get(&s->blocks, j), &state)) goto invalid;
            if (state.id != BLOCK_AIR) ++s->block_count;
        }
        if (s->block_count != count) goto invalid;
    }
    c->resident = c->modified = true; return c;
invalid:
    b->ok = false; chunk_release_sections(c); free(c); return NULL;
}
