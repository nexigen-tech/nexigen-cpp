#include "splash_image.hpp"

#include <format>
#include <vector>

#include <GL/gl.h>
#include <png.h>

#include "nexigen_splash_asset.hpp"

namespace nexigen::logview {

SplashImage::~SplashImage() {
    release();
}

SplashImage::SplashImage(SplashImage&& other) noexcept
    : texture_(other.texture_), width_(other.width_), height_(other.height_) {
    other.texture_ = 0;
    other.width_ = 0;
    other.height_ = 0;
}

SplashImage& SplashImage::operator=(SplashImage&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    release();
    texture_ = other.texture_;
    width_ = other.width_;
    height_ = other.height_;
    other.texture_ = 0;
    other.width_ = 0;
    other.height_ = 0;
    return *this;
}

void SplashImage::release() noexcept {
    if (texture_ != 0U) {
        glDeleteTextures(1, &texture_);
        texture_ = 0U;
    }
}

nexigen::core::Result<SplashImage> SplashImage::load_embedded() noexcept {
    png_image png{};
    png.version = PNG_IMAGE_VERSION;
    if (png_image_begin_read_from_memory(&png, assets::kNexigenSplashPng.data(),
                                         assets::kNexigenSplashPng.size()) == 0) {
        return std::unexpected(nexigen::core::ErrorCode::CorruptData);
    }
    png.format = PNG_FORMAT_RGB;
    std::vector<unsigned char> pixels(PNG_IMAGE_SIZE(png));
    if (png_image_finish_read(&png, nullptr, pixels.data(), 0, nullptr) == 0) {
        png_image_free(&png);
        return std::unexpected(nexigen::core::ErrorCode::CorruptData);
    }
    png_image_free(&png);

    SplashImage image{};
    image.width_ = static_cast<int>(png.width);
    image.height_ = static_cast<int>(png.height);
    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (texture == 0U) {
        return std::unexpected(nexigen::core::ErrorCode::SystemError);
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    while (glGetError() != GL_NO_ERROR) {}
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, image.width_, image.height_, 0, GL_RGB, GL_UNSIGNED_BYTE,
                 pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    if (glGetError() != GL_NO_ERROR) {
        glDeleteTextures(1, &texture);
        return std::unexpected(nexigen::core::ErrorCode::SystemError);
    }
    image.texture_ = texture;
    return image;
}

std::string SplashImage::to_string() const {
    return std::format("SplashImage(width={}, height={}, texture={})", width_, height_, texture_);
}

std::string to_string(const SplashImage& image) {
    return image.to_string();
}

} // namespace nexigen::logview
