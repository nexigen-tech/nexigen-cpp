// nexigen-canview — bundled startup splash texture.
#pragma once

#include <cstdint>
#include <string>

#include <nexigen/core/result.hpp>

namespace nexigen::canview {

/// @brief Decoded and uploaded startup splash artwork.
///
/// The texture is owned by the GUI thread and must be destroyed while the
/// OpenGL context is current. The embedded PNG is decoded with libpng and freed
/// before this object becomes visible to the renderer.
class SplashImage {
public:
    /// @brief Construct an empty image.
    SplashImage() = default;
    /// @brief Delete the OpenGL texture.
    ~SplashImage();

    SplashImage(const SplashImage&) = delete;
    SplashImage& operator=(const SplashImage&) = delete;

    /// @brief Move-construct an image.
    /// @param other Source image, left empty.
    SplashImage(SplashImage&& other) noexcept;
    /// @brief Move-assign an image, destroying the previous texture.
    /// @param other Source image, left empty.
    SplashImage& operator=(SplashImage&& other) noexcept;

    /// @brief Decode the embedded PNG and upload it as an RGB texture.
    /// @return The image, or a decode/upload failure.
    [[nodiscard]] static nexigen::core::Result<SplashImage> load_embedded() noexcept;

    /// @brief Whether a texture is present.
    /// @return `true` when uploaded.
    [[nodiscard]] bool valid() const noexcept { return texture_ != 0U; }
    /// @brief OpenGL texture id.
    /// @return Texture id, or `0` when empty.
    [[nodiscard]] unsigned int texture() const noexcept { return texture_; }
    /// @brief Decoded image width.
    /// @return Width in pixels.
    [[nodiscard]] int width() const noexcept { return width_; }
    /// @brief Decoded image height.
    /// @return Height in pixels.
    [[nodiscard]] int height() const noexcept { return height_; }

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with dimensions and texture id.
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief Delete the texture if present.
    void release() noexcept;

    unsigned int texture_{0}; ///< OpenGL texture id.
    int width_{0};            ///< Decoded width.
    int height_{0};           ///< Decoded height.
};

/// @brief Free diagnostic mirror of `SplashImage::to_string`.
/// @param image Image to render.
/// @return The same text.
[[nodiscard]] std::string to_string(const SplashImage& image);

} // namespace nexigen::canview
