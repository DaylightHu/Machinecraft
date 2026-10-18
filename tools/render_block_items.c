#define STBI_ONLY_PNG
#define STBI_NO_HDR
#define STBI_NO_LINEAR

/* raylib vendors stb as third-party code. Its PNG-only configuration leaves
 * two internal helpers unused, so silence that warning only around stb. */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

enum { ICON_SIZE = 128, COLOR_CHANNELS = 4 };

typedef struct { float x, y; } Vec2;
typedef struct { unsigned char r, g, b, a; } Pixel;

typedef struct {
    int width;
    int height;
    const Pixel *pixels;
} SourceImage;

typedef struct {
    Vec2 origin;
    Vec2 u_axis;
    Vec2 v_axis;
    float brightness;
    bool flip_u;
    bool flip_v;
    int clockwise_turns;
} FaceProjection;

typedef struct {
    const char *top_path;
    const char *left_path;
    const char *right_path;
    const char *output_name;
    int top_turns;
    int left_turns;
    int right_turns;
} ItemSpec;

_Static_assert(sizeof(Pixel) == COLOR_CHANNELS,
               "Pixel must contain exactly four bytes");

static unsigned char shaded_channel(unsigned char channel, float brightness)
{
    float value = (float)channel*brightness;
    if (value > 255.0f) value = 255.0f;
    return (unsigned char)(value + 0.5f);
}

static Pixel shade(Pixel pixel, float brightness)
{
    pixel.r = shaded_channel(pixel.r, brightness);
    pixel.g = shaded_channel(pixel.g, brightness);
    pixel.b = shaded_channel(pixel.b, brightness);
    return pixel;
}

static void blend_over(Pixel *destination, Pixel source)
{
    unsigned source_alpha = source.a;
    unsigned inverse_alpha = 255u - source_alpha;
    unsigned destination_alpha = destination->a;
    unsigned output_alpha = source_alpha + destination_alpha*inverse_alpha/255u;

    if (output_alpha == 0) {
        *destination = (Pixel){0, 0, 0, 0};
        return;
    }

    destination->r = (unsigned char)(
        (source.r*source_alpha +
         destination->r*destination_alpha*inverse_alpha/255u)/output_alpha);
    destination->g = (unsigned char)(
        (source.g*source_alpha +
         destination->g*destination_alpha*inverse_alpha/255u)/output_alpha);
    destination->b = (unsigned char)(
        (source.b*source_alpha +
         destination->b*destination_alpha*inverse_alpha/255u)/output_alpha);
    destination->a = (unsigned char)output_alpha;
}

static void paint_face(SourceImage source, Pixel *destination,
                       FaceProjection face)
{
    float determinant = face.u_axis.x*face.v_axis.y -
                        face.u_axis.y*face.v_axis.x;
    if (determinant == 0.0f) return;

    for (int y = 0; y < ICON_SIZE; ++y) {
        for (int x = 0; x < ICON_SIZE; ++x) {
            /* Test the destination pixel's center against the projected face. */
            float relative_x = (float)x + 0.5f - face.origin.x;
            float relative_y = (float)y + 0.5f - face.origin.y;
            float u = (relative_x*face.v_axis.y -
                       relative_y*face.v_axis.x)/determinant;
            float v = (face.u_axis.x*relative_y -
                       face.u_axis.y*relative_x)/determinant;

            if (u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f)
                continue;

            if (face.flip_u) u = 1.0f - u;
            if (face.flip_v) v = 1.0f - v;
            for (int turn = 0; turn < face.clockwise_turns; ++turn) {
                float previous_u = u;
                u = 1.0f - v;
                v = previous_u;
            }
            int source_x = (int)(u*(float)source.width);
            int source_y = (int)(v*(float)source.height);
            if (source_x == source.width) --source_x;
            if (source_y == source.height) --source_y;
            Pixel sample = source.pixels[source_y*source.width + source_x];
            sample = shade(sample, face.brightness);
            blend_over(&destination[y*ICON_SIZE + x], sample);
        }
    }
}

static void paint_isometric_block(
    SourceImage top_source,
    SourceImage left_source,
    SourceImage right_source,
    Pixel *destination,
    int top_turns,
    int left_turns,
    int right_turns)
{
    const float side_height = 50.0f;

    /* Vertically center prisms of different heights in the icon. */
    float top_y = 63.0f - (52.0f + side_height)*0.5f;
    float edge_y = top_y + 26.0f;

    FaceProjection left = {
        {16.0f, edge_y},
        {48.0f, 26.0f},
        {0.0f, side_height},
        0.76f,
        false,
        false,
        left_turns
    };
    FaceProjection right = {
        {112.0f, edge_y},
        {-48.0f, 26.0f},
        {0.0f, side_height},
        0.88f,
        true,
        false,
        right_turns
    };
    FaceProjection top = {
        {64.0f, top_y},
        {48.0f, 26.0f},
        {-48.0f, 26.0f},
        1.00f,
        false,
        false,
        top_turns
    };

    paint_face(left_source, destination, left);
    paint_face(right_source, destination, right);
    paint_face(top_source, destination, top);
}

static bool ensure_output_directory(const char *path)
{
#ifdef _WIN32
    int result = _mkdir(path);
#else
    int result = mkdir(path, 0777);
#endif
    return result == 0 || errno == EEXIST;
}

static uint32_t png_crc32(const unsigned char *data, size_t size,
                          uint32_t crc)
{
    for (size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u &
                  (uint32_t)-(int32_t)(crc & 1u));
    }
    return crc;
}

static uint32_t png_adler32(const unsigned char *data, size_t size)
{
    uint32_t a = 1, b = 0;
    for (size_t index = 0; index < size; ++index) {
        a = (a + data[index]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

static bool write_bytes(FILE *file, const void *data, size_t size)
{
    return !size || fwrite(data, 1, size, file) == size;
}

static bool write_be32(FILE *file, uint32_t value)
{
    unsigned char bytes[4] = {
        (unsigned char)(value >> 24), (unsigned char)(value >> 16),
        (unsigned char)(value >> 8), (unsigned char)value
    };
    return write_bytes(file, bytes, sizeof(bytes));
}

static bool write_png_chunk(FILE *file, const char type[4],
                            const unsigned char *data, size_t size)
{
    if (size > UINT32_MAX || !write_be32(file, (uint32_t)size) ||
        !write_bytes(file, type, 4) || !write_bytes(file, data, size))
        return false;
    uint32_t crc = png_crc32((const unsigned char *)type, 4, UINT32_MAX);
    crc = ~png_crc32(data, size, crc);
    return write_be32(file, crc);
}

static bool write_png(const char *path, const Pixel *pixels)
{
    const size_t row_size = 1u + (size_t)ICON_SIZE*COLOR_CHANNELS;
    const size_t raw_size = row_size*ICON_SIZE;
    unsigned char *raw = malloc(raw_size);
    if (!raw) return false;
    for (int y = 0; y < ICON_SIZE; ++y) {
        unsigned char *row = raw + (size_t)y*row_size;
        row[0] = 0; /* PNG filter: none. */
        memcpy(row + 1, pixels + (size_t)y*ICON_SIZE,
               (size_t)ICON_SIZE*sizeof(*pixels));
    }

    size_t blocks = (raw_size + 65534u)/65535u;
    size_t zlib_size = 2u + raw_size + blocks*5u + 4u;
    unsigned char *zlib = malloc(zlib_size);
    if (!zlib) { free(raw); return false; }
    size_t out = 0, cursor = 0;
    zlib[out++] = 0x78;
    zlib[out++] = 0x01; /* Deflate with no compression. */
    while (cursor < raw_size) {
        size_t remaining = raw_size - cursor;
        uint16_t length = (uint16_t)(remaining > 65535u ? 65535u : remaining);
        bool final = cursor + length == raw_size;
        zlib[out++] = final ? 1u : 0u;
        zlib[out++] = (unsigned char)length;
        zlib[out++] = (unsigned char)(length >> 8);
        uint16_t inverse = (uint16_t)~length;
        zlib[out++] = (unsigned char)inverse;
        zlib[out++] = (unsigned char)(inverse >> 8);
        memcpy(zlib + out, raw + cursor, length);
        out += length;
        cursor += length;
    }
    uint32_t adler = png_adler32(raw, raw_size);
    zlib[out++] = (unsigned char)(adler >> 24);
    zlib[out++] = (unsigned char)(adler >> 16);
    zlib[out++] = (unsigned char)(adler >> 8);
    zlib[out++] = (unsigned char)adler;

    static const unsigned char signature[8] = {
        137, 80, 78, 71, 13, 10, 26, 10
    };
    unsigned char header[13] = {
        0, 0, 0, ICON_SIZE, 0, 0, 0, ICON_SIZE,
        8, 6, 0, 0, 0
    };
    FILE *file = fopen(path, "wb");
    bool ok = file && write_bytes(file, signature, sizeof(signature)) &&
              write_png_chunk(file, "IHDR", header, sizeof(header)) &&
              write_png_chunk(file, "IDAT", zlib, out) &&
              write_png_chunk(file, "IEND", NULL, 0);
    if (file && fclose(file) != 0) ok = false;
    free(zlib);
    free(raw);
    return ok;
}

static bool render_item(const ItemSpec *item, const char *output_directory)
{
    const char *const paths[] = {
        item->top_path, item->left_path, item->right_path
    };
    unsigned char *loaded[3] = {0};
    SourceImage sources[3] = {0};
    for (int index = 0; index < 3; ++index) {
        int channels = 0;
        loaded[index] = stbi_load(paths[index], &sources[index].width,
                                  &sources[index].height, &channels,
                                  COLOR_CHANNELS);
        (void)channels;
        if (!loaded[index]) {
            fprintf(stderr, "Cannot load %s: %s\n", paths[index],
                    stbi_failure_reason());
            for (int loaded_index = 0; loaded_index < 3; ++loaded_index)
                stbi_image_free(loaded[loaded_index]);
            return false;
        }
        sources[index].pixels = (const Pixel *)loaded[index];
    }

    Pixel *output = calloc((size_t)ICON_SIZE*ICON_SIZE, sizeof(*output));
    if (!output) {
        fprintf(stderr, "Cannot allocate image data for: %s\n", item->output_name);
        for (int index = 0; index < 3; ++index)
            stbi_image_free(loaded[index]);
        return false;
    }

    paint_isometric_block(sources[0], sources[1], sources[2], output,
                          item->top_turns, item->left_turns,
                          item->right_turns);

    char output_path[512];
    int written = snprintf(output_path, sizeof(output_path), "%s/%s",
                           output_directory, item->output_name);
    bool valid_path = written >= 0 && (size_t)written < sizeof(output_path);
    bool exported = valid_path && write_png(output_path, output);
    if (exported)
        printf("Wrote %s\n", output_path);
    else
        fprintf(stderr, "Cannot write output image: %s\n",
                valid_path ? output_path : item->output_name);

    free(output);
    for (int index = 0; index < 3; ++index)
        stbi_image_free(loaded[index]);
    return exported;
}

int main(int argc, char **argv)
{
    static const ItemSpec items[] = {
        {
            "res/textures/block/sticky_piston_top.png",
            "res/textures/block/sticky_piston_side0.png",
            "res/textures/block/sticky_piston_side0.png",
            "sticky_piston.png",
            0, 1, 1
        }
    };
    const char *output_directory = "res/textures/item";

    if (argc > 2) {
        fprintf(stderr, "Usage: %s [output-directory]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (argc == 2) output_directory = argv[1];

    if (!ensure_output_directory(output_directory)) {
        fprintf(stderr, "Cannot create output directory %s: error %d\n",
                output_directory, errno);
        return EXIT_FAILURE;
    }

    bool complete = true;
    for (size_t index = 0; index < sizeof(items)/sizeof(items[0]); ++index) {
        if (!render_item(&items[index], output_directory)) complete = false;
    }
    return complete ? EXIT_SUCCESS : EXIT_FAILURE;
}

