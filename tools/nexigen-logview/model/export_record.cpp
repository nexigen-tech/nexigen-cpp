#include "export_record.hpp"

#include <format>

namespace nexigen::logview {

std::string ExportRecord::to_string() const {
    return std::format("ExportRecord(seq={}, kind={}, frame={})", seq, kind, frame.to_string());
}

} // namespace nexigen::logview
