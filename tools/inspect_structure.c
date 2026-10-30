#include "rs/structure.h"

#include <inttypes.h>
#include <stdio.h>

/* A detached reader: no window, world creation or world mutation. Link against
 * the project's simulation core without GUI dependencies. */
int main(int argc, char **argv)
{
    if (argc!=2) { fprintf(stderr,"Usage: %s FILE.mcs\n",argv[0]); return 2; }
    StructureResult result;
    Structure *s=structure_read(argv[1],&result);
    if (!s) { fprintf(stderr,"%s\n",structure_result_name(result)); return 1; }
    StructureInfo info=structure_info(s);
    printf("size=%" PRIu32 ",%" PRIu32 ",%" PRIu32 " cells=%zu containers=%zu source_tick=%" PRIu64 "\n",
        info.width,info.height,info.length,info.volume,info.container_count,info.source_tick);
    printf("origin=%d,%d,%d anchor=%d,%d,%d\n",info.origin.x,info.origin.y,info.origin.z,
        info.anchor.x,info.anchor.y,info.anchor.z);
    for (uint32_t y=0;y<info.height;++y) for (uint32_t z=0;z<info.length;++z)
        for (uint32_t x=0;x<info.width;++x) {
            BlockPos local={(int)x,(int)y,(int)z}; BlockState state;
            if (!structure_get(s,local,&state)) { structure_destroy(s); return 1; }
            if (state.id==BLOCK_AIR) continue;
            printf("%" PRIu32 ",%" PRIu32 ",%" PRIu32 " %s\n",x,y,z,block_name((BlockId)state.id));
            for (size_t slot=0;slot<structure_container_size(s,local);++slot) {
                ContainerStack stack;
                if (structure_get_container_slot(s,local,slot,&stack)!=STRUCTURE_OK) {
                    structure_destroy(s); return 1;
                }
                if (stack.count) printf("  slot=%zu item=%" PRIu32 " count=%u limit=%u\n",
                    slot,stack.item,(unsigned)stack.count,(unsigned)stack.limit);
            }
        }
    structure_destroy(s); return 0;
}
