#include "../frame.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#endif

static unsigned int checks = 0;
static const char *fixture_path = "frame-test.tmp";

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static FILE *fixture(const char *data, size_t length)
{
    /* Exclusive creation prevents overwriting an existing user file. */
#ifdef _WIN32
    /* Older MSVCRT versions do not implement fopen's C11 'x' mode. */
    const int descriptor = _open(fixture_path,
                                _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY,
                                _S_IREAD | _S_IWRITE);
    CHECK(descriptor != -1);
    FILE *file = _fdopen(descriptor, "w+b");
    if (file == NULL) {
        (void)_close(descriptor);
        (void)remove(fixture_path);
    }
#else
    FILE *file = fopen(fixture_path, "w+bx");
#endif
    CHECK(file != NULL);
    CHECK(fwrite(data, 1U, length, file) == length);
    CHECK(fseek(file, 0L, SEEK_SET) == 0);
    return file;
}

static int close_fixture(FILE *file)
{
    const int result = fclose(file);
    CHECK(remove(fixture_path) == 0);
    return result;
}

static void reject(const char *data, size_t length)
{
    FILE *file = fixture(data, length);
    Frame frame = {0};
    const char *error = NULL;
    CHECK(!frame_read_ppm(file, &frame, &error));
    CHECK(error != NULL);
    CHECK(frame.pixels == NULL && frame.width == 0 && frame.height == 0);
    CHECK(frame.max_value == 0);
    frame_destroy(&frame);
    CHECK(close_fixture(file) == 0);
}

#define REJECT(data) reject(data, sizeof(data) - 1U)

static void test_processing(void)
{
    unsigned char colors[] = {255,0,0, 0,255,0, 0,0,255, 255,255,255, 0,0,0, 77,77,77};
    Frame source = {3, 2, 255, colors}; /* Borrowed test storage; do not destroy. */
    const unsigned char original[] = {255,0,0, 0,255,0, 0,0,255, 255,255,255, 0,0,0, 77,77,77};
    const char *error = NULL;
    Frame result = {0};
    CHECK(frame_resize(&source, 3, 2, &result, &error));
    CHECK(result.pixels != source.pixels);
    CHECK(memcmp(result.pixels, original, sizeof original) == 0);
    frame_destroy(&result);
    /* Odd ratios: 3 -> 5 chooses [0,0,1,1,2]; 2 -> 3 chooses [0,0,1]. */
    CHECK(frame_resize(&source, 5, 3, &result, &error));
    const size_t columns[] = {0,0,1,1,2};
    const size_t rows[] = {0,0,1};
    for (size_t y = 0; y < 3; ++y) {
        for (size_t x = 0; x < 5; ++x) {
            CHECK(memcmp(result.pixels + (y * 5U + x) * 3U,
                         original + (rows[y] * 3U + columns[x]) * 3U, 3U) == 0);
        }
    }
    frame_destroy(&result);
    CHECK(frame_resize(&source, 2, 1, &result, &error));
    CHECK(memcmp(result.pixels, original, 6U) == 0);
    frame_destroy(&result);
    CHECK(frame_resize(&source, 1, 1, &result, &error));
    CHECK(memcmp(result.pixels, original, 3U) == 0);
    frame_destroy(&result);
    CHECK(memcmp(source.pixels, original, sizeof original) == 0);
    CHECK(!frame_resize(&source, 0, 2, &result, &error));
    CHECK(!frame_resize(&source, SIZE_MAX, 2, &result, &error));
    CHECK(!frame_resize(&source, SIZE_MAX / 3U + 1U, 1, &result, &error));
    CHECK(result.pixels == NULL && result.width == 0);
    CHECK(!frame_resize(&source, 1, 1, &source, &error));
    CHECK(frame_grayscale(&source, &error));
    const unsigned char gray[] = {76,150,29,255,0,77};
    for (size_t i = 0; i < sizeof gray; ++i) {
        CHECK(colors[i * 3U] == gray[i]);
        CHECK(colors[i * 3U + 1U] == gray[i]);
        CHECK(colors[i * 3U + 2U] == gray[i]);
    }
    CHECK(frame_grayscale(&source, &error)); /* Idempotent. */
    CHECK(colors[0] == 76 && colors[3] == 150);

    FILE *file = fixture("", 0);
    CHECK(frame_write_ppm(file, &source, &error));
    CHECK(fseek(file, 0L, SEEK_SET) == 0);
    CHECK(frame_read_ppm(file, &result, &error));
    CHECK(result.width == 3 && result.height == 2 && result.max_value == 255);
    CHECK(memcmp(result.pixels, source.pixels, sizeof colors) == 0);
    frame_destroy(&result);
    CHECK(close_fixture(file) == 0);

    unsigned char low[] = {15,0,0};
    Frame low_frame = {1,1,15,low};
    CHECK(frame_grayscale(&low_frame, &error));
    CHECK(low[0] == 4 && low[1] == 4 && low[2] == 4);
    CHECK(frame_resize(&low_frame, 2, 2, &result, &error));
    CHECK(result.max_value == 15 && result.pixels[11] == 4);
    frame_destroy(&result);
    Frame empty = {0};
    CHECK(!frame_grayscale(&empty, &error));
    CHECK(!frame_resize(&empty, 1, 1, &result, &error));
    low[2] = 16;
    CHECK(!frame_grayscale(&low_frame, &error));
    CHECK(low[0] == 4 && low[2] == 16); /* No partial mutation on invalid input. */
    file = fixture("", 0);
    CHECK(!frame_write_ppm(file, &low_frame, &error));
    CHECK(ftell(file) == 0);
    CHECK(close_fixture(file) == 0);
}

int main(void)
{
    /* Binary zeros and high-bit bytes must survive unchanged, in row order. */
    const char image[] = "P6\n# dimensions\n2 2\n255\n"
                         "\xff\0\0\0\xff\0\0\0\xff\x0a\x20\x23";
    const unsigned char expected[] = {
        255, 0, 0, 0, 255, 0, 0, 0, 255, 10, 32, 35
    };
    FILE *file = fixture(image, sizeof image - 1U);
    Frame frame = {0};
    const char *error = NULL;
    CHECK(frame_read_ppm(file, &frame, &error));
    CHECK(error == NULL);
    CHECK(frame.width == 2 && frame.height == 2 && frame.max_value == 255);
    CHECK(memcmp(frame.pixels, expected, sizeof expected) == 0);
    CHECK(close_fixture(file) == 0);
    frame_destroy(&frame);
    CHECK(frame.pixels == NULL && frame.width == 0 && frame.height == 0);
    CHECK(frame.max_value == 0);
    frame_destroy(&frame);

    /* Every whitespace byte and '#' is legal at the start of the raster. */
    const unsigned char starts[] = {0, 9, 10, 11, 12, 13, 32, 35, 255};
    for (size_t i = 0; i < sizeof starts; ++i) {
        const char header[] = "P6\n1 1\n255\n";
        file = fixture(header, sizeof header - 1U);
        CHECK(fseek(file, 0L, SEEK_END) == 0);
        CHECK(fputc((int)starts[i], file) != EOF);
        CHECK(fputc(0, file) != EOF && fputc(255, file) != EOF);
        CHECK(fseek(file, 0L, SEEK_SET) == 0);
        CHECK(frame_read_ppm(file, &frame, &error));
        CHECK(frame.pixels[0] == starts[i]);
        CHECK(frame.pixels[1] == 0 && frame.pixels[2] == 255);
        frame_destroy(&frame);
        CHECK(close_fixture(file) == 0);
    }

    const char comments[] = "P6\r\n# a\r\n1# b\n 1\t# c\n15\n\0\x0f\x07";
    file = fixture(comments, sizeof comments - 1U);
    CHECK(frame_read_ppm(file, &frame, &error));
    CHECK(frame.max_value == 15 && frame.pixels[1] == 15);
    frame_destroy(&frame);
    CHECK(close_fixture(file) == 0);

    /* The loader consumes exactly one frame, leaving the next intact. */
    const char sequence[] = "P6\n1 1\n1\n\0\1\0P6\n1 1\n1\n\1\0\1";
    file = fixture(sequence, sizeof sequence - 1U);
    CHECK(frame_read_ppm(file, &frame, &error));
    CHECK(frame.pixels[0] == 0 && frame.pixels[1] == 1);
    frame_destroy(&frame);
    CHECK(frame_read_ppm(file, &frame, &error));
    CHECK(frame.pixels[0] == 1 && frame.pixels[1] == 0);
    frame_destroy(&frame);
    CHECK(fgetc(file) == EOF);
    CHECK(close_fixture(file) == 0);

    REJECT("");
    REJECT("P");
    REJECT("P3\n1 1\n255\nabc");
    REJECT("P6x1 1\n255\nabc");
    REJECT("P6\n# unterminated comment");
    REJECT("P6\n-1 1\n255\nabc");
    REJECT("P6\n+1 1\n255\nabc");
    REJECT("P6\n0 1\n255\nabc");
    REJECT("P6\n1 0\n255\nabc");
    REJECT("P6\n1x 1\n255\nabc");
    REJECT("P6\n1");
    REJECT("P6\n1 1\n0\nabc");
    REJECT("P6\n1 1\n256\nabc");
    REJECT("P6\n1 1\n65536\nabc");
    REJECT("P6\n1 1\n255");
    REJECT("P6\n1 1\n255# no separator\nabc");
    REJECT("P6\n1 1\n255\nab");
    REJECT("P6\n1 1\n1\n\0\2\0");
    REJECT("P6\n9999999999999999999999999999999999999999 1\n255\n");

    char overflow[128];
    int length = snprintf(overflow, sizeof overflow, "P6\n%zu 2\n255\n", SIZE_MAX);
    CHECK(length > 0 && (size_t)length < sizeof overflow);
    reject(overflow, (size_t)length);
    length = snprintf(overflow, sizeof overflow, "P6\n%zu 1\n255\n", SIZE_MAX / 3U + 1U);
    CHECK(length > 0 && (size_t)length < sizeof overflow);
    reject(overflow, (size_t)length);

    test_processing();
    printf("PASS: %u checks\n", checks);
    return EXIT_SUCCESS;
}
