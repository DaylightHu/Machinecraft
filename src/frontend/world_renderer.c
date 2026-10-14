#include "world_renderer.h"
#include "raymath.h"
#include "rlgl.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { REGION_SIZE = 16 };
enum { SOLIDS, DUST, TORCHES, PARTS };

typedef struct {
    BlockState state;
    uint32_t first_vertex;
    uint8_t solid_faces;
    bool pending;
} RenderCell;

typedef struct {
    Mesh mesh;
    unsigned uv_first, uv_end;
    int vertices_per_quad;
    bool dirty, rebuilt;
} RegionPart;

typedef struct { RegionPart parts[PARTS]; } RenderRegion;

struct WorldRenderer {
    World *world;
    WorldConfig config;
    const BlockRenderer *blocks;
    RenderCell *cells;
    uint32_t *pending;
    size_t pending_count, region_count;
    int regions_x, regions_z;
    RenderRegion *regions;
    Material materials[PARTS];
};

static int part_for(unsigned id)
{
    switch (id) {
    case BLOCK_IRON: case BLOCK_REDSTONE_BLOCK: return SOLIDS;
    case BLOCK_REDSTONE_WIRE: return DUST;
    case BLOCK_REDSTONE_TORCH: return TORCHES;
    default: return -1;
    }
}

static size_t cell_index(const WorldRenderer *r, BlockPos p)
{
    return ((size_t)p.y*r->config.size_z + p.z)*r->config.size_x + p.x;
}

static size_t region_index(const WorldRenderer *r, BlockPos p)
{
    return ((size_t)(p.y/REGION_SIZE)*r->regions_z + p.z/REGION_SIZE)*r->regions_x + p.x/REGION_SIZE;
}

static BlockPos cell_position(const WorldRenderer *r, uint32_t index)
{
    BlockPos p;
    p.x = (int)(index % (unsigned)r->config.size_x);
    index /= (unsigned)r->config.size_x;
    p.z = (int)(index % (unsigned)r->config.size_z);
    p.y = (int)(index / (unsigned)r->config.size_z);
    return p;
}

static bool inside(const WorldRenderer *r, BlockPos p)
{
    return p.x >= 0 && p.x < r->config.size_x &&
           p.y >= 0 && p.y < r->config.size_y &&
           p.z >= 0 && p.z < r->config.size_z;
}

static bool solid_at(const WorldRenderer *r, BlockPos p)
{
    return inside(r,p) && part_for(r->cells[cell_index(r,p)].state.id) == SOLIDS;
}

static unsigned wire_sides(BlockState state)
{
    const unsigned horizontal = (1u << DIR_EAST) | (1u << DIR_WEST) |
                                (1u << DIR_SOUTH) | (1u << DIR_NORTH);
    return state.data.wire.connections & state.data.wire.up_connections & horizontal;
}

static void on_change(void *context, BlockPos p, BlockState before, BlockState after)
{
    WorldRenderer *r = context;
    size_t index = cell_index(r,p);
    RenderCell *cell = &r->cells[index];
    int old_part = part_for(before.id), new_part = part_for(after.id);
    RenderRegion *region = &r->regions[region_index(r,p)];
    bool geometry = old_part != new_part;
    bool visual = before.id != after.id;
    if (old_part == new_part && new_part == DUST) {
        geometry = wire_sides(before) != wire_sides(after);
        visual = geometry || before.data.wire.connections != after.data.wire.connections ||
                 (before.data.wire.power > 0) != (after.data.wire.power > 0);
    } else if (old_part == new_part && new_part == TORCHES) {
        geometry = before.data.torch.support_dir != after.data.torch.support_dir;
        visual = geometry || before.data.torch.lit != after.data.torch.lit;
    }
    cell->state = after;
    if (!visual && !geometry) return;

    if (geometry) {
        if (old_part >= 0) region->parts[old_part].dirty = true;
        if (new_part >= 0) region->parts[new_part].dirty = true;
        cell->first_vertex = UINT32_MAX;
    }
    if ((old_part == SOLIDS) != (new_part == SOLIDS)) {
        static const BlockPos offsets[] = {{0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
        for (int face = 0; face < 6; ++face) {
            BlockPos neighbor = {p.x+offsets[face].x,p.y+offsets[face].y,p.z+offsets[face].z};
            if (solid_at(r,neighbor))
                r->regions[region_index(r,neighbor)].parts[SOLIDS].dirty = true;
        }
    }
    if (!cell->pending) {
        cell->pending = true;
        r->pending[r->pending_count++] = (uint32_t)index;
    }
}

static unsigned visible_faces(const WorldRenderer *r, BlockPos p)
{
    static const BlockPos offsets[] = {{0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
    unsigned mask = 0;
    for (int face = 0; face < 6; ++face) {
        BlockPos n = {p.x+offsets[face].x,p.y+offsets[face].y,p.z+offsets[face].z};
        if (!solid_at(r,n)) mask |= 1u << face;
    }
    return mask;
}

static unsigned bit_count(unsigned bits)
{
    unsigned count = 0;
    for (; bits; bits &= bits-1) ++count;
    return count;
}

static BlockPos region_origin(const WorldRenderer *r, size_t index)
{
    int x = (int)(index % (size_t)r->regions_x);
    index /= (size_t)r->regions_x;
    int z = (int)(index % (size_t)r->regions_z);
    return (BlockPos){x*REGION_SIZE,(int)(index/(size_t)r->regions_z)*REGION_SIZE,z*REGION_SIZE};
}

static bool rebuild_part(WorldRenderer *r, size_t region_id, int kind)
{
    RegionPart *part = &r->regions[region_id].parts[kind];
    BlockPos origin = region_origin(r,region_id);
    int end_x = origin.x+REGION_SIZE, end_y = origin.y+REGION_SIZE, end_z = origin.z+REGION_SIZE;
    if (end_x > r->config.size_x) end_x = r->config.size_x;
    if (end_y > r->config.size_y) end_y = r->config.size_y;
    if (end_z > r->config.size_z) end_z = r->config.size_z;
    int quads = 0;
    for (int y = origin.y; y < end_y; ++y)
    for (int z = origin.z; z < end_z; ++z)
    for (int x = origin.x; x < end_x; ++x) {
        BlockPos p = {x,y,z};
        RenderCell *cell = &r->cells[cell_index(r,p)];
        if (part_for(cell->state.id) != kind) continue;
        if (kind == SOLIDS) {
            cell->solid_faces = (uint8_t)visible_faces(r,p);
            quads += (int)bit_count(cell->solid_faces);
        } else if (kind == DUST) quads += 1+(int)bit_count(wire_sides(cell->state));
        else quads += 6;
    }

    Mesh mesh = {0};
    /* Ordinary regions use indexed quads. Extremely dense cutout regions
     * fall back to non-indexed triangles, avoiding 16-bit index overflow. */
    int stride = quads*4 <= UINT16_MAX ? 4 : 6;
    mesh.vertexCount = quads*stride;
    mesh.triangleCount = quads*2;
    if (quads) {
        mesh.vertices = MemAlloc((unsigned)mesh.vertexCount*3*sizeof(float));
        mesh.texcoords = MemAlloc((unsigned)mesh.vertexCount*2*sizeof(float));
        if (stride == 4) mesh.indices = MemAlloc((unsigned)quads*6*sizeof(unsigned short));
        if (!mesh.vertices || !mesh.texcoords || (stride == 4 && !mesh.indices)) {
            UnloadMesh(mesh);
            return false;
        }
        static const int triangles[] = {0,1,2,0,2,3};
        int next = 0;
        for (int y = origin.y; y < end_y; ++y)
        for (int z = origin.z; z < end_z; ++z)
        for (int x = origin.x; x < end_x; ++x) {
            BlockPos p = {x,y,z};
            RenderCell *cell = &r->cells[cell_index(r,p)];
            if (part_for(cell->state.id) != kind) continue;
            BlockGeometry geometry;
            block_renderer_build_geometry(r->blocks,cell->state,(Vector3){(float)x,(float)y,(float)z},cell->solid_faces,&geometry);
            cell->first_vertex = (uint32_t)next;
            for (int face = 0; face < geometry.quads; ++face) {
                if (stride == 4)
                    for (int i = 0; i < 6; ++i)
                        mesh.indices[(next/4)*6+i] = (unsigned short)(next+triangles[i]);
                for (int i = 0; i < stride; ++i) {
                    int v = stride == 4 ? i : triangles[i];
                    Vector3 p3 = geometry.vertices[face][v];
                    Vector2 p2 = geometry.uv[face][v];
                    mesh.vertices[next*3] = p3.x;
                    mesh.vertices[next*3+1] = p3.y;
                    mesh.vertices[next*3+2] = p3.z;
                    mesh.texcoords[next*2] = p2.x;
                    mesh.texcoords[next*2+1] = p2.y;
                    ++next;
                }
            }
        }
        UploadMesh(&mesh,true);
        if (!mesh.vboId || !mesh.vboId[0] || !mesh.vboId[1]) {
            UnloadMesh(mesh);
            return false;
        }
        /* Positions stay on the GPU. Keep UVs for local/coalesced updates and
         * keep the index pointer required by raylib's indexed draw path. */
        MemFree(mesh.vertices);
        mesh.vertices = NULL;
    }
    UnloadMesh(part->mesh);
    part->mesh = mesh;
    part->vertices_per_quad = stride;
    part->uv_first = UINT32_MAX;
    part->uv_end = 0;
    part->dirty = false;
    part->rebuilt = true;
    return true;
}

static void patch_uv(WorldRenderer *r, uint32_t index)
{
    RenderCell *cell = &r->cells[index];
    int kind = part_for(cell->state.id);
    if (kind < 0 || cell->first_vertex == UINT32_MAX) return;
    BlockPos p = cell_position(r,index);
    RegionPart *part = &r->regions[region_index(r,p)].parts[kind];
    if (part->rebuilt || !part->mesh.texcoords) return;
    Vector2 uv[BLOCK_GEOMETRY_QUADS][4];
    int quads = block_renderer_build_uv(r->blocks,cell->state,cell->solid_faces,uv);
    unsigned first = cell->first_vertex;
    unsigned end = first+(unsigned)(quads*part->vertices_per_quad);
    static const int triangles[] = {0,1,2,0,2,3};
    bool changed = false;
    unsigned next = first;
    for (int face = 0; face < quads; ++face)
        for (int i = 0; i < part->vertices_per_quad; ++i) {
            int v = part->vertices_per_quad == 4 ? i : triangles[i];
            float *target = &part->mesh.texcoords[next*2];
            if (target[0] != uv[face][v].x || target[1] != uv[face][v].y) {
                target[0] = uv[face][v].x;
                target[1] = uv[face][v].y;
                changed = true;
            }
            ++next;
        }
    if (!changed) return;
    if (first < part->uv_first) part->uv_first = first;
    if (end > part->uv_end) part->uv_end = end;
}

WorldRenderer *world_renderer_create(World *world, WorldConfig config,
                                     const BlockRenderer *blocks)
{
    if (!world || !blocks || config.size_x <= 0 || config.size_y <= 0 || config.size_z <= 0 ||
        (size_t)config.size_x*config.size_y*config.size_z != world_volume(world)) return NULL;
    if (!world_in_bounds(world,(BlockPos){config.size_x-1,config.size_y-1,config.size_z-1}) ||
        world_in_bounds(world,(BlockPos){config.size_x,0,0}) ||
        world_in_bounds(world,(BlockPos){0,config.size_y,0}) ||
        world_in_bounds(world,(BlockPos){0,0,config.size_z})) return NULL;
    WorldRenderer *r = calloc(1,sizeof(*r));
    if (!r) return NULL;
    r->config = config;
    r->blocks = blocks;
    r->regions_x = 1+(config.size_x-1)/REGION_SIZE;
    r->regions_z = 1+(config.size_z-1)/REGION_SIZE;
    r->region_count = (size_t)r->regions_x*r->regions_z*(1+(config.size_y-1)/REGION_SIZE);
    size_t volume = world_volume(world);
    r->cells = calloc(volume,sizeof(*r->cells));
    r->pending = malloc(volume*sizeof(*r->pending));
    r->regions = calloc(r->region_count,sizeof(*r->regions));
    if (!r->cells || !r->pending || !r->regions) {
        world_renderer_destroy(r);
        return NULL;
    }
    for (int y = 0; y < config.size_y; ++y)
    for (int z = 0; z < config.size_z; ++z)
    for (int x = 0; x < config.size_x; ++x) {
        BlockPos p = {x,y,z};
        RenderCell *cell = &r->cells[cell_index(r,p)];
        cell->state = world_get(world,p);
        cell->first_vertex = UINT32_MAX;
    }
    const Texture2D textures[] = {blocks->atlas,blocks->wire_atlas,blocks->torch_atlas};
    for (int kind = 0; kind < PARTS; ++kind) {
        r->materials[kind] = LoadMaterialDefault();
        r->materials[kind].maps[MATERIAL_MAP_DIFFUSE].texture = textures[kind];
        if (kind != SOLIDS) r->materials[kind].shader = blocks->cutout_shader;
        for (size_t region = 0; region < r->region_count; ++region)
            r->regions[region].parts[kind].dirty = true;
    }
    r->world = world;
    world_set_change_observer(world,on_change,r);
    return r;
}

void world_renderer_destroy(WorldRenderer *r)
{
    if (!r) return;
    if (r->world) world_set_change_observer(r->world,NULL,NULL);
    if (r->regions)
        for (size_t region = 0; region < r->region_count; ++region)
            for (int kind = 0; kind < PARTS; ++kind)
                UnloadMesh(r->regions[region].parts[kind].mesh);
    /* Materials borrow the block renderer's textures and shaders. */
    for (int kind = 0; kind < PARTS; ++kind) MemFree(r->materials[kind].maps);
    free(r->regions);
    free(r->pending);
    free(r->cells);
    free(r);
}

bool world_renderer_sync(WorldRenderer *r)
{
    if (!r) return false;
    for (size_t region = 0; region < r->region_count; ++region)
        for (int kind = 0; kind < PARTS; ++kind) {
            RegionPart *part = &r->regions[region].parts[kind];
            part->rebuilt = false;
            if (part->dirty && !rebuild_part(r,region,kind)) return false;
        }
    for (size_t i = 0; i < r->pending_count; ++i) {
        uint32_t index = r->pending[i];
        patch_uv(r,index);
        r->cells[index].pending = false;
    }
    r->pending_count = 0;
    for (size_t region = 0; region < r->region_count; ++region)
        for (int kind = 0; kind < PARTS; ++kind) {
            RegionPart *part = &r->regions[region].parts[kind];
            if (part->uv_first == UINT32_MAX || part->uv_end <= part->uv_first) continue;
            int bytes = (int)((part->uv_end-part->uv_first)*2*sizeof(float));
            int offset = (int)(part->uv_first*2*sizeof(float));
            UpdateMeshBuffer(part->mesh,1,part->mesh.texcoords+part->uv_first*2,bytes,offset);
            part->uv_first = UINT32_MAX;
            part->uv_end = 0;
        }
    return true;
}

void world_renderer_draw(WorldRenderer *r)
{
    if (!r) return;
    rlDrawRenderBatchActive();
    Matrix identity = MatrixIdentity();
    for (int kind = 0; kind < PARTS; ++kind)
        for (size_t region = 0; region < r->region_count; ++region) {
            Mesh mesh = r->regions[region].parts[kind].mesh;
            if (!mesh.vertexCount) continue;
            DrawMesh(mesh,r->materials[kind],identity);
        }
}
