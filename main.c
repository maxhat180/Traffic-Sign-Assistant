#include "frame.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <image.ppm>\n", argv[0]);
        return EXIT_FAILURE;
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
