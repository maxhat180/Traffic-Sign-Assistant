#include "frame.h"
#include "detect.h"
#ifdef TRAFFIC_SIGN_WITH_OPENCV
#include "image_adapter.h"
#endif
#ifdef TRAFFIC_SIGN_WITH_RECOGNIZER
#include "recognizer.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

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

#ifdef TRAFFIC_SIGN_WITH_RECOGNIZER
static bool nonnegative_number(const char *text, size_t *out)
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
    return true;
}
#endif

static int usage(const char *program)
{
    fprintf(stderr, "Usage: %s <image.ppm> [--grayscale] [--resize WIDTH HEIGHT] "
                    "[--output result.ppm] [--detect PREFIX]\n", program);
    fprintf(stderr, "Grayscale requires --output; resize requires --output or --detect.\n"
                    "Order: resize, detect on color, grayscale, save.\n");
    fprintf(stderr, "Detector: --red-min N --red-ratio N --min-area N --min-side N\n"
                    "          --max-aspect N --min-fill N --max-fill N --no-cleanup\n"
                    "Ratios: red in percent; aspect/fill in per mille.\n");
#ifdef TRAFFIC_SIGN_WITH_RECOGNIZER
    fprintf(stderr, "Recognition: --recognize MODEL [--confidence N]\n"
                    "Confidence is a minimum tree-vote share in per mille.\n");
#endif
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
    const char *detect_prefix = NULL;
    DetectorConfig config = detector_defaults();
    bool detector_options = false;
#ifdef TRAFFIC_SIGN_WITH_RECOGNIZER
    const char *recognition_model = NULL;
    unsigned int recognition_confidence = 600U;
    bool recognition_options = false;
#endif
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
        } else if (strcmp(argv[i], "--detect") == 0 && detect_prefix == NULL && argc - i > 1) {
            detect_prefix = argv[++i];
        } else if (strcmp(argv[i], "--no-cleanup") == 0) {
            config.cleanup = false;
            detector_options = true;
#ifdef TRAFFIC_SIGN_WITH_RECOGNIZER
        } else if (strcmp(argv[i], "--recognize") == 0 &&
                   recognition_model == NULL && argc - i > 1) {
            recognition_model = argv[++i];
        } else if (strcmp(argv[i], "--confidence") == 0 &&
                   !recognition_options && argc - i > 1) {
            size_t value;
            if (!nonnegative_number(argv[i + 1], &value) || value > 1000U) {
                return usage(argv[0]);
            }
            recognition_confidence = (unsigned int)value;
            recognition_options = true;
            ++i;
#endif
        } else if (argc - i > 1) {
            size_t value;
            if (!dimension(argv[i + 1], &value) || value > UINT_MAX) { return usage(argv[0]); }
            if (strcmp(argv[i], "--red-min") == 0) { config.red_min = (unsigned int)value; }
            else if (strcmp(argv[i], "--red-ratio") == 0) { config.red_ratio = (unsigned int)value; }
            else if (strcmp(argv[i], "--min-area") == 0) { config.min_area = value; }
            else if (strcmp(argv[i], "--min-side") == 0) { config.min_side = value; }
            else if (strcmp(argv[i], "--max-aspect") == 0) { config.max_aspect = (unsigned int)value; }
            else if (strcmp(argv[i], "--min-fill") == 0) { config.min_fill = (unsigned int)value; }
            else if (strcmp(argv[i], "--max-fill") == 0) { config.max_fill = (unsigned int)value; }
            else { return usage(argv[0]); }
            detector_options = true;
            ++i;
        } else {
            return usage(argv[0]);
        }
    }
    if ((grayscale && output == NULL) || (width != 0 && output == NULL && detect_prefix == NULL) ||
        (detector_options && detect_prefix == NULL)) {
        return usage(argv[0]);
    }
#ifdef TRAFFIC_SIGN_WITH_RECOGNIZER
    if ((recognition_model != NULL && detect_prefix == NULL) ||
        (recognition_options && recognition_model == NULL)) {
        return usage(argv[0]);
    }
#endif
    Frame frame = {0};
    const char *error = NULL;
#ifdef TRAFFIC_SIGN_WITH_OPENCV
    const bool loaded = frame_read_image(argv[1], &frame, &error);
    if (!loaded) {
        fprintf(stderr, "Could not load image: %s\n", error);
        return EXIT_FAILURE;
    }
#else
    FILE *file = fopen(argv[1], "rb");
    if (file == NULL) {
        perror("Could not open image");
        return EXIT_FAILURE;
    }
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
#endif
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
    if (detect_prefix != NULL) {
        Detection detection = {0};
        if (!detect_signs(&frame, &config, &detection, &error) ||
            !detection_save(&frame, &detection, detect_prefix, &error)) {
            fprintf(stderr, "Detection failed: %s\n", error);
            detection_destroy(&detection);
            frame_destroy(&frame);
            return EXIT_FAILURE;
        }
        printf("Components: %zu\nCandidates: %zu\n", detection.components, detection.count);
#ifdef TRAFFIC_SIGN_WITH_RECOGNIZER
        if (recognition_model != NULL) {
            Recognizer *recognizer = recognizer_load(recognition_model,
                                                     recognition_confidence, &error);
            size_t known = 0;
            if (recognizer == NULL ||
                !recognition_save(recognizer, &frame, &detection, detect_prefix,
                                  &known, &error)) {
                fprintf(stderr, "Recognition failed: %s\n", error);
                recognizer_destroy(recognizer);
                detection_destroy(&detection);
                frame_destroy(&frame);
                return EXIT_FAILURE;
            }
            printf("Recognized: %zu\nUnknown: %zu\n", known, detection.count - known);
            recognizer_destroy(recognizer);
        }
#endif
        detection_destroy(&detection);
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
#ifdef TRAFFIC_SIGN_WITH_OPENCV
    printf("Format: OpenCV-decoded RGB\nWidth: %zu\nHeight: %zu\nMaximum color value: %u\n",
#else
    printf("Format: P6\nWidth: %zu\nHeight: %zu\nMaximum color value: %u\n",
#endif
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
