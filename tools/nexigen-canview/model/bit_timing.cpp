#include "bit_timing.hpp"

namespace nexigen::canview {

namespace {

namespace hwcan = nexigen::hw::can;

constexpr std::uint64_t stuffing_numerator{115U}; ///< 1.15 stuffing factor.
constexpr std::uint64_t stuffing_denominator{100U};

std::uint64_t scale_ns(std::uint64_t bits, std::uint32_t bitrate) noexcept {
    if (bitrate == 0U) {
        return 0U;
    }
    return (bits * 1'000'000'000ULL) / bitrate;
}

} // namespace

FrameTiming estimate_frame_timing(const hwcan::CanMessage& message,
                                  const ChannelTiming& timing) noexcept {
    FrameTiming result{};
    if (timing.nominal_bitrate == 0U || message.kind == hwcan::CanMessageKind::BusIdle) {
        return result;
    }
    result.available = true;
    if (message.kind == hwcan::CanMessageKind::Error ||
        message.kind == hwcan::CanMessageKind::Overload) {
        result.bits = 14U;
        result.time_ns = scale_ns(result.bits, timing.nominal_bitrate);
        return result;
    }

    const std::uint64_t identifier_bits = message.extended ? 29U : 11U;
    const std::uint64_t control_bits = 1U + identifier_bits + 1U + 1U + 1U + 4U; // SOF..DLC
    const std::uint64_t data_bits = static_cast<std::uint64_t>(message.dlc) * 8U;
    const bool fd_frame = message.kind == hwcan::CanMessageKind::FdData;
    const std::uint64_t crc_bits = fd_frame ? 21U : 17U;
    const std::uint64_t trailer_bits = 1U + 1U + 2U + 7U + 3U; // CRC delim, ACK, EOF, IFS
    const std::uint64_t nominal_bits = control_bits + data_bits + crc_bits + trailer_bits;

    if (!fd_frame) {
        const std::uint64_t stuffed = (nominal_bits * stuffing_numerator) / stuffing_denominator;
        result.bits = stuffed;
        result.time_ns = scale_ns(stuffed, timing.nominal_bitrate);
        return result;
    }

    const bool brs = message.frame_flags.has(hwcan::CanFrameFlags::bit_rate_switch);
    const std::uint32_t data_rate =
        brs && timing.data_bitrate != 0U ? timing.data_bitrate : timing.nominal_bitrate;
    const std::uint64_t nominal_part = control_bits + 2U; // control + BRS/ESI
    const std::uint64_t data_part = data_bits + crc_bits;
    const std::uint64_t stuffed_data = (data_part * stuffing_numerator) / stuffing_denominator;
    result.bits = nominal_part + stuffed_data + trailer_bits;
    result.time_ns = scale_ns(nominal_part, timing.nominal_bitrate) +
                     scale_ns(stuffed_data + trailer_bits, data_rate);
    return result;
}

} // namespace nexigen::canview
