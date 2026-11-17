# Specification: `media/video-encoder`

## Metadata
- **Module:** `media` (CMake target: `nexigen_media`, `STATIC`, **gate**)
- **Layer:** L2 (depends on `core`, `chrono`)
- **Build Gate:** Controlled by `NEXIGEN_ENABLE_MEDIA=ON` (default `OFF`)
- **External Dependencies:** System `libavcodec`, `libavutil`, `libavformat` (FFmpeg C API, linked privately)
- **Status:** Draft (Proposal)

## Overview
The `media` module provides a thin, zero-overhead C++23 RAII wrapper around FFmpeg's C libraries (`libavcodec` / `libavformat`) for **All-Intra** video encoding and zero-reencode container remuxing. Following the same architectural boundary as `nexigen_com`, all third-party C headers and types (`AVCodecContext`, `AVFrame`, `AVPacket`) are strictly encapsulated within private implementation files (`detail/`) and never leak into public `nexigen::media` headers.

---

## Requirements

### MED-001: Strict Dependency Isolation & Build Gating
The module MUST be completely optional and isolate FFmpeg C headers from downstream targets (`recorder`, `nexigen-recordd`, `nexigen-export`).

* **Scenario MED-001.1: Build gate disabled (`NEXIGEN_ENABLE_MEDIA=OFF`)**
  * **Given** a CMake configuration with `-DNEXIGEN_ENABLE_MEDIA=OFF`
  * **When** the project builds
  * **Then** `nexigen_media` is not built, FFmpeg libraries are not searched or linked, and `nexigen-recordd` / `nexigen-export` compile cleanly with pass-through and raw storage support only.
* **Scenario MED-001.2: Header boundary enforcement**
  * **Given** any public header under `include/nexigen/media/`
  * **When** included by a downstream translation unit
  * **Then** no `<libavcodec/...>` or `<libavutil/...>` headers are transitively included; internal FFmpeg state is held via opaque PIMPL pointers or aligned storage.

### MED-002: Hardware-Agnostic All-Intra Video Encoder
`media::VideoEncoder` MUST support intra-frame-only (`GOP = 1`) compression for RAW video frames across heterogeneous hardware platforms and CPU fallback.

* **Scenario MED-002.1: Multi-backend initialization**
  * **Given** an `EncoderConfig` specifying `Codec` (`H265` | `AV1`), `Backend` (`Auto` | `Nvenc` | `Vaapi` | `Qsv` | `Rkmpp` | `Cpu`), resolution, pixel format (`RGB24`, `BGR24`, `NV12`, `YUV420P`), and quality (`cqp` / `bitrate`)
  * **When** `media::VideoEncoder::create(config)` is called
  * **Then** it probes and initializes the corresponding `libavcodec` encoder (`hevc_nvenc`/`av1_nvenc`, `hevc_vaapi`/`av1_vaapi`, `hevc_qsv`/`av1_qsv`, `hevc_rkmpp`, or `libx265`/`libsvtav1`), enforces `gop_size = 1` and `max_b_frames = 0`, and returns `core::Result<VideoEncoder, core::ErrorCode>`.
* **Scenario MED-002.2: Automatic hardware fallback (`Backend::Auto`)**
  * **Given** `Backend::Auto` is configured
  * **When** `VideoEncoder::create` probes available devices
  * **Then** it attempts hardware backends in deterministic priority order (`Nvenc` -> `Rkmpp` -> `Vaapi` -> `Qsv`), falling back to `Cpu` (`libsvtav1` / `libx265` with `ultrafast` / `zerolatency` tuning) only if `allow_cpu_fallback == true`, or failing with `core::ErrorCode::DeviceUnavailable` otherwise.
* **Scenario MED-002.3: Synchronous frame encoding without heap churn**
  * **Given** an initialized `VideoEncoder` and a pre-allocated output buffer (`std::span<std::byte>`)
  * **When** `encoder.encode(RawFrameView, out_buffer)` is invoked on a worker thread
  * **Then** it wraps the input pointer in a reusable `AVFrame` without copying pixel data, submits it to the encoder, writes the resulting compressed I-frame packet into `out_buffer`, and returns the exact byte count (`core::Result<std::size_t, core::ErrorCode>`).

### MED-003: Zero-Copy DMA-BUF Hardware Ingestion
When hardware acceleration (`Vaapi`, `Rkmpp`, `Nvenc`) is active, the encoder MUST accept Linux `dma_buf` file descriptors to avoid CPU-to-GPU/VPU memory copies.

* **Scenario MED-003.1: Importing `dma_buf` frame descriptors**
  * **Given** a `RawFrameView` carrying a valid `dmabuf_fd >= 0`, stride, and DRM format modifier
  * **When** passed to a DRM/VAAPI-capable `VideoEncoder`
  * **Then** the encoder maps the descriptor as `AV_PIX_FMT_DRM_PRIME` / `AV_PIX_FMT_VAAPI` directly into the hardware surface pool without `mmap` or `memcpy` on the CPU.

### MED-004: Zero-Reencode Container Muxer (`.mp4` / `.mkv`)
`media::ContainerMuxer` MUST encapsulate pre-compressed packets (`H.264`, `H.265`, `AV1`, `JPEG XS`) and `RAW` frames into `.mp4` or `.mkv` containers via `libavformat` without decoding or re-encoding.

* **Scenario MED-004.1: Streaming packet muxing**
  * **Given** a `MuxerConfig` specifying output path, container format (`Mp4` | `Mkv`), codec ID, dimensions, and timebase (`1/1'000'000'000` ns)
  * **When** `muxer.write_packet(EncodedPacketView)` is called sequentially with compressed payloads from `.nxvid`
  * **Then** it constructs an `AVPacket` referencing the input `std::span<const std::byte>`, rescales `PTS`/`DTS` from `chrono::Timestamp` deltas, and writes interleaved container frames via `av_interleaved_write_frame`.
* **Scenario MED-004.2: Codec-to-container validation**
  * **Given** `ContainerFormat::Mp4` is requested for `Codec::Raw` or unsupported bitstream formats
  * **When** `ContainerMuxer::create(config)` is called
  * **Then** it fails immediately before opening files and returns `core::ErrorCode::UnsupportedFormat`.

### MED-005: C++23 & `nexigen-cpp` Mandate Compliance
All code in `nexigen_media` MUST comply with global compiler and serialization rules.

* **Scenario MED-005.1: `-fno-exceptions -fno-rtti` and FFmpeg error mapping**
  * **Given** global compilation with `-fno-exceptions -fno-rtti -Werror`
  * **When** any `avcodec_*` or `avformat_*` call returns a negative `AVERROR` code
  * **Then** it is translated deterministically into `core::ErrorCode` (e.g., `AVERROR(EAGAIN)` -> `WouldBlock`, `AVERROR(ENOMEM)` -> `OutOfMemory`, `AVERROR_ENCODER_NOT_FOUND` -> `NotFound`) without throwing exceptions.
* **Scenario MED-005.2: Public type serialization**
  * **Given** public enums (`Codec`, `Backend`, `PixelFormat`, `ContainerFormat`) and structs (`EncoderConfig`, `MuxerConfig`, `EncoderStats`)
  * **When** used in configs, logs, or tests
  * **Then** enums implement `constexpr to_string` / `from_string` + `std::formatter`, and structs implement `to_string()` + ADL `to_json` / `from_json` (`JSON_NOEXCEPTION=1`).
