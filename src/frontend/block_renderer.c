#include "block_renderer.h"
#include "rlgl.h"

enum { TILE_SIZE = 32, TILE_COUNT = 2 };

typedef struct {
    Vector3 normal;
    Vector3 vertices[4];
} CubeFace;

/* Counterclockwise vertices as seen from outside each face. */
static const CubeFace cube_faces[] = {
    {{ 0,  0,  1}, {{-0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f},
                    { 0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f}}},
    {{ 0,  0, -1}, {{ 0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f, -0.5f},
                    {-0.5f,  0.5f, -0.5f}, { 0.5f,  0.5f, -0.5f}}},
    {{ 1,  0,  0}, {{ 0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f, -0.5f},
                    { 0.5f,  0.5f, -0.5f}, { 0.5f,  0.5f,  0.5f}}},
    {{-1,  0,  0}, {{-0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f,  0.5f},
                    {-0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f, -0.5f}}},
    {{ 0,  1,  0}, {{-0.5f,  0.5f,  0.5f}, { 0.5f,  0.5f,  0.5f},
                    { 0.5f,  0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f}}},
    {{ 0, -1,  0}, {{-0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f},
                    { 0.5f, -0.5f,  0.5f}, {-0.5f, -0.5f,  0.5f}}}
};

static Image load_tile(const char *path)
{
    Image image = LoadImage(path);
    if (!image.data) {
        TraceLog(LOG_ERROR, "BLOCK TEXTURE: Cannot load %s", path);
        return (Image){0};
    }
    if (image.width != TILE_SIZE || image.height != TILE_SIZE) {
        TraceLog(LOG_ERROR, "BLOCK TEXTURE: %s must be %dx%d (got %dx%d)",
                 path, TILE_SIZE, TILE_SIZE, image.width, image.height);
        UnloadImage(image);
        return (Image){0};
    }
    return image;
}

bool block_renderer_init(BlockRenderer *renderer)
{
    if (!renderer) return false;
    *renderer = (BlockRenderer){0};

    Image iron = load_tile("res/textures/iron_block.png");
    if (!iron.data) return false;

    Image redstone = load_tile("res/textures/redstone_block.png");
    if (!redstone.data) {
        UnloadImage(iron);
        return false;
    }

    Image atlas = GenImageColor(TILE_SIZE*TILE_COUNT, TILE_SIZE, BLANK);
    if (atlas.data) {
        Rectangle tile = {0, 0, TILE_SIZE, TILE_SIZE};
        ImageDraw(&atlas, iron, tile, tile, WHITE);
        Rectangle right = {TILE_SIZE, 0, TILE_SIZE, TILE_SIZE};
        ImageDraw(&atlas, redstone, tile, right, WHITE);
        renderer->atlas = LoadTextureFromImage(atlas);
        UnloadImage(atlas);
    }
    UnloadImage(redstone);
    UnloadImage(iron);

    if (!renderer->atlas.id) {
        TraceLog(LOG_ERROR, "BLOCK TEXTURE: Cannot create the block atlas");
        return false;
    }

    SetTextureFilter(renderer->atlas, TEXTURE_FILTER_POINT);
    SetTextureWrap(renderer->atlas, TEXTURE_WRAP_CLAMP);
    return true;
}

void block_renderer_unload(BlockRenderer *renderer)
{
    if (!renderer) return;
    if (renderer->atlas.id) UnloadTexture(renderer->atlas);
    *renderer = (BlockRenderer){0};
}

void block_renderer_draw_cube(const BlockRenderer *renderer, BlockId id,
                              Vector3 center)
{
    if (!renderer || !renderer->atlas.id) return;

    int tile;
    switch (id) {
    case BLOCK_IRON:           tile = 0; break;
    case BLOCK_REDSTONE_BLOCK: tile = 1; break;
    default: return;
    }

    /* Inset by half a texel so tile edges cannot sample their neighbor.
     * Image row zero maps to v=0; bottom vertices use the larger v. */
    float u0 = (tile*TILE_SIZE + 0.5f)/renderer->atlas.width;
    float u1 = ((tile + 1)*TILE_SIZE - 0.5f)/renderer->atlas.width;
    float v0 = 0.5f/renderer->atlas.height;
    float v1 = (TILE_SIZE - 0.5f)/renderer->atlas.height;
    const Vector2 uv[4] = {{u0, v1}, {u1, v1}, {u1, v0}, {u0, v0}};

    rlCheckRenderBatchLimit(24);
    rlSetTexture(renderer->atlas.id);
    rlBegin(RL_QUADS);
    /* White tint preserves the source colors instead of multiplying them
     * by the prototype's LIGHTGRAY/red block colors. */
    rlColor4ub(255, 255, 255, 255);
    for (int face = 0; face < 6; ++face) {
        const CubeFace *quad = &cube_faces[face];
        rlNormal3f(quad->normal.x, quad->normal.y, quad->normal.z);
        for (int vertex = 0; vertex < 4; ++vertex) {
            rlTexCoord2f(uv[vertex].x, uv[vertex].y);
            rlVertex3f(center.x + quad->vertices[vertex].x,
                       center.y + quad->vertices[vertex].y,
                       center.z + quad->vertices[vertex].z);
        }
    }
    rlEnd();
    rlSetTexture(0);
}
