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

    printf("PASS: %u checks\n", checks);
    return EXIT_SUCCESS;
}
