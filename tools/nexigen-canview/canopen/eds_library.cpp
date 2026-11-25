#include "eds_library.hpp"

#include <format>

#include <sys/stat.h>

namespace nexigen::canview {

std::string EdsLoadResult::to_string() const {
    return std::format("EdsLoadResult(dictionary={}, diagnostics={}, cache={}, reloaded={}, "
                       "failed={})",
                       dictionary != nullptr, diagnostics.size(), from_cache, reloaded, failed);
}

bool EdsLibrary::stat_file(const std::string& path, std::size_t& size,
                           std::int64_t& mtime) noexcept {
    struct stat info{};
    if (::stat(path.c_str(), &info) != 0) {
        return false;
    }
    size = static_cast<std::size_t>(info.st_size);
    mtime = static_cast<std::int64_t>(info.st_mtime);
    return true;
}

EdsLoadResult EdsLibrary::load(const std::string& path) {
    EdsLoadResult result{};
    std::size_t size = 0U;
    std::int64_t mtime = 0;
    if (!stat_file(path, size, mtime)) {
        result.diagnostics.push_back(EdsDiagnostic{0U, "cannot stat '" + path + "'"});
        result.failed = true;
        return result;
    }

    const auto cached = cache_.find(path);
    if (cached != cache_.end() && cached->second.size == size && cached->second.mtime == mtime) {
        result.dictionary = cached->second.dictionary;
        result.from_cache = true;
        return result;
    }

    const bool replacing = cached != cache_.end();
    if (!replacing && cache_.size() >= max_files) {
        result.diagnostics.push_back(EdsDiagnostic{0U, "EDS file limit reached"});
        result.failed = true;
        return result;
    }
    const std::uint64_t previous_bytes = replacing ? cached->second.size : 0U;
    if (total_bytes_ - previous_bytes + size > budget_bytes_) {
        result.diagnostics.push_back(EdsDiagnostic{0U, "EDS memory budget exceeded"});
        result.failed = true;
        return result;
    }

    EdsParseResult parsed = EdsParser::load(path, limits_);
    result.diagnostics = std::move(parsed.diagnostics);
    result.reloaded = replacing;
    if (!parsed.ok()) {
        if (replacing) {
            result.dictionary = cached->second.dictionary;
        }
        result.failed = true;
        return result;
    }
    const std::shared_ptr<const EdsDictionary> dictionary = std::move(parsed.dictionary);
    if (replacing) {
        total_bytes_ -= previous_bytes;
    }
    total_bytes_ += size;
    cache_[path] = Entry{dictionary, size, mtime};
    result.dictionary = dictionary;
    return result;
}

std::shared_ptr<const EdsDictionary> EdsLibrary::find(const std::string& path) const {
    const auto it = cache_.find(path);
    return it == cache_.end() ? nullptr : it->second.dictionary;
}

void EdsLibrary::clear() noexcept {
    cache_.clear();
    total_bytes_ = 0U;
}

} // namespace nexigen::canview
