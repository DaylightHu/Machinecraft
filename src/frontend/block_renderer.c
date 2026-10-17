#include "block_renderer.h"
#include "rlgl.h"

#include <stddef.h>
#include <string.h>

enum { TILE_SIZE = 32, TILE_COUNT = 2 };
enum {
    WIRE_SHAPES = 16, WIRE_STATES = 2,
    WIRE_SIDE_FIRST = WIRE_SHAPES*WIRE_STATES,
    WIRE_TILES = WIRE_SIDE_FIRST + WIRE_STATES,
    WIRE_COLUMNS = 8,
    WIRE_ROWS = (WIRE_TILES + WIRE_COLUMNS - 1)/WIRE_COLUMNS
};

static const float wire_offset = 0.002f;
static const Direction wire_directions[] = {
    DIR_EAST, DIR_WEST, DIR_SOUTH, DIR_NORTH
};

/* Match raylib's default desktop vertex shader; only binary-alpha pixels
 * survive, so transparent texels cannot write depth or cover other blocks. */
static const char cutout_fragment_shader[] =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec4 texel = texture(texture0, fragTexCoord);\n"
    "    if (texel.a < 0.5) discard;\n"
    "    finalColor = texel*fragColor*colDiffuse;\n"
    "}\n";

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

/* Vanilla 1.20.4-style torch: four full-texture side sheets and two caps.
 * Each source pixel covers 1/32 of a block. The supplied sprites have a
 * four-pixel-wide stem (x=14..17) and a body top at row 7. The lit sprite's
 * extra pixels extend beyond these sheets' central stem, including above
 * its cap; alpha discard removes every transparent pixel. */
static const CubeFace torch_faces[] = {
    {{ 0,  0,  1}, {{-0.5f, 0,  0.0625f}, { 0.5f, 0,  0.0625f},
                    { 0.5f, 1,  0.0625f}, {-0.5f, 1,  0.0625f}}},
    {{ 0,  0, -1}, {{ 0.5f, 0, -0.0625f}, {-0.5f, 0, -0.0625f},
                    {-0.5f, 1, -0.0625f}, { 0.5f, 1, -0.0625f}}},
    {{ 1,  0,  0}, {{ 0.0625f, 0,  0.5f}, { 0.0625f, 0, -0.5f},
                    { 0.0625f, 1, -0.5f}, { 0.0625f, 1,  0.5f}}},
    {{-1,  0,  0}, {{-0.0625f, 0, -0.5f}, {-0.0625f, 0,  0.5f},
                    {-0.0625f, 1,  0.5f}, {-0.0625f, 1, -0.5f}}},
    {{ 0,  1,  0}, {{-0.0625f, 0.78125f,  0.0625f}, { 0.0625f, 0.78125f,  0.0625f},
                    { 0.0625f, 0.78125f, -0.0625f}, {-0.0625f, 0.78125f, -0.0625f}}},
    {{ 0, -1,  0}, {{-0.0625f, 0, -0.0625f}, { 0.0625f, 0, -0.0625f},
                    { 0.0625f, 0,  0.0625f}, {-0.0625f, 0,  0.0625f}}}
};

typedef struct {
    Vector3 origin;
    Vector3 x_axis, y_axis, z_axis;
} TorchTransform;

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

static Rectangle wire_tile_rectangle(int tile)
{
    return (Rectangle){(tile%WIRE_COLUMNS)*TILE_SIZE,
                       (tile/WIRE_COLUMNS)*TILE_SIZE, TILE_SIZE, TILE_SIZE};
}

static Image create_wire_atlas(void)
{
    static const char *const paths[] = {
        "res/textures/block/redstone_dot0.png",
        "res/textures/block/redstone_dot1.png",
        "res/textures/block/redstone_wire0.png", 
        "res/textures/block/redstone_wire1.png"
    };
    /* The source arm points down in image space (+Z / south in the world).
     * CW turns map south -> west -> north -> east. */
    static const int clockwise_turns[] = {3, 1, 0, 2};
    Image sources[4] = {0};
    Image arms[4] = {0};
    Image atlas = {0};
    Rectangle full = {0, 0, TILE_SIZE, TILE_SIZE};
    bool complete = false;

    for (int index = 0; index < 4; ++index) {
        sources[index] = load_tile(paths[index]);
        if (!sources[index].data) goto cleanup;
    }
    atlas = GenImageColor(WIRE_COLUMNS*TILE_SIZE, WIRE_ROWS*TILE_SIZE, BLANK);
    if (!atlas.data) goto cleanup;

    for (int powered = 0; powered < WIRE_STATES; ++powered) {
        for (int direction = 0; direction < 4; ++direction) {
            arms[direction] = ImageCopy(sources[2 + powered]);
            if (!arms[direction].data) goto cleanup;
            for (int turn = 0; turn < clockwise_turns[direction]; ++turn)
                ImageRotateCW(&arms[direction]);
        }
        for (int mask = 0; mask < WIRE_SHAPES; ++mask) {
            Rectangle destination = wire_tile_rectangle(powered*WIRE_SHAPES + mask);
            for (int direction = 0; direction < 4; ++direction) {
                if (mask & (1 << direction))
                    ImageDraw(&atlas, arms[direction], full, destination, WHITE);
            }
            /* Opposite pairs form straight lines without a center dot. */
            bool straight = (mask == 3 || mask == 12);
            if (!straight)
                ImageDraw(&atlas, sources[powered], full, destination, WHITE);
        }

        /* Climbing strips use a full line without a dot in the middle. */
        Rectangle side = wire_tile_rectangle(WIRE_SIDE_FIRST + powered);
        ImageDraw(&atlas, arms[2], full, side, WHITE);
        ImageDraw(&atlas, arms[3], full, side, WHITE);
        for (int direction = 0; direction < 4; ++direction) {
            UnloadImage(arms[direction]);
            arms[direction] = (Image){0};
        }
    }
    complete = true;

cleanup:
    for (int index = 0; index < 4; ++index) {
        if (sources[index].data) UnloadImage(sources[index]);
        if (arms[index].data) UnloadImage(arms[index]);
    }
    if (!complete) {
        if (atlas.data) UnloadImage(atlas);
        return (Image){0};
    }
    return atlas;
}

static Image create_torch_atlas(void)
{
    static const char *const paths[] = {
        "res/textures/block/redstone_torch0.png", 
        "res/textures/block/redstone_torch1.png"
    };
    Image sources[2] = {0};
    Image atlas = {0};
    for (int lit = 0; lit < 2; ++lit) {
        sources[lit] = load_tile(paths[lit]);
        if (!sources[lit].data) goto cleanup;
    }

    atlas = GenImageColor(2*TILE_SIZE, TILE_SIZE, BLANK);
    if (atlas.data) {
        Rectangle full = {0, 0, TILE_SIZE, TILE_SIZE};
        for (int lit = 0; lit < 2; ++lit) {
            Rectangle destination = {lit*TILE_SIZE, 0, TILE_SIZE, TILE_SIZE};
            ImageDraw(&atlas, sources[lit], full, destination, WHITE);
        }
    }

cleanup:
    for (int lit = 0; lit < 2; ++lit)
        if (sources[lit].data) UnloadImage(sources[lit]);
    return atlas;
}

bool block_renderer_init(BlockRenderer *renderer)
{
    if (!renderer) return false;
    *renderer = (BlockRenderer){0};

    Image iron = load_tile("res/textures/block/iron_block.png");
    if (!iron.data) return false;

    Image redstone = load_tile("res/textures/block/redstone_block.png");
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

    Image glass = load_tile("res/textures/block/glass.png");
    if (glass.data) {
        renderer->glass_texture = LoadTextureFromImage(glass);
        UnloadImage(glass);
    }
    if (!renderer->glass_texture.id) {
        TraceLog(LOG_ERROR, "BLOCK TEXTURE: Cannot load the glass texture");
        block_renderer_unload(renderer);
        return false;
    }
    SetTextureFilter(renderer->glass_texture, TEXTURE_FILTER_POINT);
    SetTextureWrap(renderer->glass_texture, TEXTURE_WRAP_CLAMP);

    Image wire_atlas = create_wire_atlas();
    if (wire_atlas.data) {
        renderer->wire_atlas = LoadTextureFromImage(wire_atlas);
        UnloadImage(wire_atlas);
    }
    if (!renderer->wire_atlas.id) {
        TraceLog(LOG_ERROR, "BLOCK TEXTURE: Cannot create the redstone dust atlas");
        block_renderer_unload(renderer);
        return false;
    }
    SetTextureFilter(renderer->wire_atlas, TEXTURE_FILTER_POINT);
    SetTextureWrap(renderer->wire_atlas, TEXTURE_WRAP_CLAMP);

    Image torch_atlas = create_torch_atlas();
    if (torch_atlas.data) {
        renderer->torch_atlas = LoadTextureFromImage(torch_atlas);
        UnloadImage(torch_atlas);
    }
    if (!renderer->torch_atlas.id) {
        TraceLog(LOG_ERROR, "BLOCK TEXTURE: Cannot create the redstone torch atlas");
        block_renderer_unload(renderer);
        return false;
    }
    SetTextureFilter(renderer->torch_atlas, TEXTURE_FILTER_POINT);
    SetTextureWrap(renderer->torch_atlas, TEXTURE_WRAP_CLAMP);

    renderer->cutout_shader = LoadShaderFromMemory(NULL, cutout_fragment_shader);
    if (renderer->cutout_shader.id == rlGetShaderIdDefault()) {
        /* LoadShaderFromMemory allocates its own locations even on fallback. */
        MemFree(renderer->cutout_shader.locs);
        renderer->cutout_shader = (Shader){0};
    }
    if (!renderer->cutout_shader.id || !renderer->cutout_shader.locs) {
        TraceLog(LOG_ERROR, "BLOCK TEXTURE: Cannot load the cutout shader (OpenGL 3.3)");
        block_renderer_unload(renderer);
        return false;
    }
    return true;
}

void block_renderer_unload(BlockRenderer *renderer)
{
    if (!renderer) return;
    if (renderer->cutout_shader.id) UnloadShader(renderer->cutout_shader);
    if (renderer->torch_atlas.id) UnloadTexture(renderer->torch_atlas);
    if (renderer->wire_atlas.id) UnloadTexture(renderer->wire_atlas);
    if (renderer->glass_texture.id) UnloadTexture(renderer->glass_texture);
    if (renderer->atlas.id) UnloadTexture(renderer->atlas);
    *renderer = (BlockRenderer){0};
}

void block_renderer_draw_cube(const BlockRenderer *renderer, BlockId id,
                              Vector3 center)
{
    if (!renderer || !renderer->atlas.id) return;

    int tile = 0;
    Texture2D texture = renderer->atlas;
    switch (id) {
    case BLOCK_IRON:           tile = 0; break;
    case BLOCK_REDSTONE_BLOCK: tile = 1; break;
    case BLOCK_GLASS:
        texture = renderer->glass_texture;
        if (!texture.id) return;
        break;
    default: return;
    }

    /* Inset by half a texel so tile edges cannot sample their neighbor.
     * Image row zero maps to v=0; bottom vertices use the larger v. */
    float u0 = (tile*TILE_SIZE + 0.5f)/texture.width;
    float u1 = ((tile + 1)*TILE_SIZE - 0.5f)/texture.width;
    float v0 = 0.5f/texture.height;
    float v1 = (TILE_SIZE - 0.5f)/texture.height;
    const Vector2 uv[4] = {{u0, v1}, {u1, v1}, {u1, v0}, {u0, v0}};

    bool cutout = id == BLOCK_GLASS && renderer->cutout_shader.id;
    if (cutout) BeginShaderMode(renderer->cutout_shader);
    rlCheckRenderBatchLimit(24);
    rlSetTexture(texture.id);
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
    if (cutout) EndShaderMode();
}

static void wire_uv(Texture2D atlas, int tile, Vector2 uv[4])
{
    Rectangle rectangle = wire_tile_rectangle(tile);
    float u0 = (rectangle.x + 0.5f)/atlas.width;
    float u1 = (rectangle.x + TILE_SIZE - 0.5f)/atlas.width;
    float v0 = (rectangle.y + 0.5f)/atlas.height;
    float v1 = (rectangle.y + TILE_SIZE - 0.5f)/atlas.height;
    uv[0] = (Vector2){u0, v1};
    uv[1] = (Vector2){u1, v1};
    uv[2] = (Vector2){u1, v0};
    uv[3] = (Vector2){u0, v0};
}

static void emit_wire_face(const CubeFace *face, Vector3 center,
                           const Vector2 uv[4], bool climbing)
{
    rlNormal3f(face->normal.x, face->normal.y, face->normal.z);
    for (int vertex = 0; vertex < 4; ++vertex) {
        Vector3 position = face->vertices[vertex];
        /* Keep the lower wall edge just outside its support. The upper edge
         * meets the higher wire exactly; this avoids a gap at the step. */
        if (climbing && position.y < 0) {
            position.x += face->normal.x*wire_offset;
            position.z += face->normal.z*wire_offset;
        }
        rlTexCoord2f(uv[vertex].x, uv[vertex].y);
        rlVertex3f(center.x + position.x, center.y + position.y,
                   center.z + position.z);
    }
}

void block_renderer_begin_cutouts(const BlockRenderer *renderer)
{
    if (renderer && renderer->cutout_shader.id)
        BeginShaderMode(renderer->cutout_shader);
}

void block_renderer_end_cutouts(void)
{
    EndShaderMode();
}

void block_renderer_draw_wire(const BlockRenderer *renderer, BlockState state,
                              Vector3 center)
{
    if (!renderer || !renderer->wire_atlas.id || state.id != BLOCK_REDSTONE_WIRE)
        return;

    int mask = 0;
    for (int direction = 0; direction < 4; ++direction) {
        if (state.data.wire.connections & (1u << wire_directions[direction]))
            mask |= 1 << direction;
    }
    int powered = state.data.wire.power > 0;
    Vector2 uv[4];
    wire_uv(renderer->wire_atlas, powered*WIRE_SHAPES + mask, uv);

    rlCheckRenderBatchLimit(20);
    rlSetTexture(renderer->wire_atlas.id);
    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, 255);
    Vector3 top = {center.x, center.y - 1.0f + wire_offset, center.z};
    emit_wire_face(&cube_faces[4], top, uv, false);

    wire_uv(renderer->wire_atlas, WIRE_SIDE_FIRST + powered, uv);
    static const int side_faces[] = {3, 2, 1, 0};
    static const Vector3 offsets[] = {{1,0,0}, {-1,0,0}, {0,0,1}, {0,0,-1}};
    for (int direction = 0; direction < 4; ++direction) {
        unsigned bit = 1u << wire_directions[direction];
        if (!(state.data.wire.up_connections & state.data.wire.connections & bit))
            continue;
        Vector3 side = {center.x + offsets[direction].x,
                        center.y + wire_offset,
                        center.z + offsets[direction].z};
        emit_wire_face(&cube_faces[side_faces[direction]], side, uv, true);
    }
    rlEnd();
    rlSetTexture(0);
}

static TorchTransform torch_transform(Vector3 center, Direction support)
{
    TorchTransform transform = {
        {center.x, center.y - 0.5f, center.z},
        {1, 0, 0}, {0, 1, 0}, {0, 0, 1}
    };
    /* Facing is away from the support, whose direction points toward it. */
    Vector3 facing;
    switch (support) {
    case DIR_EAST:  facing = (Vector3){-1, 0,  0}; break;
    case DIR_WEST:  facing = (Vector3){ 1, 0,  0}; break;
    case DIR_SOUTH: facing = (Vector3){ 0, 0, -1}; break;
    case DIR_NORTH: facing = (Vector3){ 0, 0,  1}; break;
    default: return transform;
    }

    /* Vanilla's wall mount is 3.5/16 above the cell bottom, tilted 22.5
     * degrees outward. The stem's bottom intersects the supporting face. */
    const float sine = 0.3826834324f, cosine = 0.9238795325f;
    transform.origin.x -= facing.x*0.5f;
    transform.origin.y += 3.5f/16.0f;
    transform.origin.z -= facing.z*0.5f;
    transform.x_axis = (Vector3){facing.x*cosine, -sine, facing.z*cosine};
    transform.y_axis = (Vector3){facing.x*sine, cosine, facing.z*sine};
    transform.z_axis = (Vector3){-facing.z, 0, facing.x};
    return transform;
}

static Vector3 torch_rotate(TorchTransform transform, Vector3 vector)
{
    return (Vector3){
        transform.x_axis.x*vector.x + transform.y_axis.x*vector.y + transform.z_axis.x*vector.z,
        transform.x_axis.y*vector.x + transform.y_axis.y*vector.y + transform.z_axis.y*vector.z,
        transform.x_axis.z*vector.x + transform.y_axis.z*vector.y + transform.z_axis.z*vector.z
    };
}

static void torch_uv(Texture2D atlas, int lit, Rectangle source, Vector2 uv[4])
{
    /* Use texel boundaries so the four-pixel stem meets its caps exactly.
     * Point filtering preserves pixels; tile sides are transparent gutters. */
    float u0 = (lit*TILE_SIZE + source.x)/atlas.width;
    float u1 = (lit*TILE_SIZE + source.x + source.width)/atlas.width;
    float v0 = source.y/atlas.height;
    float v1 = (source.y + source.height)/atlas.height;
    uv[0] = (Vector2){u0, v1};
    uv[1] = (Vector2){u1, v1};
    uv[2] = (Vector2){u1, v0};
    uv[3] = (Vector2){u0, v0};
}

void block_renderer_draw_torch(const BlockRenderer *renderer, BlockState state,
                               Vector3 center)
{
    if (!renderer || !renderer->torch_atlas.id || state.id != BLOCK_REDSTONE_TORCH)
        return;

    TorchTransform transform = torch_transform(center, (Direction)state.data.torch.support_dir);
    int lit = state.data.torch.lit ? 1 : 0;
    Vector2 uv[4];
    torch_uv(renderer->torch_atlas, lit, (Rectangle){0, 0, TILE_SIZE, TILE_SIZE}, uv);

    rlCheckRenderBatchLimit(24);
    rlSetTexture(renderer->torch_atlas.id);
    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, 255);
    for (int face = 0; face < 6; ++face) {
        if (face == 4)
            torch_uv(renderer->torch_atlas, lit, (Rectangle){14, 7, 4, 4}, uv);
        else if (face == 5)
            torch_uv(renderer->torch_atlas, lit, (Rectangle){14, 28, 4, 4}, uv);

        const CubeFace *quad = &torch_faces[face];
        Vector3 normal = torch_rotate(transform, quad->normal);
        rlNormal3f(normal.x, normal.y, normal.z);
        for (int vertex = 0; vertex < 4; ++vertex) {
            Vector3 position = torch_rotate(transform, quad->vertices[vertex]);
            rlTexCoord2f(uv[vertex].x, uv[vertex].y);
            rlVertex3f(transform.origin.x + position.x,
                       transform.origin.y + position.y,
                       transform.origin.z + position.z);
        }
    }
    rlEnd();
    rlSetTexture(0);
}

int block_renderer_build_uv(const BlockRenderer *renderer, BlockState state,
                            unsigned solid_faces,
                            Vector2 uv[BLOCK_GEOMETRY_QUADS][4])
{
    int count = 0;
    if (state.id == BLOCK_IRON || state.id == BLOCK_REDSTONE_BLOCK ||
        state.id == BLOCK_GLASS) {
        bool glass = state.id == BLOCK_GLASS;
        Texture2D texture = glass ? renderer->glass_texture : renderer->atlas;
        int tile = state.id == BLOCK_REDSTONE_BLOCK ? 1 : 0;
        float u0 = (tile*TILE_SIZE + 0.5f)/texture.width;
        float u1 = ((tile + 1)*TILE_SIZE - 0.5f)/texture.width;
        float v0 = 0.5f/texture.height;
        float v1 = (TILE_SIZE - 0.5f)/texture.height;
        Vector2 face_uv[4] = {{u0,v1},{u1,v1},{u1,v0},{u0,v0}};
        for (int face = 0; face < 6; ++face)
            if (solid_faces & (1u << face))
                memcpy(uv[count++], face_uv, sizeof(face_uv));
    } else if (state.id == BLOCK_REDSTONE_WIRE) {
        int mask = 0;
        for (int direction = 0; direction < 4; ++direction)
            if (state.data.wire.connections & (1u << wire_directions[direction]))
                mask |= 1 << direction;
        int powered = state.data.wire.power > 0;
        wire_uv(renderer->wire_atlas, powered*WIRE_SHAPES + mask, uv[count++]);
        for (int direction = 0; direction < 4; ++direction) {
            unsigned bit = 1u << wire_directions[direction];
            if (state.data.wire.up_connections & state.data.wire.connections & bit)
                wire_uv(renderer->wire_atlas, WIRE_SIDE_FIRST + powered, uv[count++]);
        }
    } else if (state.id == BLOCK_REDSTONE_TORCH) {
        int lit = state.data.torch.lit ? 1 : 0;
        for (int face = 0; face < 6; ++face) {
            Rectangle source = {0,0,TILE_SIZE,TILE_SIZE};
            if (face == 4) source = (Rectangle){14,7,4,4};
            if (face == 5) source = (Rectangle){14,28,4,4};
            torch_uv(renderer->torch_atlas, lit, source, uv[count++]);
        }
    }
    return count;
}

void block_renderer_build_geometry(const BlockRenderer *renderer, BlockState state,
                                   Vector3 center, unsigned solid_faces,
                                   BlockGeometry *geometry)
{
    geometry->quads = block_renderer_build_uv(renderer, state, solid_faces, geometry->uv);
    int count = 0;
    if (state.id == BLOCK_IRON || state.id == BLOCK_REDSTONE_BLOCK ||
        state.id == BLOCK_GLASS) {
        for (int face = 0; face < 6; ++face) {
            if (!(solid_faces & (1u << face))) continue;
            for (int vertex = 0; vertex < 4; ++vertex) {
                Vector3 p = cube_faces[face].vertices[vertex];
                geometry->vertices[count][vertex] = (Vector3){center.x+p.x,center.y+p.y,center.z+p.z};
            }
            ++count;
        }
    } else if (state.id == BLOCK_REDSTONE_WIRE) {
        for (int vertex = 0; vertex < 4; ++vertex) {
            Vector3 p = cube_faces[4].vertices[vertex];
            geometry->vertices[0][vertex] = (Vector3){center.x+p.x,center.y-1+wire_offset+p.y,center.z+p.z};
        }
        count = 1;
        static const int side_faces[] = {3,2,1,0};
        static const Vector3 offsets[] = {{1,0,0},{-1,0,0},{0,0,1},{0,0,-1}};
        for (int direction = 0; direction < 4; ++direction) {
            unsigned bit = 1u << wire_directions[direction];
            if (!(state.data.wire.up_connections & state.data.wire.connections & bit)) continue;
            const CubeFace *face = &cube_faces[side_faces[direction]];
            for (int vertex = 0; vertex < 4; ++vertex) {
                Vector3 p = face->vertices[vertex];
                if (p.y < 0) {
                    p.x += face->normal.x*wire_offset;
                    p.z += face->normal.z*wire_offset;
                }
                geometry->vertices[count][vertex] = (Vector3){
                    center.x+offsets[direction].x+p.x,
                    center.y+wire_offset+p.y,center.z+offsets[direction].z+p.z};
            }
            ++count;
        }
    } else if (state.id == BLOCK_REDSTONE_TORCH) {
        TorchTransform transform = torch_transform(center, (Direction)state.data.torch.support_dir);
        for (int face = 0; face < 6; ++face)
            for (int vertex = 0; vertex < 4; ++vertex) {
                Vector3 p = torch_rotate(transform, torch_faces[face].vertices[vertex]);
                geometry->vertices[face][vertex] = (Vector3){transform.origin.x+p.x,transform.origin.y+p.y,transform.origin.z+p.z};
            }
    }
}
