// nexigen::exec — manifest format accessor
// (change p3-arxml-manifest-accessor).
#include <nexigen/exec/manifest_accessor.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include <nexigen/core/settings_io.hpp>
#include <nexigen/exec/detail/path_utils.hpp>
#include <nexigen/exec/machine_manifest.hpp>

namespace nexigen::exec {

namespace {

struct XmlNode {
    std::string name{};
    std::string text{};
    std::vector<XmlNode> children{};
};

[[nodiscard]] bool starts_with(std::string_view text, std::string_view prefix) noexcept {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] bool is_space(char ch) noexcept {
    return std::isspace(static_cast<unsigned char>(ch)) != 0;
}

[[nodiscard]] std::string trim(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && is_space(text[begin])) {
        ++begin;
    }
    while (end > begin && is_space(text[end - 1])) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

[[nodiscard]] std::string decode_entities(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size();) {
        if (raw[i] != '&') {
            out.push_back(raw[i++]);
            continue;
        }
        const std::size_t semicolon = raw.find(';', i + 1);
        if (semicolon == std::string_view::npos || semicolon - i > 12) {
            out.push_back(raw[i++]);
            continue;
        }
        const std::string_view entity = raw.substr(i + 1, semicolon - i - 1);
        if (entity == "amp") {
            out.push_back('&');
        } else if (entity == "lt") {
            out.push_back('<');
        } else if (entity == "gt") {
            out.push_back('>');
        } else if (entity == "quot") {
            out.push_back('"');
        } else if (entity == "apos") {
            out.push_back('\'');
        } else if (entity.size() > 1 && entity[0] == '#') {
            std::uint32_t value = 0;
            if (entity[1] == 'x' || entity[1] == 'X') {
                value = static_cast<std::uint32_t>(
                    std::strtoul(std::string(entity.substr(2)).c_str(), nullptr, 16));
            } else {
                value = static_cast<std::uint32_t>(
                    std::strtoul(std::string(entity.substr(1)).c_str(), nullptr, 10));
            }
            out.push_back(static_cast<char>(value & 0xffU));
        } else {
            out.append(raw.substr(i, semicolon - i + 1));
        }
        i = semicolon + 1;
    }
    return out;
}

class XmlParser {
public:
    explicit XmlParser(std::string_view input) noexcept : input_(input) {}

    [[nodiscard]] core::Result<XmlNode> parse() {
        XmlNode root;
        if (const core::Status status = parse_element(root); !status) {
            return std::unexpected(status.error());
        }
        skip_misc();
        if (pos_ != input_.size()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        return root;
    }

private:
    void skip_misc() noexcept {
        for (;;) {
            while (pos_ < input_.size() && is_space(input_[pos_])) {
                ++pos_;
            }
            if (starts_with(input_.substr(pos_), "<!--")) {
                const std::size_t end = input_.find("-->", pos_ + 4);
                pos_ = end == std::string_view::npos ? input_.size() : end + 3;
                continue;
            }
            if (starts_with(input_.substr(pos_), "<?")) {
                const std::size_t end = input_.find("?>", pos_ + 2);
                pos_ = end == std::string_view::npos ? input_.size() : end + 2;
                continue;
            }
            if (starts_with(input_.substr(pos_), "<!DOCTYPE")) {
                const std::size_t end = input_.find('>', pos_ + 9);
                pos_ = end == std::string_view::npos ? input_.size() : end + 1;
                continue;
            }
            return;
        }
    }

    [[nodiscard]] std::string read_name() {
        const std::size_t begin = pos_;
        while (pos_ < input_.size() && !is_space(input_[pos_]) && input_[pos_] != '>' &&
               input_[pos_] != '/' && input_[pos_] != '=') {
            ++pos_;
        }
        std::string name(input_.substr(begin, pos_ - begin));
        const std::size_t colon = name.find(':');
        if (colon != std::string::npos) {
            name.erase(0, colon + 1);
        }
        return name;
    }

    void skip_attributes() noexcept {
        while (pos_ < input_.size()) {
            const char ch = input_[pos_];
            if (ch == '>' || ch == '/') {
                return;
            }
            if (ch == '"' || ch == '\'') {
                const std::size_t end = input_.find(ch, pos_ + 1);
                pos_ = end == std::string_view::npos ? input_.size() : end + 1;
                continue;
            }
            ++pos_;
        }
    }

    [[nodiscard]] bool fail() const noexcept { return pos_ >= input_.size(); }

    [[nodiscard]] core::Status read_start_tag(XmlNode& out, bool& self_closing) {
        skip_misc();
        if (fail() || input_[pos_] != '<') {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        ++pos_;
        if (fail() || input_[pos_] == '/') {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        out.name = read_name();
        if (out.name.empty()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        skip_attributes();
        if (fail()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        if (input_[pos_] == '/') {
            self_closing = true;
            if (pos_ + 1 >= input_.size() || input_[pos_ + 1] != '>') {
                return std::unexpected(core::ErrorCode::InvalidArgument);
            }
            pos_ += 2;
            return {};
        }
        if (input_[pos_] != '>') {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        ++pos_;
        return {};
    }

    void append_text(XmlNode& out) noexcept {
        const std::size_t begin = pos_;
        while (pos_ < input_.size() && input_[pos_] != '<') {
            ++pos_;
        }
        out.text += decode_entities(input_.substr(begin, pos_ - begin));
    }

    [[nodiscard]] bool consume_comment() noexcept {
        if (!starts_with(input_.substr(pos_), "<!--")) {
            return false;
        }
        skip_misc();
        return true;
    }

    [[nodiscard]] core::Status consume_cdata(XmlNode& out) {
        if (!starts_with(input_.substr(pos_), "<![CDATA[")) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        const std::size_t end = input_.find("]]>", pos_ + 9);
        if (end == std::string_view::npos) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        out.text.append(input_.substr(pos_ + 9, end - pos_ - 9));
        pos_ = end + 3;
        return {};
    }

    [[nodiscard]] core::Status parse_close_tag(std::string_view name) {
        pos_ += 2;
        const std::string closing = read_name();
        while (pos_ < input_.size() && is_space(input_[pos_])) {
            ++pos_;
        }
        if (closing != name || fail() || input_[pos_] != '>') {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        ++pos_;
        return {};
    }

    [[nodiscard]] core::Status parse_children(XmlNode& out) {
        for (;;) {
            if (fail()) {
                return std::unexpected(core::ErrorCode::InvalidArgument);
            }
            if (input_[pos_] != '<') {
                append_text(out);
                continue;
            }
            if (consume_comment()) {
                continue;
            }
            if (starts_with(input_.substr(pos_), "<![CDATA[")) {
                if (const core::Status status = consume_cdata(out); !status) {
                    return status;
                }
                continue;
            }
            if (starts_with(input_.substr(pos_), "</")) {
                return parse_close_tag(out.name);
            }
            XmlNode child;
            if (const core::Status status = parse_element(child); !status) {
                return status;
            }
            out.children.push_back(std::move(child));
        }
    }

    [[nodiscard]] core::Status parse_element(XmlNode& out) {
        bool self_closing = false;
        if (const core::Status status = read_start_tag(out, self_closing); !status) {
            return status;
        }
        if (self_closing) {
            return {};
        }
        return parse_children(out);
    }

    std::string_view input_;
    std::size_t pos_{0};
};

[[nodiscard]] std::string tag_key(std::string_view name) {
    std::string key;
    key.reserve(name.size());
    for (const char ch : name) {
        key.push_back(ch == '-' ? '_'
                                : static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return key;
}

[[nodiscard]] bool is_array_key(std::string_view key) noexcept {
    return key == "function_groups" || key == "apps" || key == "startup_configs" || key == "args" ||
           key == "states" || key == "dependencies" || key == "resource_groups" ||
           key == "function_group_port_mapping" || key == "env" || key == "cpus" ||
           key == "machines" || key == "processes";
}

/// @brief Map XML container tag keys whose JSON spelling differs.
[[nodiscard]] std::string_view json_key(std::string_view key) noexcept {
    return key == "applications" ? std::string_view("apps") : key;
}

[[nodiscard]] bool is_numeric_key(std::string_view key) noexcept {
    return key == "version" || key == "suite_restart_attempts" || key == "suite_restart_delay_s" ||
           key == "idle_timeout_s" || key == "startup_timeout_ms" ||
           key == "termination_timeout_ms" || key == "restart_attempts" ||
           key == "restart_delay_s" || key == "memory_limit_mb" || key == "priority" ||
           key == "cpu";
}

[[nodiscard]] bool is_bool_key(std::string_view key) noexcept {
    return key == "reporting" || key == "require_executable_digest";
}

[[nodiscard]] core::Result<nlohmann::json> scalar_json(std::string_view key, std::string_view raw) {
    const std::string text = trim(raw);
    if (is_numeric_key(key)) {
        if (text.empty()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        char* end = nullptr;
        const unsigned long long value = std::strtoull(text.c_str(), &end, 10);
        if (end == nullptr || *end != '\0') {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        return nlohmann::json(static_cast<std::uint64_t>(value));
    }
    if (is_bool_key(key)) {
        if (text == "true") {
            return nlohmann::json(true);
        }
        if (text == "false") {
            return nlohmann::json(false);
        }
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return nlohmann::json(text);
}

[[nodiscard]] core::Result<nlohmann::json> node_json(const XmlNode& node, std::string_view key);

[[nodiscard]] core::Result<nlohmann::json> array_json(const XmlNode& node, std::string_view key) {
    if (key == "env") {
        nlohmann::json object = nlohmann::json::object();
        for (const XmlNode& entry : node.children) {
            std::string name;
            std::string value;
            for (const XmlNode& field : entry.children) {
                const std::string field_key = tag_key(field.name);
                if (field_key == "key") {
                    name = trim(field.text);
                } else if (field_key == "value") {
                    value = field.text;
                }
            }
            if (!name.empty()) {
                object[name] = value;
            }
        }
        return object;
    }
    nlohmann::json array = nlohmann::json::array();
    if (key == "cpus") {
        for (const XmlNode& child : node.children) {
            const auto value = scalar_json("cpu", child.text);
            if (!value) {
                return std::unexpected(value.error());
            }
            array.push_back(*value);
        }
        return array;
    }
    for (const XmlNode& child : node.children) {
        const auto value = node_json(child, json_key(tag_key(child.name)));
        if (!value) {
            return std::unexpected(value.error());
        }
        array.push_back(*value);
    }
    return array;
}

[[nodiscard]] core::Result<nlohmann::json> node_json(const XmlNode& node, std::string_view key) {
    if (node.children.empty() && key != "cpus" && key != "env") {
        return scalar_json(key, node.text);
    }
    if (key == "cpus" || is_array_key(key)) {
        return array_json(node, key);
    }
    nlohmann::json object = nlohmann::json::object();
    for (const XmlNode& child : node.children) {
        const std::string child_key(json_key(tag_key(child.name)));
        const auto value = node_json(child, child_key);
        if (!value) {
            return std::unexpected(value.error());
        }
        object[child_key] = *value;
    }
    return object;
}

} // namespace

ManifestFormat ManifestAccessor::detect(std::string_view path, std::string_view content) noexcept {
    if (path.size() >= 6 && path.substr(path.size() - 6) == ".arxml") {
        return ManifestFormat::Arxml;
    }
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".xml") {
        return ManifestFormat::Arxml;
    }
    for (const char ch : content) {
        if (!is_space(ch)) {
            return ch == '<' ? ManifestFormat::Arxml : ManifestFormat::Json;
        }
    }
    return ManifestFormat::Json;
}

core::Status ManifestAccessor::load(std::string_view path, SuiteManifest& out) noexcept {
    std::string text;
    const core::settings_io::FileRead read = core::settings_io::read_text_file(path, text);
    if (read != core::settings_io::FileRead::Ok) {
        return std::unexpected(read == core::settings_io::FileRead::Absent
                                   ? core::ErrorCode::InvalidArgument
                                   : core::ErrorCode::SystemError);
    }
    if (detect(path, text) == ManifestFormat::Json) {
        return load_suite_manifest(path, out);
    }

    XmlParser parser(text);
    auto root = parser.parse();
    if (!root || root->name != "SUITE-MANIFEST") {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    const auto document = node_json(*root, "suite_manifest");
    if (!document) {
        return std::unexpected(document.error());
    }
    SuiteManifest parsed{};
    from_json(*document, parsed);
    parsed.manifest_path = detail::absolute_from_cwd(path);
    if (const core::Status valid = validate_suite_manifest(parsed); !valid) {
        return valid;
    }
    out = std::move(parsed);
    return {};
}

core::Status load_suite_manifest_auto(std::string_view path, SuiteManifest& out) noexcept {
    return ManifestAccessor::load(path, out);
}

core::Status ManifestAccessor::load_machine(std::string_view path, MachineManifest& out) noexcept {
    std::string text;
    const core::settings_io::FileRead read = core::settings_io::read_text_file(path, text);
    if (read != core::settings_io::FileRead::Ok) {
        return std::unexpected(read == core::settings_io::FileRead::Absent
                                   ? core::ErrorCode::InvalidArgument
                                   : core::ErrorCode::SystemError);
    }
    MachineManifest parsed{};
    if (detect(path, text) == ManifestFormat::Json) {
        const nlohmann::json document = nlohmann::json::parse(text, nullptr, false);
        if (document.is_discarded()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        from_json(document, parsed);
    } else {
        XmlParser parser(text);
        auto root = parser.parse();
        if (!root || root->name != "MACHINE-MANIFEST") {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        const auto document = node_json(*root, "machine_manifest");
        if (!document) {
            return std::unexpected(document.error());
        }
        from_json(*document, parsed);
    }
    if (const core::Status resolved = parsed.resolve_references(); !resolved) {
        return resolved;
    }
    if (const core::Status valid = parsed.validate(); !valid) {
        return valid;
    }
    out = std::move(parsed);
    return {};
}

core::Status load_machine_manifest_auto(std::string_view path, MachineManifest& out) noexcept {
    return ManifestAccessor::load_machine(path, out);
}

} // namespace nexigen::exec
