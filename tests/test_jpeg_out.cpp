//! Integration test for C++17 variant of the toojpeg implementation
#include <iostream>
#include "toojpeg_17.h"
#include "vendor/sha2.h"
#include "tests.h"

#include <filesystem>
#include <array>

using std::cout;
using std::endl;

// 800x600 image
const auto width = 800;
const auto height = 600;

void testColor() {
    cout << "800*600 color gradient jpg: " << std::filesystem::current_path().append("color_gradient.jpg") << endl;
    // RGB: one byte each for red, green, blue
    const auto bytesPerPixel = 3;
    auto image = std::vector<unsigned char>(width * height * bytesPerPixel);

    // create a nice color transition
    for (auto y = 0; y < height; y++) {
        for (auto x = 0; x < width; x++) {
            // memory location of current pixel
            auto offset = (y * width + x) * bytesPerPixel;
            // red and green fade from 0 to 255, blue is always 127
            image[offset] = 255 * x / width;
            image[offset + 1] = 255 * y / height;
            image[offset + 2] = 127;
        }
    }
    std::ofstream outfile("color_gradient.jpg",
                          std::ios_base::trunc | std::ios_base::out |
                          std::ios_base::binary);
    bool ok = TooJpeg17::writeJpeg<90>(
            [&outfile](ByteView v) { outfile.write(v.data(), v.size_); }, image.data(), width, height, false, true,
            "TooJpeg17 example image");
    outfile.flush();
    ASSERT_EQUAL(ok, true);

    std::vector<unsigned char> hash(picosha2::k_digest_size);
    std::array<unsigned char, picosha2::k_digest_size> expected = {123, 245, 49, 202, 213, 219, 131, 175, 72, 129, 182,
                                                                   152, 15, 16, 158, 243, 136, 190, 229, 106, 233, 89,
                                                                   60, 61, 122, 146, 59, 135, 173, 108, 90, 89};
    std::ifstream in("color_gradient.jpg");
    picosha2::hash256(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>(), hash.begin(), hash.end());
    ASSERT_THROW(std::equal(hash.begin(), hash.end(), expected.begin()));
}

void testGrayscale() {
    cout << "800*600 grayscale gradient jpg: " << std::filesystem::current_path().append("grayscale_gradient.jpg")
         << endl;

    // Grayscale: one byte per pixel
    const auto bytesPerPixel = 1;

    auto image = std::vector<unsigned char>(width * height * bytesPerPixel);

    // create a grayscale transition
    for (auto y = 0; y < height; y++) {
        for (auto x = 0; x < width; x++) {
            // memory location of current pixel
            auto offset = (y * width + x) * bytesPerPixel;
            // red and green fade from 0 to 255, blue is always 127
            image[offset] = 255 * x / width;
            image[offset + 1] = 255 * y / height;
            image[offset + 2] = 127;
        }
    }
    std::ofstream outfile("grayscale_gradient.jpg",
                          std::ios_base::trunc | std::ios_base::out |
                          std::ios_base::binary);
    bool ok = TooJpeg17::writeJpeg<90>(
            [&outfile](ByteView v) { outfile.write(v.data(), v.size_); }, image.data(), width, height, false, false,
            "TooJpeg17 example image");
    outfile.flush();
    ASSERT_EQUAL(ok, true);

    std::vector<unsigned char> hash(picosha2::k_digest_size);
    std::array<unsigned char, picosha2::k_digest_size> expected = {215, 33, 80, 145, 167, 9, 23, 212, 246, 246, 72, 55,
                                                                   10, 102, 224, 237, 149, 162, 58, 10, 251, 204, 106,
                                                                   3, 178, 5, 62, 55, 134, 202, 85, 46};
    std::ifstream in("grayscale_gradient.jpg");
    picosha2::hash256(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>(), hash.begin(), hash.end());
    for (auto a: hash) std::cout << (int) a << ",";
    cout << endl;

    ASSERT_THROW(std::equal(hash.begin(), hash.end(), expected.begin()));
}

int main(int argc, char *argv[]) {
    std::ios_base::sync_with_stdio(false);

    ASSERT_EQUAL(argc, 2);
    switch (argv[1][0]) {
        case '0':
            testColor();
            break;
        case '1':
            testGrayscale();
            break;
    }
    return 0;
}