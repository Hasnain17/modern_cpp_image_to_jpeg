//! Originally based on TooJpeg, written by Stephan Brumme, 2018-2019 see https://create.stephan-brumme.com/toojpeg/.
//! This is a C++17 modified variant using std::char, constexpr to precompute certain constants and lookup tables.
#include "toojpeg_17.h"

namespace TooJpeg17 {

// convert from RGB to YCbCr, constants are similar to ITU-R, see https://en.wikipedia.org/wiki/YCbCr#JPEG_conversion
inline float rgb2y(float r, float g, float b) { return +0.299f * r + 0.587f * g + 0.114f * b; }

inline float rgb2cb(float r, float g, float b) { return -0.16874f * r - 0.33126f * g + 0.5f * b; }

inline float rgb2cr(float r, float g, float b) { return +0.5f * r - 0.41869f * g - 0.08131f * b; }

template<typename T>
constexpr bool is_close(T a, T b) {
    return std::abs(a - b) <=
           std::numeric_limits<T>::epsilon() * std::abs(a + b)
           || std::abs(a - b) < std::numeric_limits<T>::min();
}

// CPP(14): Compute the square root during compile time for consts.
constexpr double sqrt_helper(double x, double curr, double prev) {
    return is_close(curr, prev) ? curr : sqrt_helper(x, 0.5 * (curr + x / curr), curr);
}

/*
* Constexpr version of the square root
* Returns an approximation for the square root of "x"
* Invariant: finite and non-negative value of "x"
*/
constexpr double sqrt(double x) { return sqrt_helper(x, x, 0); }

/**
 * 8-point discrete cosine transform (DCT)
 *
 * Based on https://dev.w3.org/Amaya/libjpeg/jfdctflt.c.
 * Forward DCT computation "in one dimension" (fast AAN algorithm by Arai, Agui and Nakajima: "A fast DCT-SQ scheme for images")
 * With 5 multiplications and 28 additions.
 * @tparam horizontal True if computing rows, false for columns
 * @param block Input/Output block. Changes values in-place.
 */
void DCT(float block[8 * 8], bool horizontal) noexcept {
    constexpr double SqrtHalfSqrt = sqrt((2 + sqrt(2)) / 2);
    constexpr double InvSqrt = 1.0 / sqrt(2);
    constexpr double HalfSqrtSqrt = sqrt(2 - sqrt(2)) / 2.0;
    constexpr double InvSqrtSqrt = 1.0 / sqrt(2 + sqrt(2));
    uint8_t stride = horizontal ? 1 : 8;

    // modify in-place
    auto &block0 = block[0];
    auto &block1 = block[1 * stride];
    auto &block2 = block[2 * stride];
    auto &block3 = block[3 * stride];
    auto &block4 = block[4 * stride];
    auto &block5 = block[5 * stride];
    auto &block6 = block[6 * stride];
    auto &block7 = block[7 * stride];

    auto add07 = block0 + block7;
    auto sub07 = block0 - block7;
    auto add16 = block1 + block6;
    auto sub16 = block1 - block6;
    auto add25 = block2 + block5;
    auto sub25 = block2 - block5;
    auto add34 = block3 + block4;
    auto sub34 = block3 - block4;

    auto add0347 = add07 + add34;
    auto sub07_34 = add07 - add34; // "even part" / "phase 2"
    auto add1256 = add16 + add25;
    auto sub16_25 = add16 - add25;

    block[0] = add0347 + add1256;
    block4 = add0347 - add1256; // "phase 3"

    auto z1 = (sub16_25 + sub07_34) * InvSqrt;
    block2 = sub07_34 + z1;
    block6 = sub07_34 - z1; // "phase 5"

    auto sub23_45 = sub25 + sub34; // "odd part" / "phase 2"
    auto sub12_56 = sub16 + sub25;
    auto sub01_67 = sub16 + sub07;

    auto z5 = (sub23_45 - sub01_67) * HalfSqrtSqrt;
    auto z2 = sub23_45 * InvSqrtSqrt + z5;
    auto z3 = sub12_56 * InvSqrt;
    auto z4 = sub01_67 * SqrtHalfSqrt + z5;
    auto z6 = sub07 + z3; // "phase 5"
    auto z7 = sub07 - z3;
    block1 = z6 + z4;
    block7 = z6 - z4; // "phase 6"
    block5 = z7 + z2;
    block3 = z7 - z2;
}


// CPP: Compile time huffman table computation
constexpr std::array<BitCode, 256> generateHuffmanTable(const uint8_t numCodes[16], const uint8_t *values) {
    std::array<BitCode, 256> result{};

    // process all bitsizes 1 thru 16, no JPEG Huffman code is allowed to exceed 16 bits
    uint16_t huffmanCode = 0;
    for (auto numBits = 1; numBits <= 16; numBits++) {
        // ... and each code of these bitsizes
        for (auto i = 0;
             i < numCodes[numBits - 1]; i++) // note: numCodes array starts at zero, but smallest bitsize is 1
            result[*values++] = BitCode(huffmanCode++, numBits);

        // next Huffman code needs to be one bit wider
        huffmanCode <<= 1u;
    }

    return result;
}

constexpr auto ht_l_dc = generateHuffmanTable(DcLuminanceCodesPerBitsize, DcLuminanceValues);
constexpr auto ht_l_ac = generateHuffmanTable(AcLuminanceCodesPerBitsize, AcLuminanceValues);
constexpr auto ht_c_dc = generateHuffmanTable(DcChrominanceCodesPerBitsize, DcChrominanceValues);
constexpr auto ht_c_ac = generateHuffmanTable(AcChrominanceCodesPerBitsize, AcChrominanceValues);

// compile-time compute Huffman code tables
constexpr std::tuple<const BitCode *, const BitCode *> huffman_luminance() {
    return std::make_tuple(ht_l_dc.data(), ht_l_ac.data());
}

// chrominance is only relevant for color images
constexpr std::tuple<const BitCode *, const BitCode *> huffman_chrominance() {
    return std::make_tuple(ht_c_dc.data(), ht_c_ac.data());
}

constexpr std::tuple<const BitCode *, const BitCode *> huffman(bool luminance) {
    return luminance ? huffman_luminance() : huffman_chrominance();
}

constexpr std::array<BitCode, 2 * CodeWordLimit> codewordsArray = codewords_for_quantized_dct();

/**
 * Run DCT, quantize and write Huffman bit codes
 *
 * @param writer Output writer
 * @param block64 A 8*8 block
 * @param scaled scaled luminance / chrominance.
 * @param lastDC
 * @param huffmanDC
 * @param huffmanAC
 * @param codewords
 * @return
 */
template<bool luminance>
int16_t encode_block(BitWriter<> &writer, float *block64, const float* scaled, int16_t lastDC) noexcept {

    const BitCode *codewords = &codewordsArray[CodeWordLimit];
    constexpr auto h = huffman(luminance);
    auto[huffmanDC, huffmanAC] = h;

    // DCT: rows
    for (auto offset = 0; offset < 8; offset++) DCT(block64 + offset * 8, true);
    // DCT: columns
    for (auto offset = 0; offset < 8; offset++) DCT(block64 + offset * 1, false);

    // Scale
    for (auto i = 0; i < 8 * 8; i++) block64[i] *= scaled[i];

    // Encode DC (the first coefficient is the "average color" of the 8x8 block)
    auto DC = int(std::nearbyint(block64[0]));

    // quantize and zigzag the other 63 coefficients
    auto posNonZero = 0; // find last coefficient which is not zero (because trailing zeros are encoded differently)
    int16_t quantized[8 * 8];
    for (auto i = 1; i < 8 * 8; i++) // start at 1 because block64[0]=DC was already processed
    {
        auto value = block64[ZigZagInv[i]];
        quantized[i] = int(std::nearbyint(value));
        // remember offset of last non-zero coefficient
        if (quantized[i] != 0)
            posNonZero = i;
    }

    // same "average color" as previous block ?
    auto diff = DC - lastDC;
    if (diff == 0)
        writer << huffmanDC[0x00];   // yes, write a special short symbol
    else {
        auto bits = codewords[diff]; // nope, encode the difference to previous block's average color
        writer << huffmanDC[bits.numBits] << bits;
    }

    // encode ACs (quantized[1..63])
    auto offset = 0; // upper 4 bits count the number of consecutive zeros
    // quantized[0] was already written, skip all trailing zeros, too
    // zeros are encoded in a special way
    for (auto i = 1; i <= posNonZero; i++) {
        // found another zero ?
        while (quantized[i] == 0) {
            offset += 0x10; // add 1 to the upper 4 bits
            // split into blocks of at most 16 consecutive zeros
            if (offset > 0xF0) // remember, the counter is in the upper 4 bits, 0xF = 15
            {
                writer << huffmanAC[0xF0]; // 0xF0 is a special code for "16 zeros"
                offset = 0;
            }
            i++;
        }

        auto encoded = codewords[quantized[i]];
        // combine number of zeros with the number of bits of the next non-zero value
        writer << huffmanAC[offset + encoded.numBits] << encoded; // and the value itself
        offset = 0;
    }

    // send end-of-block code (0x00), only needed if there are trailing zeros
    if (posNonZero < 8 * 8 - 1) // = 63
        writer << huffmanAC[0x00];

    return DC;
}

bool writeJpegQuality(WRITE_BACK output, const uint8_t *pixels, unsigned short width, unsigned short height,
                      bool downsample, bool isRGB, unsigned char quality_,
                      const std::string_view comment) {

    // grayscale images can't be downsampled (because there are no Cb + Cr channels)
    if (!isRGB) downsample = false;

    if (!(quality_ > 1 && quality_ <= 100)) { throw std::runtime_error("Quality must be in [1..100]"); }

    // convert to an internal JPEG quality factor, formula taken from libjpeg
    auto quality = quality_ < 50 ? 5000 / quality_ : 200 - quality_ * 2;

    // reject invalid pointers
    if (pixels == nullptr)
        return false;
    // check image format
    if (width == 0 || height == 0)
        return false;

    // CPP(14): Compile time compute quantisation tables for the given quality level
    std::array<uint8_t, 8 * 8> quantLuminance = quant_table(DefaultQuantLuminance_A, quality);
    std::array<uint8_t, 8 * 8> quantChrominance = quant_table(DefaultQuantChrominance_A, quality);
    auto scaledLuminance = scaled_luminance(quantLuminance);
    auto scaledChrominance = scaled_chrominance(quantChrominance);

    return writeJpegIntern(BitWriter(std::move(output)), pixels, width, height, downsample, isRGB, quantLuminance, quantChrominance,
                           scaledLuminance.data(), scaledChrominance.data(), comment);
}

bool writeJpegIntern(BitWriter<> bitWriter, const uint8_t *pixels, unsigned short width, unsigned short height,
                     bool downsample, bool isRGB, const std::array<uint8_t, 8 * 8> &quantLuminance,
                     const std::array<uint8_t, 8 * 8> &quantChrominance,
                     const float* scaled_luminance, const float* scaled_chrominance,
                     const std::string_view comment) {

    // number of components
    const uint8_t numComponents = isRGB ? 3 : 1;

    // ////////////////////////////////////////
    // JFIF headers
    const uint8_t HeaderJfif[2 + 2 + 16] =
            {0xFF, 0xD8,         // SOI marker (start of image)
             0xFF, 0xE0,         // JFIF APP0 tag
             0, 16,              // length: 16 bytes (14 bytes payload + 2 bytes for this length field)
             'J', 'F', 'I', 'F', 0, // JFIF identifier, zero-terminated
             1, 1,               // JFIF version 1.1
             0,                 // no density units specified
             0, 1, 0, 1,           // density: 1 pixel "per pixel" horizontally and vertically
             0, 0};             // no thumbnail (size 0 x 0)
    bitWriter << HeaderJfif;

    // comment (optional)
    if (!comment.empty()) {
        // block size is number of bytes (without zero terminator) + 2 bytes for this length field
        bitWriter.addMarker(0xFE_bn, 2 + comment.size());
        bitWriter << comment;
    }

    // write quantization tables
    // each table has 64 entries and is preceded by an ID byte
    // length: 65 bytes per table + 2 bytes for this length field
    bitWriter.addMarker(0xDB_bn, 2 + (isRGB ? 2 : 1) * (1 + 8 * 8));

    // first  quantization table
    bitWriter << 0x00_bn << quantLuminance;
    // second quantization table, only relevant for color images
    if (isRGB) bitWriter << 0x01_bn << quantChrominance;

    // ////////////////////////////////////////
    // write image infos (SOF0 - start of frame)
    // length: 6 bytes general info + 3 per channel + 2 bytes for this length field
    bitWriter.addMarker(0xC0_bn, 2 + 6 + 3 * numComponents);

    // 8 bits per channel
    bitWriter << 0x08_bn
              // image dimensions (big-endian)
              << (height >> 8u) << (height & 0xFFu)
              << (width >> 8u) << (width & 0xFFu);

    // sampling and quantization tables for each component
    bitWriter << numComponents;       // 1 component (grayscale, Y only) or 3 components (Y,Cb,Cr)
    for (uint8_t id = 1; id <= numComponents; id++)
        bitWriter << id                // component ID (Y=1, Cb=2, Cr=3)
                  // bitmasks for sampling: highest 4 bits: horizontal, lowest 4 bits: vertical
                  << (id == 1 && downsample ? 0x22
                                            : 0x11) // 0x11 is default YCbCr 4:4:4 and 0x22 stands for YCbCr 4:2:0
                  << (id == 1 ? 0 : 1); // use quantization table 0 for Y, table 1 for Cb and Cr

    // ////////////////////////////////////////
    // Huffman tables

    constexpr auto len = 1 + 16 + 12 +  // for the DC luminance (chrominance)
                         1 + 16 + 162; // for the AC luminance (chrominance)
    // DHT marker - define Huffman tables; 2 bytes for the length field, store chrominance only if needed
    bitWriter.addMarker(0xC4_bn, isRGB ? (2 + len + len) : (2 + len));

    // store luminance's DC+AC Huffman table definitions
    bitWriter << 0x00_bn // highest 4 bits: 0 => DC, lowest 4 bits: 0 => Y (baseline)
              << DcLuminanceCodesPerBitsize
              << DcLuminanceValues;
    bitWriter << 0x10_bn // highest 4 bits: 1 => AC, lowest 4 bits: 0 => Y (baseline)
              << AcLuminanceCodesPerBitsize
              << AcLuminanceValues;

    // chrominance is only relevant for color images
    if (isRGB) {
        // store luminance's DC+AC Huffman table definitions
        bitWriter << 0x01_bn // highest 4 bits: 0 => DC, lowest 4 bits: 1 => Cr,Cb (baseline)
                  << DcChrominanceCodesPerBitsize
                  << DcChrominanceValues;
        bitWriter << 0x11_bn // highest 4 bits: 1 => AC, lowest 4 bits: 1 => Cr,Cb (baseline)
                  << AcChrominanceCodesPerBitsize
                  << AcChrominanceValues;
    }

    // ////////////////////////////////////////
    // start of scan (there is only a single scan for baseline JPEGs)
    // 2 bytes for the length field, 1 byte for number of components,
    // then 2 bytes for each component and 3 bytes for spectral selection
    bitWriter.addMarker(0xDA_bn, 2 + 1 + 2 * numComponents + 3);

    // assign Huffman tables to each component
    bitWriter << numComponents;
    for (auto id = 1; id <= numComponents; id++)
        // highest 4 bits: DC Huffman table, lowest 4 bits: AC Huffman table
        // Y: tables 0 for DC and AC; Cb + Cr: tables 1 for DC and AC
        bitWriter << id << (id == 1 ? 0x00_bn : 0x11_bn);

    // constant values for our baseline JPEGs (which have a single sequential scan)
    // spectral selection: must be from 0 to 63; successive approximation must be 0
    constexpr uint8_t Spectral[3] = {0, 63, 0};
    bitWriter << Spectral;

    // the next two variables are frequently used when checking for image borders
    const auto maxWidth = width - 1; // "last row"
    const auto maxHeight = height - 1; // "bottom line"

    // process MCUs (minimum codes units) => image is subdivided into a grid of 8x8 or 16x16 tiles
    const auto sampling = downsample ? 2 : 1; // 1x1 or 2x2 sampling
    const auto mcuSize = 8 * sampling;

    // average color of the previous MCU
    int16_t lastYDC = 0, lastCbDC = 0, lastCrDC = 0;
    // convert from RGB to YCbCr
    // Unsafe: Explicitly not initialize those variables, they will be filled completely.
    std::array<std::array<float, 8>, 8> Y, Cb, Cr;
    // Multi-dimensional size == one-dimensional size? Check mem layout (no padding)
    static_assert(sizeof(Y) == sizeof(std::array<float, 8 * 8>));

    for (auto mcuY = 0; mcuY < height; mcuY += mcuSize) // each step is either 8 or 16 (=mcuSize)
        for (auto mcuX = 0; mcuX < width; mcuX += mcuSize) {
            // YCbCr 4:4:4 format: each MCU is a 8x8 block - the same applies to grayscale images, too
            // YCbCr 4:2:0 format: each MCU represents a 16x16 block, stored as 4x 8x8 Y-blocks plus 1x 8x8 Cb and 1x 8x8 Cr block)
            // iterate once (YCbCr444 and grayscale) or twice (YCbCr420)
            for (auto blockY = 0; blockY < mcuSize; blockY += 8)
                for (auto blockX = 0; blockX < mcuSize; blockX += 8) {
                    // now we finally have an 8x8 block ...
                    for (auto deltaY = 0; deltaY < 8; deltaY++) {
                        // must not exceed image borders, replicate last row/column if needed
                        auto column = std::min(mcuX + blockX, maxWidth);
                        auto row = std::min(mcuY + blockY + deltaY, maxHeight);
                        for (auto deltaX = 0; deltaX < 8; deltaX++) {
                            // find actual pixel position within the current image
                            // the cast ensures that we don't run into multiplication overflows
                            auto pixelPos = row * int(width) + column;
                            if (column < maxWidth) column++;

                            // grayscale images have solely a Y channel which can be easily derived from the input pixel by shifting it by 128
                            if (!isRGB) {
                                Y[deltaY][deltaX] = float(pixels[pixelPos]) - 128.f;
                                continue;
                            }

                            // RGB: 3 bytes per pixel (whereas grayscale images have only 1 byte per pixel)
                            auto r = pixels[3 * pixelPos];
                            auto g = pixels[3 * pixelPos + 1];
                            auto b = pixels[3 * pixelPos + 2];

                            Y[deltaY][deltaX] = rgb2y(r, g, b) -
                                                128; // again, the JPEG standard requires Y to be shifted by 128
                            // YCbCr444 is easy - the more complex YCbCr420 has to be computed about 20 lines below in a second pass
                            if (!downsample) {
                                Cb[deltaY][deltaX] = rgb2cb(r, g, b); // standard RGB-to-YCbCr conversion
                                Cr[deltaY][deltaX] = rgb2cr(r, g, b);
                            }
                        }
                    }

                    // encode Y channel
                    lastYDC = encode_block<true>(bitWriter, reinterpret_cast<float *>(Y.data()),
                                                 scaled_luminance, lastYDC);
                    // Cb and Cr are encoded about 50 lines below
                }

            // grayscale images don't need any Cb and Cr information
            if (!isRGB)
                continue;

            // ////////////////////////////////////////
            // the following lines are only relevant for YCbCr420:
            // average/downsample chrominance of four pixels while respecting the image borders
            if (downsample)
                // iterating loop in reverse increases cache read efficiency
                for (short deltaY = 7; deltaY >= 0; deltaY--) {
                    auto row = std::min(mcuY + 2 * deltaY, maxHeight); // each deltaX/Y step covers a 2x2 area
                    auto column = mcuX;                        // column is updated inside next loop
                    auto pixelPos = (row * int(width) + column) * 3;     // numComponents = 3

                    // deltas (in bytes) to next row / column, must not exceed image borders
                    auto rowStep = (row < maxHeight) ? 3 * int(width)
                                                     : 0; // always numComponents*width except for bottom    line
                    auto columnStep = (column < maxWidth) ? 3
                                                          : 0; // always numComponents       except for rightmost pixel

                    for (short deltaX = 0; deltaX < 8; deltaX++) {
                        // let's add all four samples (2x2 area)
                        auto right = pixelPos + columnStep;
                        auto down = pixelPos + rowStep;
                        auto downRight = pixelPos + columnStep + rowStep;

                        // note: cast from 8 bits to >8 bits to avoid overflows when adding
                        auto r = short(pixels[pixelPos]) + pixels[right] + pixels[down] + pixels[downRight];
                        auto g = short(pixels[pixelPos + 1]) + pixels[right + 1] + pixels[down + 1] +
                                 pixels[downRight + 1];
                        auto b = short(pixels[pixelPos + 2]) + pixels[right + 2] + pixels[down + 2] +
                                 pixels[downRight + 2];

                        // convert to Cb and Cr
                        Cb[deltaY][deltaX] = rgb2cb(r, g, b) /
                                             4; // I still have to divide r,g,b by 4 to get their average values
                        Cr[deltaY][deltaX] = rgb2cr(r, g, b) / 4; // it's a bit faster if done AFTER CbCr conversion

                        // step forward to next 2x2 area
                        pixelPos += 2 * 3; // 2 pixels => 6 bytes (2*numComponents)
                        column += 2;

                        // reached right border ?
                        if (column >= maxWidth) {
                            columnStep = 0;
                            pixelPos = ((row + 1) * int(width) - 1) *
                                       3; // same as (row * width + maxWidth) * numComponents => current's row last pixel
                        }
                    }
                } // end of YCbCr420 code for Cb and Cr

            // encode Cb and Cr
            // reinterpret_cast necessary to go from multi-dimensional std::array to a flat view one-dimensional array
            lastCbDC = encode_block<false>(bitWriter, reinterpret_cast<float *>(Cb.data()), scaled_chrominance, lastCbDC);
            lastCrDC = encode_block<false>(bitWriter, reinterpret_cast<float *>(Cr.data()), scaled_chrominance, lastCrDC);
        }

    bitWriter.flush(); // now image is completely encoded, write any bits still left in the buffer

    bitWriter << 0xFF_bn << 0xD9_bn; // this marker has no length, therefore I can't use addMarker()
    return true;
}

} // namespace TooJpeg
