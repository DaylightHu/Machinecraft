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

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

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
} FaceProjection;

typedef void (*ItemPainter)(SourceImage source, Pixel *destination);

typedef struct {
    const char *source_path;
    const char *output_name;
    ItemPainter paint;
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

            int source_x = (int)(u*(float)source.width);
            int source_y = (int)(v*(float)source.height);
            Pixel sample = source.pixels[source_y*source.width + source_x];
            sample = shade(sample, face.brightness);
            blend_over(&destination[y*ICON_SIZE + x], sample);
        }
    }
}

static void paint_isometric_cube(SourceImage source, Pixel *destination)
{
    /* The three visible faces share exact edges. Different brightness values
     * make the 3D form readable without changing the original texture files. */
    const FaceProjection left = {
        {16.0f, 38.0f}, {48.0f, 26.0f}, {0.0f, 50.0f}, 0.76f
    };
    const FaceProjection right = {
        {112.0f, 38.0f}, {-48.0f, 26.0f}, {0.0f, 50.0f}, 0.88f
    };
    const FaceProjection top = {
        {64.0f, 12.0f}, {48.0f, 26.0f}, {-48.0f, 26.0f}, 1.00f
    };

    paint_face(source, destination, left);
    paint_face(source, destination, right);
    paint_face(source, destination, top);
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

static bool render_item(const ItemSpec *item, const char *output_directory)
{
    int width = 0;
    int height = 0;
    int source_channels = 0;
    unsigned char *loaded = stbi_load(item->source_path, &width, &height,
                                      &source_channels, COLOR_CHANNELS);
    (void)source_channels;
    if (!loaded) {
        fprintf(stderr, "Cannot load %s: %s\n", item->source_path,
                stbi_failure_reason());
        return false;
    }

    Pixel *output = calloc((size_t)ICON_SIZE*ICON_SIZE, sizeof(*output));
    if (!output) {
        fprintf(stderr, "Cannot allocate image data for: %s\n", item->output_name);
        stbi_image_free(loaded);
        return false;
    }

    SourceImage source = {width, height, (const Pixel *)loaded};
    item->paint(source, output);

    char output_path[512];
    int written = snprintf(output_path, sizeof(output_path), "%s/%s",
                           output_directory, item->output_name);
    bool valid_path = written >= 0 && (size_t)written < sizeof(output_path);
    bool exported = valid_path &&
        stbi_write_png(output_path, ICON_SIZE, ICON_SIZE, COLOR_CHANNELS,
                       output, ICON_SIZE*(int)sizeof(*output));
    if (exported)
        printf("Wrote %s\n", output_path);
    else
        fprintf(stderr, "Cannot write output image: %s\n",
                valid_path ? output_path : item->output_name);

    free(output);
    stbi_image_free(loaded);
    return exported;
}

int main(int argc, char **argv)
{
    static const ItemSpec items[] = {
        {
            "res/textures/block/iron_block.png",
            "iron_block.png",
            paint_isometric_cube
        },
        {
            "res/textures/block/redstone_block.png",
            "redstone_block.png",
            paint_isometric_cube
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

