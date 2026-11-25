#include "eds_parser.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <map>
#include <string>

namespace nexigen::canview {

namespace {

char lower(char value) noexcept {
    return (value >= 'A' && value <= 'Z') ? static_cast<char>(value - 'A' + 'a') : value;
}

std::string_view trim(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1U);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1U);
    }
    return text;
}

std::uint32_t parse_uint(std::string_view text, bool& ok) noexcept {
    text = trim(text);
    ok = false;
    if (text.empty()) {
        return 0U;
    }
    int base = 10;
    if (text.size() > 2U && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text.remove_prefix(2U);
    }
    std::uint32_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        return 0U;
    }
    ok = true;
    return value;
}

EdsNumeric parse_numeric(std::string_view text) noexcept {
    EdsNumeric value{};
    text = trim(text);
    if (text.empty()) {
        return value;
    }
    if (text.size() >= 7U && text.substr(0U, 7U) == "$NODEID") {
        value.node_relative = true;
        text.remove_prefix(7U);
        text = trim(text);
        if (!text.empty()) {
            if (text.front() == '+') {
                text.remove_prefix(1U);
                bool ok = false;
                const std::uint32_t offset = parse_uint(text, ok);
                if (!ok) {
                    return value;
                }
                value.node_offset = offset;
            } else {
                return value;
            }
        }
        value.valid = true;
        return value;
    }
    bool ok = false;
    value.value = parse_uint(text, ok);
    value.valid = ok;
    return value;
}

std::uint32_t parse_hex_text(std::string_view text, bool& ok) noexcept {
    text = trim(text);
    ok = false;
    if (text.size() > 2U && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text.remove_prefix(2U);
    }
    if (text.empty()) {
        return 0U;
    }
    std::uint32_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        return 0U;
    }
    ok = true;
    return value;
}

bool parse_section(std::string_view inner, std::uint16_t& index, std::uint8_t& subindex) noexcept {
    inner = trim(inner);
    if (inner.size() < 4U) {
        return false;
    }
    // Find an optional case-insensitive "sub" separator.
    std::size_t separator = std::string_view::npos;
    for (std::size_t position = 4U; position + 3U <= inner.size(); ++position) {
        if (lower(inner[position]) == 's' && lower(inner[position + 1U]) == 'u' &&
            lower(inner[position + 2U]) == 'b') {
            separator = position;
            break;
        }
    }
    const std::string_view index_text =
        inner.substr(0U, separator == std::string_view::npos ? inner.size() : separator);
    bool ok = false;
    const std::uint32_t parsed_index = parse_hex_text(index_text, ok);
    if (!ok || parsed_index > 0xFFFFU) {
        return false;
    }
    index = static_cast<std::uint16_t>(parsed_index);
    subindex = 0U;
    if (separator != std::string_view::npos) {
        bool sub_ok = false;
        const std::uint32_t parsed_sub = parse_hex_text(inner.substr(separator + 3U), sub_ok);
        if (!sub_ok || parsed_sub > 0xFFU) {
            return false;
        }
        subindex = static_cast<std::uint8_t>(parsed_sub);
    }
    return true;
}

EdsObjectType parse_object_type(std::uint32_t value) noexcept {
    switch (value) {
        case 7U: return EdsObjectType::Var;
        case 8U: return EdsObjectType::Array;
        case 9U: return EdsObjectType::Record;
        default: return EdsObjectType::Unknown;
    }
}

bool is_special_section(std::string_view inner) noexcept {
    static constexpr std::string_view names[] = {
        "fileinfo",        "deviceinfo",          "dummyusage", "mandatoryobjects",
        "optionalobjects", "manufacturerobjects", "comments"};
    std::string lowered;
    lowered.reserve(inner.size());
    for (const char character : inner) {
        lowered.push_back(lower(character));
    }
    for (const std::string_view name : names) {
        if (lowered == name) {
            return true;
        }
    }
    return false;
}

void append_diagnostic(std::vector<EdsDiagnostic>& diagnostics, std::size_t line,
                       std::string message) {
    diagnostics.push_back(EdsDiagnostic{line, std::move(message)});
}

EdsObject& ensure_object(std::map<std::uint32_t, EdsObject>& objects, std::uint16_t index,
                         std::uint8_t sub, std::size_t max_objects, bool& too_many) {
    const std::uint32_t key = (static_cast<std::uint32_t>(index) << 8U) | sub;
    const auto found = objects.find(key);
    if (found != objects.end()) {
        return found->second;
    }
    if (objects.size() >= max_objects) {
        too_many = true;
        static EdsObject dummy{};
        dummy = EdsObject{};
        dummy.index = index;
        dummy.subindex = sub;
        return dummy;
    }
    EdsObject object{};
    object.index = index;
    object.subindex = sub;
    return objects.emplace(key, std::move(object)).first->second;
}

void apply_string_key(EdsObject& object, std::string_view key, std::string_view value,
                      std::size_t line_number, const EdsLimits& limits,
                      std::vector<EdsDiagnostic>& diagnostics) {
    if (key == "ParameterName") {
        std::string name(value);
        if (name.size() > limits.max_name_bytes) {
            name.resize(limits.max_name_bytes);
            append_diagnostic(diagnostics, line_number, "parameter name truncated");
        }
        object.name = std::move(name);
        return;
    }
    if (key == "AccessType") {
        object.access = parse_eds_access(value);
        return;
    }
    if (key == "DefaultValue" || key == "COB-ID" || key == "COB_ID") {
        const EdsNumeric numeric = parse_numeric(value);
        if (!numeric.valid) {
            return;
        }
        if (key == "DefaultValue") {
            object.has_default = true;
            object.default_value = numeric;
        } else {
            object.has_cob_id = true;
            object.cob_id = numeric;
        }
    }
}

void apply_numeric_key(EdsObject& object, std::string_view key, std::string_view value,
                       std::size_t line_number, std::vector<EdsDiagnostic>& diagnostics) {
    bool ok = false;
    const std::uint32_t parsed = parse_uint(value, ok);
    if (key == "ObjectType") {
        if (!ok) {
            append_diagnostic(diagnostics, line_number, "invalid ObjectType");
        } else {
            object.type = parse_object_type(parsed);
        }
    } else if (key == "DataType") {
        if (!ok || parsed > 0xFFFFU) {
            append_diagnostic(diagnostics, line_number, "invalid DataType");
        } else {
            object.data_type = static_cast<std::uint16_t>(parsed);
        }
    } else if (key == "PDOMapping") {
        object.pdo_mappable = ok && parsed != 0U;
    } else if (key == "SubNumber" || key == "CompactSubObj") {
        if (!ok || parsed > 0xFFU) {
            append_diagnostic(diagnostics, line_number, "invalid " + std::string(key));
        } else if (key == "SubNumber") {
            object.sub_number = static_cast<std::uint16_t>(parsed);
        } else {
            object.compact_sub_obj = static_cast<std::uint16_t>(parsed);
        }
    }
}

void apply_key(EdsObject& object, std::string_view key, std::string_view value,
               std::size_t line_number, const EdsLimits& limits,
               std::vector<EdsDiagnostic>& diagnostics) {
    if (key == "ParameterName" || key == "AccessType" || key == "DefaultValue" || key == "COB-ID" ||
        key == "COB_ID") {
        apply_string_key(object, key, value, line_number, limits, diagnostics);
        return;
    }
    apply_numeric_key(object, key, value, line_number, diagnostics);
}

void process_line(std::string_view raw, std::size_t line_number, const EdsLimits& limits,
                  std::map<std::uint32_t, EdsObject>& objects, std::uint16_t& current_index,
                  std::uint8_t& current_sub, bool& have_section, bool& too_many,
                  std::vector<EdsDiagnostic>& diagnostics) {
    if (raw.size() > limits.max_line_bytes) {
        append_diagnostic(diagnostics, line_number, "line exceeds the configured limit");
        return;
    }
    std::string_view line = raw;
    if (const std::size_t comment = line.find(';'); comment != std::string_view::npos) {
        line = line.substr(0U, comment);
    }
    line = trim(line);
    if (line.empty()) {
        return;
    }
    if (line.front() == '[' && line.back() == ']') {
        const std::string_view inner = trim(line.substr(1U, line.size() - 2U));
        have_section =
            !is_special_section(inner) && parse_section(inner, current_index, current_sub);
        if (have_section) {
            static_cast<void>(
                ensure_object(objects, current_index, current_sub, limits.max_objects, too_many));
        }
        return;
    }
    if (!have_section) {
        return;
    }
    const std::size_t equals = line.find('=');
    if (equals == std::string_view::npos) {
        append_diagnostic(diagnostics, line_number, "missing '='");
        return;
    }
    apply_key(ensure_object(objects, current_index, current_sub, limits.max_objects, too_many),
              trim(line.substr(0U, equals)), trim(line.substr(equals + 1U)), line_number, limits,
              diagnostics);
}

bool expand_compact(const std::map<std::uint32_t, EdsObject>& objects, std::size_t max_objects,
                    std::size_t line_number, std::vector<EdsObject>& expanded,
                    std::vector<EdsDiagnostic>& diagnostics) {
    for (const auto& [key, object] : objects) {
        static_cast<void>(key);
        expanded.push_back(object);
        const std::uint16_t count =
            object.sub_number != 0U ? object.sub_number : object.compact_sub_obj;
        if (object.compact_sub_obj == 0U || count == 0U) {
            continue;
        }
        for (std::uint16_t sub = 0U; sub < count; ++sub) {
            const std::uint32_t sub_key = (static_cast<std::uint32_t>(object.index) << 8U) | sub;
            if (objects.find(sub_key) != objects.end()) {
                continue;
            }
            EdsObject copy = object;
            copy.subindex = static_cast<std::uint8_t>(sub & 0xFFU);
            copy.name = object.name + "_" + std::to_string(sub);
            copy.has_default = false;
            copy.has_cob_id = false;
            expanded.push_back(std::move(copy));
            if (expanded.size() >= max_objects) {
                append_diagnostic(diagnostics, line_number, "object limit exceeded");
                return false;
            }
        }
    }
    return true;
}

} // namespace

std::string EdsDiagnostic::to_string() const {
    if (line == 0U) {
        return message;
    }
    return "line " + std::to_string(line) + ": " + message;
}

std::uint64_t eds_content_hash(std::string_view content) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const char character : content) {
        hash ^= static_cast<std::uint8_t>(character);
        hash *= 1099511628211ULL;
    }
    return hash;
}

EdsParseResult EdsParser::parse(std::string_view content, std::string_view source,
                                const EdsLimits& limits) {
    EdsParseResult result{};
    if (content.size() > limits.max_file_bytes) {
        append_diagnostic(result.diagnostics, 0U, "file exceeds the configured size limit");
        return result;
    }
    auto dictionary = std::make_unique<EdsDictionary>();
    dictionary->set_source_path(std::string(source));
    dictionary->set_content_hash(eds_content_hash(content));
    dictionary->set_source_size(content.size());

    std::map<std::uint32_t, EdsObject> objects;
    std::uint16_t current_index = 0U;
    std::uint8_t current_sub = 0U;
    bool have_section = false;
    bool too_many_objects = false;
    std::size_t line_number = 0U;
    std::size_t position = 0U;

    while (position <= content.size() && !too_many_objects) {
        const std::size_t end = content.find('\n', position);
        const std::string_view raw = content.substr(
            position, end == std::string_view::npos ? content.size() - position : end - position);
        position = end == std::string_view::npos ? content.size() + 1U : end + 1U;
        ++line_number;
        process_line(raw, line_number, limits, objects, current_index, current_sub, have_section,
                     too_many_objects, result.diagnostics);
    }

    if (too_many_objects) {
        append_diagnostic(result.diagnostics, line_number, "object limit exceeded");
        return result;
    }

    std::vector<EdsObject> expanded;
    expanded.reserve(objects.size());
    if (!expand_compact(objects, limits.max_objects, line_number, expanded, result.diagnostics)) {
        return result;
    }
    for (auto& object : expanded) {
        dictionary->add_object(std::move(object));
    }
    dictionary->finalize();
    result.dictionary = std::move(dictionary);
    return result;
}

EdsParseResult EdsParser::load(const std::string& path, const EdsLimits& limits) {
    EdsParseResult result{};
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        append_diagnostic(result.diagnostics, 0U, "cannot open '" + path + "'");
        return result;
    }
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size < 0 || static_cast<std::uint64_t>(size) > limits.max_file_bytes) {
        append_diagnostic(result.diagnostics, 0U, "file exceeds the configured size limit");
        return result;
    }
    file.seekg(0, std::ios::beg);
    std::string content(static_cast<std::size_t>(size), '\0');
    if (!file.read(content.data(), size)) {
        append_diagnostic(result.diagnostics, 0U, "cannot read '" + path + "'");
        return result;
    }
    return parse(content, path, limits);
}

} // namespace nexigen::canview
