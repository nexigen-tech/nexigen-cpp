#include <nexigen/canopen/eds_access.hpp>

namespace nexigen::canopen {

EdsAccess parse_eds_access(std::string_view text) noexcept {
    if (text == "ro" || text == "RO") {
        return EdsAccess::ReadOnly;
    }
    if (text == "wo" || text == "WO") {
        return EdsAccess::WriteOnly;
    }
    if (text == "rw" || text == "RW") {
        return EdsAccess::ReadWrite;
    }
    if (text == "rwr" || text == "RWR") {
        return EdsAccess::ReadWriteInput;
    }
    if (text == "rww" || text == "RWW") {
        return EdsAccess::ReadWriteOutput;
    }
    if (text == "const" || text == "CONST") {
        return EdsAccess::Constant;
    }
    return EdsAccess::Unknown;
}

} // namespace nexigen::canopen
