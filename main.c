#include "frame.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

static bool dimension(const char *text, size_t *out)
{
    size_t value = 0;
    if (*text == '\0') {
        return false;
    }
    for (; *text != '\0'; ++text) {
        if (*text < '0' || *text > '9') {
            return false;
        }
        const size_t digit = (size_t)(*text - '0');
        if (value > (SIZE_MAX - digit) / 10U) {
            return false;
        }
        value = value * 10U + digit;
    }
    *out = value;
    return value != 0;
}

static int usage(const char *program)
{
    fprintf(stderr, "Usage: %s <image.ppm> [--grayscale] [--resize WIDTH HEIGHT] "
                    "[--output result.ppm]\n", program);
    fprintf(stderr, "Transforms require --output. Resize runs before grayscale.\n");
    return EXIT_FAILURE;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        return usage(argv[0]);
    }
    bool grayscale = false;
    size_t width = 0;
    size_t height = 0;
    const char *output = NULL;
    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--grayscale") == 0 && !grayscale) {
            grayscale = true;
        } else if (strcmp(argv[i], "--resize") == 0 && width == 0 && argc - i > 2) {
            if (!dimension(argv[i + 1], &width) || !dimension(argv[i + 2], &height)) {
                return usage(argv[0]);
            }
            i += 2;
        } else if (strcmp(argv[i], "--output") == 0 && output == NULL && argc - i > 1) {
            output = argv[++i];
        } else {
            return usage(argv[0]);
        }
    }
    if ((grayscale || width != 0) && output == NULL) {
        return usage(argv[0]);
    }
    FILE *file = fopen(argv[1], "rb");
    if (file == NULL) {
        perror("Could not open image");
        return EXIT_FAILURE;
    }
    Frame frame = {0};
    const char *error = NULL;
    const bool loaded = frame_read_ppm(file, &frame, &error);
    const int close_result = fclose(file);
    if (!loaded) {
        fprintf(stderr, "Could not load image: %s\n", error);
    }
    if (close_result != 0) {
        perror("Could not close image");
    }
    if (!loaded || close_result != 0) {
        frame_destroy(&frame);
        return EXIT_FAILURE;
    }
    if (width != 0) {
        Frame resized = {0};
        if (!frame_resize(&frame, width, height, &resized, &error)) {
            fprintf(stderr, "Resize failed: %s\n", error);
            frame_destroy(&frame);
            return EXIT_FAILURE;
        }
        frame_destroy(&frame);
        frame = resized; /* Transfer the allocation's ownership. */
    }
    if (grayscale && !frame_grayscale(&frame, &error)) {
        fprintf(stderr, "Grayscale failed: %s\n", error);
        frame_destroy(&frame);
        return EXIT_FAILURE;
    }
    if (output != NULL) {
        FILE *destination = fopen(output, "wb");
        if (destination == NULL) {
            perror("Could not open output image");
            frame_destroy(&frame);
            return EXIT_FAILURE;
        }
        const bool written = frame_write_ppm(destination, &frame, &error);
        const int output_close = fclose(destination);
        if (!written) {
            fprintf(stderr, "Save failed: %s\n", error);
        }
        if (output_close != 0) {
            perror("Could not close output image");
        }
        if (!written || output_close != 0) {
            frame_destroy(&frame);
            return EXIT_FAILURE;
        }
    }
    printf("Format: P6\nWidth: %zu\nHeight: %zu\nMaximum color value: %u\n",
           frame.width, frame.height, frame.max_value);
    printf("RGB bytes: %zu\n", frame.width * frame.height * 3U);
    const size_t count = frame.width * frame.height;
    const size_t preview = count < 5U ? count : 5U;
    for (size_t i = 0; i < preview; ++i) {
        const unsigned char *pixel = frame.pixels + i * 3U;
        printf("Pixel (%zu, %zu): R=%u G=%u B=%u\n",
               i % frame.width, i / frame.width,
               (unsigned int)pixel[0], (unsigned int)pixel[1],
               (unsigned int)pixel[2]);
    }
    frame_destroy(&frame);
    if (fflush(stdout) == EOF || ferror(stdout)) {
        fprintf(stderr, "Could not write frame metadata\n");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
