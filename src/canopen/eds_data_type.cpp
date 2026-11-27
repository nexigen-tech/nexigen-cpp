#include <nexigen/canopen/eds_data_type_info.hpp>

#include <algorithm>
#include <array>

namespace nexigen::canopen {

namespace {

constexpr std::array<EdsDataTypeInfo, 26> kDataTypes{{
    {0x0001U, "BOOLEAN", 1U, EdsDataKind::Boolean, false},
    {0x0002U, "INTEGER8", 8U, EdsDataKind::Integer, true},
    {0x0003U, "INTEGER16", 16U, EdsDataKind::Integer, true},
    {0x0004U, "INTEGER32", 32U, EdsDataKind::Integer, true},
    {0x0005U, "UNSIGNED8", 8U, EdsDataKind::Unsigned, false},
    {0x0006U, "UNSIGNED16", 16U, EdsDataKind::Unsigned, false},
    {0x0007U, "UNSIGNED32", 32U, EdsDataKind::Unsigned, false},
    {0x0008U, "REAL32", 32U, EdsDataKind::Real, false},
    {0x0009U, "VISIBLE_STRING", 0U, EdsDataKind::String, false},
    {0x000AU, "OCTET_STRING", 0U, EdsDataKind::String, false},
    {0x000BU, "UNICODE_STRING", 0U, EdsDataKind::String, false},
    {0x000CU, "TIME_OF_DAY", 48U, EdsDataKind::Time, false},
    {0x000DU, "TIME_DIFFERENCE", 48U, EdsDataKind::Time, false},
    {0x000FU, "DOMAIN", 0U, EdsDataKind::Domain, false},
    {0x0010U, "INTEGER24", 24U, EdsDataKind::Integer, true},
    {0x0011U, "REAL64", 64U, EdsDataKind::Real, false},
    {0x0012U, "INTEGER40", 40U, EdsDataKind::Integer, true},
    {0x0013U, "INTEGER48", 48U, EdsDataKind::Integer, true},
    {0x0014U, "INTEGER56", 56U, EdsDataKind::Integer, true},
    {0x0015U, "INTEGER64", 64U, EdsDataKind::Integer, true},
    {0x0016U, "UNSIGNED24", 24U, EdsDataKind::Unsigned, false},
    {0x0018U, "UNSIGNED40", 40U, EdsDataKind::Unsigned, false},
    {0x0019U, "UNSIGNED48", 48U, EdsDataKind::Unsigned, false},
    {0x001AU, "UNSIGNED56", 56U, EdsDataKind::Unsigned, false},
    {0x001BU, "UNSIGNED64", 64U, EdsDataKind::Unsigned, false},
    {0xFFFFU, "Unknown", 0U, EdsDataKind::Unknown, false},
}};

} // namespace

const EdsDataTypeInfo& eds_data_type(std::uint16_t code) noexcept {
    const auto it =
        std::find_if(kDataTypes.begin(), kDataTypes.end(),
                     [code](const EdsDataTypeInfo& entry) { return entry.code == code; });
    return it == kDataTypes.end() ? kDataTypes.back() : *it;
}

} // namespace nexigen::canopen
