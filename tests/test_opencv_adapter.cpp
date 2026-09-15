#include "../image_adapter.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace {

unsigned int checks = 0;
const char *fixture_path = "opencv-adapter-test.png";

void check(bool condition, const char *message)
{
    ++checks;
    if (!condition) {
        std::remove(fixture_path);
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void write_png_fixture()
{
    /* Canonical 2x2 RGBA PNG for the RGB pixels in examples/tiny.ppm. */
    const unsigned char png[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00,
        0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
        0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xb6, 0x0d, 0x24,
        0x00, 0x00, 0x00, 0x01, 0x73, 0x52, 0x47, 0x42, 0x00, 0xae, 0xce,
        0x1c, 0xe9, 0x00, 0x00, 0x00, 0x04, 0x67, 0x41, 0x4d, 0x41, 0x00,
        0x00, 0xb1, 0x8f, 0x0b, 0xfc, 0x61, 0x05, 0x00, 0x00, 0x00, 0x09,
        0x70, 0x48, 0x59, 0x73, 0x00, 0x00, 0x0e, 0xc3, 0x00, 0x00, 0x0e,
        0xc3, 0x01, 0xc7, 0x6f, 0xa8, 0x64, 0x00, 0x00, 0x00, 0x1a, 0x49,
        0x44, 0x41, 0x54, 0x18, 0x57, 0x63, 0x08, 0x72, 0x77, 0xfa, 0x6f,
        0x68, 0x64, 0xfc, 0x9f, 0x21, 0x31, 0x29, 0xf9, 0x7f, 0x4a, 0x6a,
        0xda, 0x7f, 0x00, 0x3e, 0x4d, 0x07, 0xc3, 0x6b, 0x3c, 0xa7, 0x68,
        0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60,
        0x82};
    std::FILE *file = std::fopen(fixture_path, "wb");
    check(file != nullptr, "PNG fixture opens");
    const bool written = std::fwrite(png, 1U, sizeof png, file) == sizeof png;
    const bool closed = std::fclose(file) == 0;
    check(written && closed, "PNG fixture writes");
}

} // namespace

int main()
{
    write_png_fixture();
    Frame decoded = {};
    const char *error = nullptr;
    check(frame_read_image(fixture_path, &decoded, &error), "PNG decodes");
    check(error == nullptr, "decode clears error");
    check(decoded.width == 2 && decoded.height == 2 && decoded.max_value == 255,
          "decoded metadata");
    const unsigned char expected[] = {
        82, 71, 66, 49, 50, 51, 97, 98, 99, 100, 101, 102};
    for (size_t i = 0; i < sizeof expected; ++i) {
        check(decoded.pixels[i] == expected[i], "PNG channels are packed RGB");
    }
    frame_destroy(&decoded);
    check(std::remove(fixture_path) == 0, "PNG fixture is removed");

    unsigned char source_pixels[4U * 4U * 3U];
    for (size_t y = 0; y < 4U; ++y) {
        for (size_t x = 0; x < 4U; ++x) {
            const size_t value = y * 10U + x;
            const size_t offset = (y * 4U + x) * 3U;
            source_pixels[offset] = static_cast<unsigned char>(value);
            source_pixels[offset + 1U] = static_cast<unsigned char>(100U + value);
            source_pixels[offset + 2U] = static_cast<unsigned char>(200U + value);
        }
    }
    const Frame source = {4U, 4U, 255U, source_pixels};
    const FramePoint corners[4] = {{1.0, 1.0}, {2.0, 1.0},
                                   {2.0, 2.0}, {1.0, 2.0}};
    Frame normalized = {};
    check(frame_normalize_quad(&source, corners, 2U, 2U, &normalized, &error),
          "quadrilateral normalizes");
    const size_t source_indexes[] = {5U, 6U, 9U, 10U};
    for (size_t pixel = 0; pixel < 4U; ++pixel) {
        for (size_t channel = 0; channel < 3U; ++channel) {
            check(normalized.pixels[pixel * 3U + channel] ==
                      source_pixels[source_indexes[pixel] * 3U + channel],
                  "normalized corner pixel");
        }
    }
    frame_destroy(&normalized);

    FramePoint invalid[4] = {{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}};
    invalid[2].x = std::nan("");
    check(!frame_normalize_quad(&source, invalid, 2U, 2U, &normalized, &error),
          "non-finite corner is rejected");
    check(normalized.pixels == nullptr, "normalization failure is atomic");

    std::cout << "PASS: " << checks << " OpenCV adapter checks\n";
    return EXIT_SUCCESS;
}
