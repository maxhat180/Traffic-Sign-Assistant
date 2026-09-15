#include "../detect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static unsigned int checks;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "Detector FAIL line %d: %s\n", __LINE__, #c); exit(EXIT_FAILURE); } } while (0)

static void red(Frame *f, size_t x, size_t y)
{
    unsigned char *p = f->pixels + (y * f->width + x) * 3U;
    p[0] = (unsigned char)f->max_value; p[1] = 0; p[2] = 0;
}

static void ring(Frame *f, size_t x, size_t y, size_t side)
{
    for (size_t i = 0; i < side; ++i) {
        red(f, x + i, y); red(f, x + i, y + side - 1U);
        red(f, x, y + i); red(f, x + side - 1U, y + i);
    }
}

int main(void)
{
    unsigned char pixels[40U * 30U * 3U];
    memset(pixels, 255, sizeof pixels);
    Frame f = {40,30,255,pixels};
    DetectorConfig c = detector_defaults();
    CHECK(c.min_area == 120 && c.min_side == 12 && c.min_fill == 150);
    /* Compact fixtures exercise the pipeline at deliberately smaller limits. */
    c.min_area = 12; c.min_side = 5; c.min_fill = 60;
    Detection d = {0};
    const char *error = NULL;
    CHECK(detect_signs(&f, &c, &d, &error));
    CHECK(d.count == 0 && d.components == 0 && error == NULL);
    detection_destroy(&d);
    ring(&f, 3, 4, 12);
    red(&f, 35, 25); /* Isolated noise should disappear. */
    CHECK(detect_signs(&f, &c, &d, &error));
    CHECK(d.count == 1 && d.components == 1);
    CHECK(d.boxes[0].x == 3 && d.boxes[0].y == 4 && d.boxes[0].width == 12 && d.boxes[0].height == 12);
    CHECK(d.raw_mask.pixels[(25U * 40U + 35U) * 3U] == 255);
    CHECK(d.clean_mask.pixels[(25U * 40U + 35U) * 3U] == 0);
    CHECK(f.pixels[(4U * 40U + 3U) * 3U + 1U] == 0); /* Source unchanged. */
    detection_destroy(&d);
    /* A one-pixel border break is restored by closing. */
    memset(pixels + (4U * 40U + 8U) * 3U, 255, 3U);
    CHECK(detect_signs(&f, &c, &d, &error));
    CHECK(d.count == 1);
    CHECK(d.raw_mask.pixels[(4U * 40U + 8U) * 3U] == 0);
    CHECK(d.clean_mask.pixels[(4U * 40U + 8U) * 3U] == 255);
    detection_destroy(&d);
    /* Multiple disconnected rings, including one touching the image border. */
    memset(pixels, 255, sizeof pixels);
    ring(&f, 0, 0, 12); ring(&f, 24, 16, 12);
    CHECK(detect_signs(&f, &c, &d, &error));
    CHECK(d.count == 2);
    CHECK(d.boxes[0].x == 0 && d.boxes[0].y == 0);
    detection_destroy(&d);
    /* Solid red objects and elongated strips fail fill/aspect respectively. */
    for (size_t y = 0; y < f.height; ++y) {
        for (size_t x = 0; x < f.width; ++x) { red(&f,x,y); }
    }
    CHECK(detect_signs(&f, &c, &d, &error));
    CHECK(d.count == 0 && d.components == 1);
    detection_destroy(&d);
    memset(pixels, 255, sizeof pixels);
    for (size_t x = 2; x < 38; ++x) { red(&f,x,10); red(&f,x,16); }
    for (size_t y = 10; y <= 16; ++y) { red(&f,2,y); red(&f,37,y); }
    CHECK(detect_signs(&f, &c, &d, &error));
    CHECK(d.count == 0);
    detection_destroy(&d);
    /* Diagonal connectivity is intentional, with cleanup disabled. */
    memset(pixels, 255, sizeof pixels);
    red(&f,5,5); red(&f,6,6);
    c.cleanup = false; c.min_area = 1; c.min_side = 1; c.max_fill = 1000;
    CHECK(detect_signs(&f, &c, &d, &error));
    CHECK(d.count == 1 && d.boxes[0].area == 2);
    CHECK(memcmp(d.raw_mask.pixels, d.clean_mask.pixels, sizeof pixels) == 0);
    detection_destroy(&d);
    /* More than eight components exercises candidate-array growth. */
    memset(pixels, 255, sizeof pixels);
    for (size_t x = 1; x < 40; x += 4U) { red(&f,x,25); }
    CHECK(detect_signs(&f, &c, &d, &error));
    CHECK(d.count == 10 && d.boxes[9].x == 37);
    detection_destroy(&d);
    /* Normalized threshold respects low Maxval; gray/dark pixels are excluded. */
    unsigned char low[] = {15,0,0, 15,15,15, 1,0,0};
    Frame low_frame = {3,1,15,low};
    CHECK(detect_signs(&low_frame, &c, &d, &error));
    CHECK(d.raw_mask.pixels[0] == 255 && d.raw_mask.pixels[3] == 0 && d.raw_mask.pixels[6] == 0);
    detection_destroy(&d);
    c.red_ratio = 100;
    CHECK(!detect_signs(&f, &c, &d, &error));
    CHECK(d.boxes == NULL && d.raw_mask.pixels == NULL);
    c = detector_defaults();
    Frame invalid = {SIZE_MAX,2,255,pixels};
    CHECK(!detect_signs(&invalid, &c, &d, &error));
    low[0] = 16;
    CHECK(!detect_signs(&low_frame, &c, &d, &error));
    detection_destroy(&d); detection_destroy(&d);
    CHECK(d.count == 0 && d.clean_mask.pixels == NULL);
    printf("PASS: %u detector checks\n", checks);
    return EXIT_SUCCESS;
}
