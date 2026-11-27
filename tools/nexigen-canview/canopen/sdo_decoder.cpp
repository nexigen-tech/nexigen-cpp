#include "sdo_decoder.hpp"

#include <cstdio>

namespace nexigen::canview {

namespace {

const char* abort_name(std::uint32_t code) noexcept {
    switch (code) {
        case 0x05030000U: return "toggle bit not alternated";
        case 0x05040000U: return "SDO protocol timeout";
        case 0x06010000U: return "unsupported access";
        case 0x06020000U: return "object does not exist";
        case 0x06040041U: return "object not mappable";
        case 0x06040042U: return "PDO length exceeded";
        case 0x06090011U: return "subindex does not exist";
        case 0x060A0023U: return "resource not available";
        case 0x08000020U: return "data cannot be transferred";
        case 0x08000022U: return "data cannot be transferred in this state";
        default:          return "unknown abort";
    }
}

void render_upload_response(const canopen::SdoEvent& event, const EdsDictionary* dictionary,
                            CoDecoded& out) {
    if (event.segmented) {
        out.status = CoDecodeStatus::Ok;
        out.set_text("SDO segmented upload response");
        return;
    }
    const EdsObject* object =
        dictionary != nullptr ? dictionary->find(event.index, event.subindex) : nullptr;
    CoSignal signal{};
    signal.name = object != nullptr ? object->name.c_str() : "value";
    signal.kind = EdsDataKind::Unsigned;
    signal.bits = static_cast<std::uint8_t>(event.value_bytes * 8U);
    signal.valid = true;
    signal.raw = event.value;
    std::snprintf(signal.text.data(), signal.text.size(), "0x%x", event.value);
    static_cast<void>(out.add_signal(signal));
    char text[96]{};
    std::snprintf(text, sizeof(text), "SDO read %s0x%04x:%u = 0x%x",
                  object != nullptr ? (object->name + " ").c_str() : "", event.index,
                  event.subindex, event.value);
    out.status = CoDecodeStatus::Ok;
    out.set_text(text);
}

void render_abort(const canopen::SdoEvent& event, CoDecoded& out) {
    out.status = CoDecodeStatus::Aborted;
    out.error = true;
    out.abort_code = event.abort_code;
    char text[128]{};
    std::snprintf(text, sizeof(text), "SDO abort 0x%08x: %s", event.abort_code,
                  abort_name(event.abort_code));
    out.set_text(text);
}

void render_completed_write(const canopen::SdoEvent& event, CoDecoded& out) {
    out.status = CoDecodeStatus::Ok;
    char text[96]{};
    std::snprintf(text, sizeof(text), "SDO write 0x%04x:%u (%zu bytes)", event.index,
                  event.subindex, event.size);
    out.set_text(text);
}

void apply_event(const canopen::SdoEvent& event, const EdsDictionary* dictionary, CoDecoded& out) {
    out.index = event.index;
    out.subindex = event.subindex;
    switch (event.kind) {
        case canopen::SdoEventKind::UploadRequest:
            out.status = CoDecodeStatus::Ok;
            out.set_text("SDO upload request");
            return;
        case canopen::SdoEventKind::ExpeditedWriteRequest:
            out.status = CoDecodeStatus::Ok;
            out.set_text("SDO expedited write request");
            return;
        case canopen::SdoEventKind::SegmentedDownloadRequest:
            out.status = CoDecodeStatus::Ok;
            out.set_text("SDO segmented download request");
            return;
        case canopen::SdoEventKind::UploadResponse:
            render_upload_response(event, dictionary, out);
            return;
        case canopen::SdoEventKind::DownloadResponse: return;
        case canopen::SdoEventKind::DownloadSegment:
            out.status = CoDecodeStatus::Ok;
            out.set_text("SDO segmented download segment");
            return;
        case canopen::SdoEventKind::UploadSegment:
            out.status = CoDecodeStatus::Ok;
            out.set_text(event.last ? "SDO segmented upload complete"
                                    : "SDO segmented upload segment");
            return;
        case canopen::SdoEventKind::CompletedWrite: render_completed_write(event, out); return;
        case canopen::SdoEventKind::Abort:          render_abort(event, out); return;
        case canopen::SdoEventKind::TooLarge:
            out.status = CoDecodeStatus::TooLarge;
            out.set_text("SDO segmented transfer exceeds 4096 bytes");
            return;
        case canopen::SdoEventKind::None:
        default:                          return;
    }
}

} // namespace

bool SdoDecoder::feed(const nexigen::hw::can::CanMessage& message, bool response, CoDecoded& out,
                      std::uint64_t now_ns, const EdsDictionary* dictionary) {
    canopen::SdoEvent event{};
    if (!assembler_.feed(message, response, now_ns, event)) {
        return false;
    }
    apply_event(event, dictionary, out);
    return true;
}

std::optional<SdoCompletedWrite> SdoDecoder::take_completed() {
    return assembler_.take_completed();
}

void SdoDecoder::expire(std::uint64_t now_ns) noexcept {
    assembler_.expire(now_ns);
}

void SdoDecoder::reset() noexcept {
    assembler_.reset();
}

std::size_t SdoDecoder::active_assemblies() const noexcept {
    return assembler_.active_assemblies();
}

} // namespace nexigen::canview
