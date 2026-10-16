#ifndef RS_STORAGE_H
#define RS_STORAGE_H
#include "rs/world.h"
#include <stddef.h>
typedef uint16_t StateId;
typedef struct {
    uint64_t *data;
    StateId *palette;
    size_t palette_count;
    uint8_t bits;
} PalettedBitBuffer;
typedef struct {
    PalettedBitBuffer blocks;
    StateId *changed_blocks;
    uint16_t block_count;
} ChunkSection;
int floor_div16(int value);
int floor_mod16(int value);
size_t section_index(BlockPos pos);
bool state_encode(BlockState state, StateId *id);
bool state_decode(StateId id, BlockState *state);
bool palette_init(PalettedBitBuffer *buffer);
void palette_destroy(PalettedBitBuffer *buffer);
StateId palette_get(const PalettedBitBuffer *buffer, size_t index);
bool palette_set(PalettedBitBuffer *buffer, size_t index, StateId state);
size_t palette_word_count(unsigned bits);
ChunkSection *section_create(void);
void section_destroy(ChunkSection *section);
StateId section_get(const ChunkSection *section, size_t index);
bool section_set(ChunkSection *section, size_t index, StateId state);
bool section_compact(ChunkSection *section);
#endif
