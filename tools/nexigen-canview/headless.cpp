#include "headless.hpp"

#include <cstdio>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <span>
#include <thread>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "budget.hpp"
#include "can_record_ring.hpp"
#include "record_meta.hpp"
#include "session_export.hpp"
#include "session_import.hpp"
#include "session_record.hpp"
#include "session_replay.hpp"

namespace nexigen::canview {

namespace {

class MockGenerator {
public:
    MockGenerator(std::span<const CanViewChannel> channels, std::uint64_t rate)
        : channels_(channels), rate_(rate == 0U ? 50'000U : rate) {}

    void generate(CanRecordRing& ring, std::uint64_t count) {
        for (std::uint64_t index = 0U; index < count; ++index) {
            emit_one(ring, index);
        }
    }

    void generate_paced(CanRecordRing& ring, std::uint64_t count) {
        const auto start = std::chrono::steady_clock::now();
        for (std::uint64_t index = 0U; index < count; ++index) {
            emit_one(ring, index);
            if ((index + 1U) % 1000U == 0U) {
                const auto target =
                    start + std::chrono::nanoseconds(((index + 1U) * 1'000'000'000ULL) / rate_);
                std::this_thread::sleep_until(target);
            }
        }
    }

private:
    void emit_one(CanRecordRing& ring, std::uint64_t index) {
        nexigen::hw::can::CanMessage message{};
        const CanViewChannel* channel = channel_for(index);
        message.channel_id =
            channel != nullptr ? channel->channel_id : static_cast<std::uint16_t>(1U + index % 4U);
        message.can_id = static_cast<std::uint32_t>(0x100U + index % 8U);
        message.dlc = 8U;
        message.hardware_timestamp_ns = 1'000'000'000ULL + (index * 1'000'000'000ULL) / rate_;
        message.hardware_sequence = index;
        message.frame_flags.bits = nexigen::hw::can::CanFrameFlags::has_hardware_timestamp |
                                   nexigen::hw::can::CanFrameFlags::has_hardware_sequence;
        for (std::size_t byte = 0U; byte < message.data.size(); ++byte) {
            message.data[byte] = static_cast<std::uint8_t>((index + byte) & 0xFFU);
        }
        const std::uint8_t meta = pack_frame_meta(CanRecordKind::Frame);
        static_cast<void>(ring.push(message, static_cast<std::int64_t>(index),
                                    CanRecordView::no_node, meta, 0U, 0U));
    }

private:
    [[nodiscard]] const CanViewChannel* channel_for(std::uint64_t index) const noexcept {
        if (channels_.empty()) {
            return nullptr;
        }
        return &channels_[index % channels_.size()];
    }

    std::span<const CanViewChannel> channels_{};
    std::uint64_t rate_{50'000U};
};

std::string_view channel_name(const CanViewConfig& config, std::uint16_t channel_id) {
    for (const auto& channel : config.channels) {
        if (channel.channel_id == channel_id) {
            return channel.name;
        }
    }
    return {};
}

void print_json(const CanViewConfig& config, const CanRecordRing& ring, std::uint64_t max_records) {
    CanViewSessionHeader header{};
    header.boot_token = 1U;
    header.channels = config.channels;
    header.frame_count = ring.size();
    std::cout << nlohmann::json(header).dump() << '\n';
    std::uint64_t printed = 0U;
    for (std::uint64_t seq = ring.head(); seq < ring.tail(); ++seq) {
        if (max_records != 0U && printed >= max_records) {
            break;
        }
        const auto record = ring.find(seq);
        if (!record.has_value()) {
            continue;
        }
        nlohmann::json line;
        if (record->is_frame()) {
            const auto frame = session_frame_from_view(
                *record, channel_name(config, record->message->channel_id), seq + 1U);
            to_json(line, frame);
        } else {
            const auto gap = session_gap_from_view(
                *record, channel_name(config, record->message->channel_id), seq + 1U);
            to_json(line, gap);
        }
        std::cout << line.dump() << '\n';
        ++printed;
    }
}

void print_text(const CanViewConfig& config, const CanRecordRing& ring, std::uint64_t max_records) {
    std::uint64_t printed = 0U;
    for (std::uint64_t seq = ring.head(); seq < ring.tail(); ++seq) {
        if (max_records != 0U && printed >= max_records) {
            break;
        }
        const auto record = ring.find(seq);
        if (!record.has_value() || record->message == nullptr) {
            continue;
        }
        const auto& message = *record->message;
        const std::string name{channel_name(config, message.channel_id)};
        if (record->is_frame()) {
            std::cout << seq << " | " << message.hardware_timestamp_ns << " | "
                      << message.channel_id << (name.empty() ? "" : " " + name) << " | 0x"
                      << std::hex << message.can_id << std::dec << " | "
                      << static_cast<unsigned>(message.dlc) << " | " << payload_to_hex(message)
                      << '\n';
        } else {
            std::cout << seq << " | gap | " << to_string(record->kind()) << " | "
                      << record->gap_count() << '\n';
        }
        ++printed;
    }
}

} // namespace

int run_headless(const CliOptions& options, const CanViewConfig& config) {
    const std::size_t capacity = capacity_for_budget(config.memory_mb);
    auto ring = CanRecordRing::create(capacity);
    if (!ring) {
        std::fprintf(stderr, "canview: history allocation failed\n");
        return 1;
    }
    if (options.offline) {
        const auto imported = SessionImporter::load(options.offline_file);
        if (imported.malformed > 0U && imported.records.empty()) {
            std::fprintf(stderr, "canview: cannot open '%s'\n", options.offline_file.c_str());
            return 1;
        }
        const auto report = replay_into_ring(imported, *ring);
        if (!options.json) {
            std::fprintf(stderr, "canview: offline frames=%zu gaps=%zu statuses=%zu\n",
                         report.frames, report.gaps, report.statuses);
        }
    } else if (options.soak_seconds > 0U) {
        const std::uint64_t total = options.soak_seconds * options.mock_rate;
        MockGenerator generator{config.channels, options.mock_rate};
        generator.generate_paced(*ring, total);
        std::fprintf(stderr, "canview: soak frames=%llu rate=%llu evictions=%llu overflow=0\n",
                     static_cast<unsigned long long>(total),
                     static_cast<unsigned long long>(options.mock_rate),
                     static_cast<unsigned long long>(ring->evictions()));
    } else if (options.mock_records > 0U) {
        MockGenerator generator{config.channels, options.mock_rate};
        generator.generate(*ring, options.mock_records);
    } else {
        std::fprintf(stderr, "canview: no input (use --offline, --mock or --soak)\n");
        return 2;
    }
    if (options.json) {
        print_json(config, *ring, options.max_records);
    } else {
        print_text(config, *ring, options.max_records);
    }
    return 0;
}

} // namespace nexigen::canview
