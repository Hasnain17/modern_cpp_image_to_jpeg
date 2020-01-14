//! Originally based on TooJpeg, written by Stephan Brumme, 2018-2019 see https://create.stephan-brumme.com/toojpeg/.
//! This is a C++17 modified variant using std::char, constexpr to precompute certain constants and lookup tables.
#pragma once

#include <functional>
#include <utility>
#include <cmath>
#include <type_traits>
#include <cstddef>
#include <memory>

/// A template parameterizable variant of TooJpeg via C++14/17 features. Used features are documented throughout the
/// this source code file with comment blocks or lines starting with CPP:.
#include "jpeg_constants.h"
#include "byte_view.h"

namespace TooJpeg17 {
/// CPP14: user-defined literal for the std::byte type. Usage: 0xff_bn
constexpr uint8_t operator "" _bn(unsigned long long v) { return v; }

using WRITE_BACK = std::function<void(ByteView)>;

// represent a single Huffman code
struct BitCode {
    /// undefined state, must be initialized at a later time
    BitCode() = default;

    /// Create a new Huffman code
    constexpr BitCode(uint16_t code_, uint8_t numBits_)
            : code(code_), numBits(numBits_) {}

    /// JPEG's Huffman codes are limited to 16 bits
    uint16_t code;
    /// number of valid bits
    uint8_t numBits;
};

/**
 * wrapper for bit output operations
 * @tparam clear_upper_bits buffer.bits may contain garbage in the high bits
 *          if you really want to "clean up" (e.g. for debugging purposes) then set this to true
 */
template<bool clear_upper_bits = false>
struct BitWriter {
    WRITE_BACK output;

    // initialize writer
    explicit BitWriter(WRITE_BACK &&output_) : output(std::move(output_)) {}

    // store the most recently encoded bits that are not written yet
    struct BitBuffer {
        uint32_t data = 0; // actually only at most 24 bits are used
        uint8_t numBits = 0; // number of valid bits (the right-most bits)
    } buffer;

    // write Huffman bits stored in BitCode, keep excess bits in BitBuffer
    BitWriter &operator<<(const BitCode &data) {
        // append the new bits to those bits leftover from previous call(s)
        buffer.numBits += data.numBits;
        buffer.data <<= data.numBits;
        buffer.data |= data.code;

        // write all "full" bytes
        while (buffer.numBits >= 8) {
            // extract highest 8 bits
            buffer.numBits -= 8;
            auto oneByte = uint8_t(buffer.data >> buffer.numBits);
            *this << oneByte;
            // 0xFF has a special meaning for JPEGs (it's a block marker)
            if (oneByte == 0xFF)
                // therefore pad a zero to indicate "nope, this one ain't a marker, it's just a coincidence"
                *this << 0_bn;

            if constexpr (clear_upper_bits) {
                buffer.data &= (1u << buffer.numBits) - 1;
            }
        }
        return *this;
    }

    // write all non-yet-written bits, fill gaps with 1s (that's a strange JPEG thing)
    void flush() {
        // at most seven set bits needed to "fill" the last byte: 0x7F = binary 0111 1111
        // I should set buffer.numBits = 0 but since there are no single bits written after flush() I can safely ignore it
        *this << BitCode(0x7F, 7);
    }

    // NOTE: all the following BitWriter functions IGNORE the BitBuffer and write straight to output !
    // write a single byte
    inline BitWriter &operator<<(std::uint8_t oneByte) {
        output(oneByte);
        return *this;
    }

    inline BitWriter &operator<<(const std::array<uint8_t, 64> &data) {
        output(ByteView{data.data(), data.size()});
        return *this;
    }

    inline BitWriter &operator<<(std::string_view data) {
        output(ByteView{data.data(), data.size()});
        return *this;
    }

    inline BitWriter &operator<<(ByteView data) {
        output(data);
        return *this;
    }

    // start a new JFIF block
    void addMarker(std::uint8_t id, uint16_t length) {
        // ID, always preceded by 0xFF
        // length of the block (big-endian, includes the 2 length bytes as well)
        output(0xFF);
        output(id);
        output(uint8_t(length >> 8u));
        output(uint8_t(length & 0xFFu));
    }
};

/// precompute JPEG codewords for quantized DCT
// note: quantized[i] is found at codewordsArray[quantized[i] + CodeWordLimit]
constexpr auto codewords_for_quantized_dct() -> std::array<BitCode, 2 * CodeWordLimit> {
    std::array<BitCode, 2 * CodeWordLimit> codewordsArray{};
    BitCode *codewords = &codewordsArray[CodeWordLimit]; // allow negative indices, so quantized[i] is at codewords[quantized[i]]
    uint8_t numBits = 1; // each codeword has at least one bit (value == 0 is undefined)
    uint32_t mask = 1; // mask is always 2^numBits - 1, initial value 2^1-1 = 2-1 = 1
    for (uint16_t value = 1; value < CodeWordLimit; value++) {
        // numBits = position of highest set bit (ignoring the sign)
        // mask    = (2^numBits) - 1
        if (value > mask) // one more bit ?
        {
            numBits++;
            mask = (mask << 1u) | 1u; // append a set bit
        }
        codewords[-value] = BitCode(mask - value,
                                    numBits); // note that I use a negative index => codewords[-value] = codewordsArray[CodeWordLimit  value]
        codewords[+value] = BitCode(value, numBits);
    }
    return codewordsArray;
}

// CPP: Compile time quantisation table computation
constexpr auto
quant_table(const std::array<uint8_t, 8 * 8> defaults, unsigned char quality) -> std::array<uint8_t, 8 * 8> {
    std::array<uint8_t, 8 * 8> quantLuminance{};
    for (auto i = 0; i < 8 * 8; i++) {
        int luminance = (defaults[ZigZagInv[i]] * quality + 50) / 100;
        quantLuminance[i] = std::clamp(luminance, 1, 255);
    }
    return quantLuminance;
}

/**
 * Adjust quantization tables with AAN scaling factors to simplify DCT
 * @param quantLuminance precomputed quantized Luminance
 * @param quantChrominance precomputed quantized Chrominance
 * @return A tuple of scaled (Luminance, Chrominance)
 */
constexpr auto scaled_luminance(std::array<uint8_t, 8 * 8> quantLuminance)
-> std::array<float, 8 * 8> {
    std::array<float, 8 * 8> scaledLuminance{};
    for (auto i = 0; i < 8 * 8; i++) {
        auto row = ZigZagInv[i] / 8;
        auto column = ZigZagInv[i] % 8;
        auto factor = 1 / (AanScaleFactors[row] * AanScaleFactors[column] * 8);
        scaledLuminance[ZigZagInv[i]] = factor / (float) (quantLuminance[i]);
    }
    return scaledLuminance;
}

constexpr auto scaled_chrominance(std::array<uint8_t, 8 * 8> quantChrominance)
-> std::array<float, 8 * 8> {
    std::array<float, 8 * 8> scaledChrominance{};
    for (auto i = 0; i < 8 * 8; i++) {
        auto row = ZigZagInv[i] / 8;
        auto column = ZigZagInv[i] % 8;
        auto factor = 1 / (AanScaleFactors[row] * AanScaleFactors[column] * 8);
        scaledChrominance[ZigZagInv[i]] = factor / (float) (quantChrominance[i]);
    }
    return scaledChrominance;
}

bool writeJpegIntern(BitWriter<> bitWriter, const uint8_t *pixels, unsigned short width, unsigned short height,
                     bool downsample, bool isRGB, const std::array<uint8_t, 8 * 8> &quantLuminance,
                     const std::array<uint8_t, 8 * 8> &quantChrominance,
                     const float *scaled_luminance, const float *scaled_chrominance,
                     std::string_view comment = "");

/**
 * Takes input pixels and writes a jpeg output
 * @tparam quality_ between 1 (worst) and 100 (best)
 * @param downsample if true then YCbCr 4:2:0 format is used (smaller size, minor quality loss) instead of 4:4:4, not relevant for grayscale
 * @param isRGB true if RGB format (3 bytes per pixel); false if grayscale (1 byte per pixel)
 * @param output callback that stores a single byte (writes to disk, memory, ...)
 * @param pixels_ stored in RGB format or grayscale, stored from upper-left to lower-right. Must be as long as width*height.
 * @param width Width pixels
 * @param height Height pixels
 * @param comment optional JPEG comment (0/NULL if no comment), must not contain ASCII code 0xFF
 * @return Returns the output stream back on success and false otherwise
 */
template<unsigned char quality_>
bool writeJpeg(WRITE_BACK output, const uint8_t *pixels, unsigned short width, unsigned short height,
               bool downsample, bool isRGB,
               const std::string_view comment = "") {

    // grayscale images can't be downsampled (because there are no Cb + Cr channels)
    if (!isRGB) downsample = false;

    static_assert(quality_ > 1 && quality_ <= 100, "Quality must be in [1..100]");

    // convert to an internal JPEG quality factor, formula taken from libjpeg
    constexpr auto quality = quality_ < 50 ? 5000 / quality_ : 200 - quality_ * 2;

    // reject invalid pointers
    if (pixels == nullptr)
        return false;
    // check image format
    if (width == 0 || height == 0)
        return false;

    // CPP(14): Compile time compute quantisation tables for the given quality level
    constexpr std::array<uint8_t, 8 * 8> quantLuminance = quant_table(DefaultQuantLuminance_A, quality);
    constexpr std::array<uint8_t, 8 * 8> quantChrominance = quant_table(DefaultQuantChrominance_A, quality);
    constexpr auto scaledLuminance = scaled_luminance(quantLuminance);
    constexpr auto scaledChrominance = scaled_chrominance(quantChrominance);

    return writeJpegIntern(BitWriter(std::move(output)), pixels, width, height, downsample, isRGB, quantLuminance, quantChrominance,
                           scaledLuminance.data(), scaledChrominance.data(), comment);
}

bool writeJpegQuality(WRITE_BACK output, const uint8_t *pixels, unsigned short width, unsigned short height,
                      bool downsample, bool isRGB, unsigned char quality_,
                      std::string_view comment = "");
} // namespace TooJpeg
