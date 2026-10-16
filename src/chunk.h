#ifndef RS_CHUNK_H
#define RS_CHUNK_H
#include "storage.h"
#include <stdio.h>
typedef struct {
    unsigned char *data;
    size_t size, capacity, cursor;
    bool ok;
} ByteBuffer;
typedef struct {
    BlockPos key;
    ChunkSection *sections[SECTION_COUNT];
    bool modified, resident;
    fpos_t archive_position;
    size_t archive_size;
} Chunk;
void bytes_init(ByteBuffer *b);
void bytes_destroy(ByteBuffer *b);
void bytes_write(ByteBuffer *b, const void *data, size_t size);
void bytes_u8(ByteBuffer *b, uint8_t v);
void bytes_u16(ByteBuffer *b, uint16_t v);
void bytes_u32(ByteBuffer *b, uint32_t v);
void bytes_u64(ByteBuffer *b, uint64_t v);
uint8_t bytes_read8(ByteBuffer *b);
uint16_t bytes_read16(ByteBuffer *b);
uint32_t bytes_read32(ByteBuffer *b);
uint64_t bytes_read64(ByteBuffer *b);
void bytes_pos(ByteBuffer *b, BlockPos p);
BlockPos bytes_read_pos(ByteBuffer *b);
void chunk_release_sections(Chunk *c);
bool chunk_encode(Chunk *c, ByteBuffer *b);
Chunk *chunk_decode(ByteBuffer *b);
#endif
