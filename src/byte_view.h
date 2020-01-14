#pragma once

#include <cstddef>
#include <cstdint>

/**
 * A non-owned byte range
 */
struct ByteView {
    const std::uint8_t one_byte_ = 0;
    const std::uint8_t *const ptr_;
    const std::uint64_t size_;

    ByteView() : ptr_(nullptr), size_(0) {}

    inline ByteView(const void *ptr, std::uint64_t size) : ptr_(reinterpret_cast<const std::uint8_t *>(ptr)), size_(size) {}

    /**
     * Create a ByteView of a fixed C-Array.
     * @tparam T The array type
     * @tparam Size The compile-time known size. Should be automatically deduced.
     * @param data The array
     */
    template<typename T, int Size>
    inline ByteView(T (&data)[Size]) : ptr_(reinterpret_cast<const std::uint8_t *>(data)), size_(Size) {}

    inline ByteView(uint8_t value) : one_byte_(value), ptr_(&one_byte_), size_(1) {}


    inline ByteView(std::byte value) : one_byte_(std::to_integer<std::uint8_t>(value)), ptr_(&one_byte_), size_(1) {}

    /**
     * @return Returns a pointer to the non-owned data.
     */
    [[nodiscard]] const char *data() const noexcept {
        return reinterpret_cast<const char *>(ptr_);
    }

    /**
     * @return Returns the size of the wrapped data slice.
     */
    [[nodiscard]] std::uint64_t size() const noexcept {
        return size_;
    }
};

