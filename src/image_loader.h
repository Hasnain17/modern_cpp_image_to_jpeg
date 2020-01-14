//! CPP Api Wrapper around stbi (image loader header-only library)
#pragma once

namespace {
#define STB_IMAGE_IMPLEMENTATION

#include "vendor/stb_image.h"
}

/**
 * CPP Api Wrapper around stbi (image loader header-only library)
 */
struct ImageLoader {
    int width, height, channels;
    unsigned char *image;

    explicit ImageLoader(char const *filename) noexcept: width(0), height(0), channels(0) {
        stbi_set_flip_vertically_on_load(false);
        image = stbi_load(filename, &width, &height, &channels, STBI_rgb);
    }

    ~ImageLoader() { stbi_image_free(image); }

    /// Return true if the image data is valid
    bool is_valid() { return image != nullptr; }

    /// Dereference operator returns the raw image data pointer
    unsigned char *operator*() {
        return image;
    }
};
