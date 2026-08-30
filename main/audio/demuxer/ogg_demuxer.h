#ifndef OGG_DEMUXER_H_
#define OGG_DEMUXER_H_

#include <cstdint>
#include <cstring>
#include <functional>
#include <utility>
#include <vector>

class OggDemuxer {
private:
    // Some official assets produced by libavcodec carry a small forward
    // granule discontinuity on a non-final page. Keep accepting up to one
    // maximum-duration Opus packet while still rejecting unbounded jumps.
    static constexpr uint64_t kMaxGranuleForwardGapSamples = 5760;

#ifdef OGG_DEMUXER_TESTING
    friend struct OggDemuxerTestPeer;
#endif

    enum ParseState : int8_t { FIND_PAGE, PARSE_HEADER, PARSE_SEGMENTS, PARSE_DATA };

    enum class OpusStreamStage : uint8_t {
        kExpectHead,
        kExpectTags,
        kAudio,
    };

    struct Opus_t {
        bool head_seen{false};
        bool tags_seen{false};
        bool mono{false};
        int sample_rate{48000};
        uint16_t pre_skip{0};
    };

    struct PendingPacket {
        std::vector<uint8_t> payload;
        int sample_rate = 48000;
        int frame_duration_ms = 0;
    };

    // Use fixed-size buffers to avoid dynamic allocation.
    struct context_t {
        bool packet_continued{false};  // Whether the current packet spans segments
        uint8_t header[27];            // Ogg page header
        uint8_t seg_table[255];        // Current segment table
        uint8_t packet_buf[2048];      // 2 KB packet buffer
        size_t packet_len = 0;         // Bytes accumulated in the packet buffer
        size_t seg_count = 0;          // Segment count in the current page
        size_t seg_index = 0;          // Current segment index
        size_t data_offset = 0;        // Bytes read in the current parsing stage
        size_t bytes_needed = 0;       // Bytes still needed for the current field
        size_t seg_remaining = 0;      // Bytes remaining in the current segment
        size_t body_size = 0;          // Total page body size
        size_t body_offset = 0;        // Bytes read from the page body
        uint32_t expected_crc = 0;
        uint32_t calculated_crc = 0;
        bool page_is_eos = false;
        uint8_t page_flags = 0;
        uint32_t page_sequence = 0;
        uint64_t granule_position = 0;
        size_t page_completed_packets = 0;
        uint64_t page_audio_samples = 0;
        OpusStreamStage page_start_stage = OpusStreamStage::kExpectHead;
        bool tags_completed_on_page = false;
    };

public:
    OggDemuxer() { Reset(); }

    void Reset();

    size_t Process(const uint8_t* data, size_t size);

    bool Finish() const;
    bool HasError() const { return has_error_; }

    void OnPacket(
        std::function<void(const uint8_t* data, int sample_rate, int frame_duration_ms, size_t len)>
            on_packet) {
        on_packet_ = std::move(on_packet);
    }

private:
    ParseState state_ = ParseState::FIND_PAGE;
    context_t ctx_;
    Opus_t opus_info_;
    bool has_error_ = false;
    bool stream_serial_set_ = false;
    bool eos_seen_ = false;
    bool comment_started_ = false;
    bool audio_granule_set_ = false;
    uint32_t stream_serial_ = 0;
    uint32_t expected_page_sequence_ = 0;
    uint64_t last_audio_granule_ = 0;
    size_t packet_count_ = 0;
    OpusStreamStage stream_stage_ = OpusStreamStage::kExpectHead;
    std::vector<PendingPacket> pending_page_packets_;
    std::function<void(const uint8_t*, int, int, size_t)> on_packet_;

    static int GetOpusPacketDurationMs(const uint8_t* data, size_t size);
    static bool ValidateOpusHead(const uint8_t* data, size_t size, Opus_t& opus_info);
    static bool ValidateOpusTags(const uint8_t* data, size_t size);
    static bool ReadOpusFrameLength(const uint8_t* data, size_t size, size_t& offset,
                                    size_t& frame_length);
    static uint32_t UpdateCrc(uint32_t crc, const uint8_t* data, size_t size);
    bool HandleCompletedPacket();
    bool FinishPage();
};

#endif
