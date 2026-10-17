#include "storage.h"
#include <stdlib.h>
#include <string.h>

enum { STATE_GLASS = 13 };

int floor_div16(int x) { int q = x / 16; return q - (x % 16 < 0); }
int floor_mod16(int x) { int r = x % 16; return r < 0 ? r + 16 : r; }
size_t section_index(BlockPos p)
{
    return (size_t)floor_mod16(p.y) * 256 + (size_t)floor_mod16(p.z) * 16 +
           (size_t)floor_mod16(p.x);
}
static unsigned compact_faces(unsigned value)
{
    return (value & 3u) | ((value >> 2) & 12u);
}
static uint8_t expand_faces(unsigned value)
{
    return (uint8_t)((value & 3u) | ((value & 12u) << 2));
}
bool state_encode(BlockState s, StateId *id)
{
    if (s.id <= BLOCK_REDSTONE_BLOCK) { *id = s.id; return true; }
    if (s.id == BLOCK_GLASS) { *id = STATE_GLASS; return true; }
    if (s.id == BLOCK_REDSTONE_TORCH) {
        unsigned face = s.data.torch.support_dir;
        if (face == DIR_DOWN) face = 4;
        else if (face == DIR_SOUTH) face = 2;
        else if (face == DIR_NORTH) face = 3;
        else if (face > DIR_WEST) return false;
        *id = (StateId)(3 + face * 2 + s.data.torch.lit); return true;
    }
    if (s.id != BLOCK_REDSTONE_WIRE || s.data.wire.power > 15 ||
        (s.data.wire.connections & ~0x33u) ||
        (s.data.wire.up_connections & ~s.data.wire.connections)) return false;
    *id = (StateId)(16 + s.data.wire.power + (compact_faces(s.data.wire.connections) << 4) +
         (compact_faces(s.data.wire.up_connections) << 8) + (s.data.wire.prefer_dot ? 4096 : 0));
    return true;
}
bool state_decode(StateId id, BlockState *s)
{
    if (id <= 2) { *s = block_default((BlockId)id); return true; }
    if (id >= 3 && id <= 12) {
        static const Direction faces[] = {DIR_EAST, DIR_WEST, DIR_SOUTH, DIR_NORTH, DIR_DOWN};
        *s = block_default(BLOCK_REDSTONE_TORCH);
        s->data.torch.support_dir = (uint8_t)faces[(id - 3) / 2];
        s->data.torch.lit = ((id - 3) & 1u) != 0; return true;
    }
    if (id == STATE_GLASS) { *s = block_default(BLOCK_GLASS); return true; }
    if (id < 16 || id > 8207) return false;
    unsigned v = id - 16;
    unsigned connections = (v >> 4) & 15u, up = (v >> 8) & 15u;
    if (up & ~connections) return false;
    *s = block_default(BLOCK_REDSTONE_WIRE);
    s->data.wire.power = (uint8_t)(v & 15u);
    s->data.wire.connections = expand_faces(connections);
    s->data.wire.up_connections = expand_faces(up);
    s->data.wire.prefer_dot = (v & 4096u) != 0; return true;
}
/* Like MCHPRS, entries never straddle a 64-bit word. */
size_t palette_word_count(unsigned bits)
{
    size_t per_word = 64 / bits;
    return (SECTION_VOLUME + per_word - 1) / per_word;
}
static unsigned entry(const PalettedBitBuffer *b, size_t index)
{
    size_t per_word = 64 / b->bits;
    unsigned shift = (unsigned)(index % per_word) * b->bits;
    return (unsigned)((b->data[index / per_word] >> shift) & ((1u << b->bits) - 1));
}
static void put(PalettedBitBuffer *b, size_t index, unsigned value)
{
    size_t per_word = 64 / b->bits, word = index / per_word;
    unsigned shift = (unsigned)(index % per_word) * b->bits;
    uint64_t mask = ((UINT64_C(1) << b->bits) - 1) << shift;
    b->data[word] = (b->data[word] & ~mask) | ((uint64_t)value << shift);
}
bool palette_init(PalettedBitBuffer *b)
{
    *b = (PalettedBitBuffer){0};
    b->data = calloc(palette_word_count(4), sizeof(uint64_t));
    b->palette = calloc(16, sizeof(StateId));
    if (!b->data || !b->palette) { palette_destroy(b); return false; }
    b->bits = 4; b->palette_count = 1; return true;
}
void palette_destroy(PalettedBitBuffer *b) { free(b->data); free(b->palette); *b = (PalettedBitBuffer){0}; }
StateId palette_get(const PalettedBitBuffer *b, size_t i)
{
    unsigned v = entry(b, i); return b->bits == 15 ? (StateId)v : b->palette[v];
}
bool palette_set(PalettedBitBuffer *b, size_t i, StateId state)
{
    if (i >= SECTION_VOLUME || state >= STATE_ID_LIMIT) return false;
    if (b->bits == 15) { put(b, i, state); return true; }
    size_t p = 0;
    while (p < b->palette_count && b->palette[p] != state) ++p;
    if (p == b->palette_count && p == (size_t)(1u << b->bits)) {
        /* Allocate the complete replacement before changing the live buffer. */
        unsigned bits = b->bits == 8 ? 15 : b->bits + 1;
        PalettedBitBuffer next = {0}; next.bits = (uint8_t)bits;
        next.data = calloc(palette_word_count(bits), sizeof(uint64_t));
        if (bits != 15) next.palette = malloc((size_t)(1u << bits) * sizeof(StateId));
        if (!next.data || (bits != 15 && !next.palette)) { palette_destroy(&next); return false; }
        if (bits != 15) {
            memcpy(next.palette, b->palette, p * sizeof(StateId)); next.palette_count = p;
        }
        for (size_t j = 0; j < SECTION_VOLUME; ++j)
            put(&next, j, bits == 15 ? palette_get(b, j) : entry(b, j));
        palette_destroy(b); *b = next;
        if (bits == 15) { put(b, i, state); return true; }
    }
    if (p == b->palette_count) b->palette[b->palette_count++] = state;
    put(b, i, (unsigned)p); return true;
}
ChunkSection *section_create(void)
{
    ChunkSection *s = calloc(1, sizeof(*s));
    if (s && !palette_init(&s->blocks)) { free(s); return NULL; }
    return s;
}
void section_destroy(ChunkSection *s)
{
    if (!s) return;
    palette_destroy(&s->blocks); free(s->changed_blocks); free(s);
}
StateId section_get(const ChunkSection *s, size_t i)
{
    if (!s) return 0;
    return s->changed_blocks && s->changed_blocks[i] != UINT16_MAX ?
           s->changed_blocks[i] : palette_get(&s->blocks, i);
}
bool section_set(ChunkSection *s, size_t i, StateId state)
{
    if (i >= SECTION_VOLUME || state >= STATE_ID_LIMIT) return false;
    StateId old = section_get(s, i);
    if (old == state) return true;
    if (!s->changed_blocks) {
        s->changed_blocks = malloc(SECTION_VOLUME * sizeof(StateId));
        if (!s->changed_blocks) return false;
        for (size_t j = 0; j < SECTION_VOLUME; ++j) s->changed_blocks[j] = UINT16_MAX;
    }
    s->changed_blocks[i] = state;
    if (!old) ++s->block_count;
    if (!state) --s->block_count;
    return true;
}
bool section_compact(ChunkSection *s)
{
    if (!s->changed_blocks) return true;
    PalettedBitBuffer next;
    if (!palette_init(&next)) return false;
    for (size_t i = 0; i < SECTION_VOLUME; ++i) if (!palette_set(&next, i, section_get(s, i))) {
        palette_destroy(&next); return false;
    }
    palette_destroy(&s->blocks); s->blocks = next;
    free(s->changed_blocks); s->changed_blocks = NULL; return true;
}
