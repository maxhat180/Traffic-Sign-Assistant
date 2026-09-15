#include "../image_adapter.h"

#include <cstdlib>
#include <iostream>

namespace {

unsigned int checks = 0;

void check(bool condition, const char *message)
{
    ++checks;
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main()
{
    /* Two rows, two pixels each, with two padding bytes per source row. */
    const unsigned char bgr[] = {
        30, 20, 10, 60, 50, 40, 0, 0,
        90, 80, 70, 120, 110, 100, 0, 0};
    Frame frame = {};
    const char *error = nullptr;
    check(frame_from_bgr(bgr, 2, 2, 8, &frame, &error), "BGR copy succeeds");
    check(error == nullptr, "success clears error");
    check(frame.width == 2 && frame.height == 2 && frame.max_value == 255,
          "frame metadata");
    const unsigned char expected[] = {
        10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120};
    for (size_t i = 0; i < sizeof expected; ++i) {
        check(frame.pixels[i] == expected[i], "BGR is converted to packed RGB");
    }
    frame_destroy(&frame);
    check(frame.pixels == nullptr && frame.width == 0, "C cleanup accepts C++ allocation");
    check(!frame_from_bgr(bgr, 2, 2, 5, &frame, &error), "short stride is rejected");
    check(frame.pixels == nullptr, "failure leaves destination empty");
    std::cout << "PASS: " << checks << " C/C++ bridge checks\n";
    return EXIT_SUCCESS;
}
