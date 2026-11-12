// nexigen::hub — embedded web assets
// (change nexigen-hub; spec hub/web-ui).
#pragma once

#include <string_view>

namespace nexigen::hub {

/// @brief The embedded single-page UI document.
///
/// The asset is compiled into the hub binary, so `/` works without a build
/// step, external font, CDN, or any network request beyond the hub origin.
///
/// @return View of static storage valid for the whole process lifetime.
/// @note Spec `hub/web-ui`, requirement "UI is a single embedded application".
/// @note Spec `hub/http-api`, requirement "Web assets are served by the binary".
[[nodiscard]] std::string_view index_html() noexcept;

/// @brief The embedded browser script driving tree and stream views.
///
/// Parses and bootstraps on load: it opens the SSE tree subscription, fetches
/// the initial tree, and moves the connection badge out of `connecting`.
///
/// @return View of static storage valid for the whole process lifetime.
/// @note Spec `hub/web-ui`, requirement "Embedded script parses and
///       bootstraps".
/// @note Spec `hub/http-api`, requirement "Web assets are served by the binary".
[[nodiscard]] std::string_view app_js() noexcept;

/// @brief The embedded stylesheet for the single-page UI.
///
/// @return View of static storage valid for the whole process lifetime.
/// @note Spec `hub/web-ui`, requirement "UI is a single embedded application".
/// @note Spec `hub/http-api`, requirement "Web assets are served by the binary".
[[nodiscard]] std::string_view style_css() noexcept;

} // namespace nexigen::hub
