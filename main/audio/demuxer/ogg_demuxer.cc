#include "ogg_demuxer.h"

#include <algorithm>

#include "esp_log.h"

#define TAG "OggDemuxer"

/// @brief Reset the demuxer.
void OggDemuxer::Reset() {
    opus_info_ = {
        .head_seen = false, .tags_seen = false, .mono = false, .sample_rate = 48000, .pre_skip = 0};

    has_error_ = false;
    stream_serial_set_ = false;
    eos_seen_ = false;
    comment_started_ = false;
    audio_granule_set_ = false;
    stream_serial_ = 0;
    expected_page_sequence_ = 0;
    last_audio_granule_ = 0;
    packet_count_ = 0;
    stream_stage_ = OpusStreamStage::kExpectHead;
    pending_page_packets_.clear();

    state_ = ParseState::FIND_PAGE;
    ctx_.packet_len = 0;
    ctx_.seg_count = 0;
    ctx_.seg_index = 0;
    ctx_.data_offset = 0;
    ctx_.bytes_needed = 4;  // Four bytes are needed for "OggS"
    ctx_.seg_remaining = 0;
    ctx_.body_size = 0;
    ctx_.body_offset = 0;
    ctx_.packet_continued = false;
    ctx_.expected_crc = 0;
    ctx_.calculated_crc = 0;
    ctx_.page_is_eos = false;
    ctx_.page_flags = 0;
    ctx_.page_sequence = 0;
    ctx_.granule_position = 0;
    ctx_.page_completed_packets = 0;
    ctx_.page_audio_samples = 0;
    ctx_.page_start_stage = OpusStreamStage::kExpectHead;
    ctx_.tags_completed_on_page = false;

    // Clear buffered data.
    memset(ctx_.header, 0, sizeof(ctx_.header));
    memset(ctx_.seg_table, 0, sizeof(ctx_.seg_table));
    memset(ctx_.packet_buf, 0, sizeof(ctx_.packet_buf));
}

uint32_t OggDemuxer::UpdateCrc(uint32_t crc, const uint8_t* data, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        crc ^= static_cast<uint32_t>(data[i]) << 24;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80000000U) != 0 ? (crc << 1) ^ 0x04c11db7U : crc << 1;
        }
    }
    return crc;
}

bool OggDemuxer::FinishPage() {
    if (ctx_.calculated_crc != ctx_.expected_crc) {
        ESP_LOGE(TAG, "Ogg page CRC mismatch");
        has_error_ = true;
        return false;
    }
    // RFC 7845: OpusHead completes alone on the BOS page with granule 0.
    if (ctx_.page_sequence == 0 &&
        (ctx_.page_start_stage != OpusStreamStage::kExpectHead ||
         stream_stage_ != OpusStreamStage::kExpectTags || ctx_.page_completed_packets != 1 ||
         ctx_.packet_continued || ctx_.granule_position != 0)) {
        ESP_LOGE(TAG, "Invalid Opus identification header page");
        has_error_ = true;
        return false;
    }

    // OpusTags may span pages, but it must be the only packet completed on
    // its final page, and that page has granule 0.
    if (ctx_.tags_completed_on_page) {
        if (ctx_.page_start_stage != OpusStreamStage::kExpectTags ||
            ctx_.page_completed_packets != 1 || ctx_.packet_continued ||
            ctx_.granule_position != 0) {
            ESP_LOGE(TAG, "Invalid Opus comment header page");
            has_error_ = true;
            return false;
        }
    } else if (ctx_.page_start_stage == OpusStreamStage::kExpectTags) {
        if (!comment_started_ || !ctx_.packet_continued || ctx_.page_completed_packets != 0 ||
            ctx_.granule_position != UINT64_MAX) {
            ESP_LOGE(TAG, "Invalid continued Opus comment header page");
            has_error_ = true;
            return false;
        }
    } else if (ctx_.page_start_stage == OpusStreamStage::kAudio) {
        if (ctx_.page_completed_packets == 0) {
            if (ctx_.page_is_eos || ctx_.granule_position != UINT64_MAX) {
                ESP_LOGE(TAG, "Invalid audio page without a completed packet");
                has_error_ = true;
                return false;
            }
        } else {
            bool invalid_granule = ctx_.granule_position == UINT64_MAX;
            if (!invalid_granule && !audio_granule_set_) {
                // RFC 7845 Sections 4.4 and 4.5: the first audio page may
                // establish an arbitrary positive origin. A non-EOS first
                // page cannot trim samples, while a complete one-page stream
                // must still contain its declared pre-skip.
                invalid_granule =
                    (!ctx_.page_is_eos && ctx_.granule_position < ctx_.page_audio_samples) ||
                    (ctx_.page_is_eos && ctx_.granule_position < opus_info_.pre_skip);
            } else if (!invalid_granule) {
                const bool sample_sum_overflows =
                    ctx_.page_audio_samples > UINT64_MAX - last_audio_granule_;
                const uint64_t decoded_end = sample_sum_overflows
                                                 ? UINT64_MAX
                                                 : last_audio_granule_ + ctx_.page_audio_samples;
                const bool tolerance_overflows =
                    decoded_end > UINT64_MAX - kMaxGranuleForwardGapSamples;
                invalid_granule =
                    sample_sum_overflows || ctx_.granule_position < last_audio_granule_ ||
                    (ctx_.page_is_eos
                         ? (ctx_.granule_position > decoded_end ||
                            ctx_.granule_position < opus_info_.pre_skip)
                         : (ctx_.granule_position < decoded_end || tolerance_overflows ||
                            ctx_.granule_position > decoded_end + kMaxGranuleForwardGapSamples));
            }
            if (invalid_granule) {
                ESP_LOGE(TAG, "Invalid Opus audio granule position");
                has_error_ = true;
                return false;
            }
            last_audio_granule_ = ctx_.granule_position;
            audio_granule_set_ = true;
        }
    }

    if (ctx_.page_is_eos) {
        eos_seen_ = true;
    }

    // A packet callback may queue audio immediately. Hold packets only until
    // their containing page has passed CRC and structural validation so a
    // corrupt page can never leak audio. This remains page-streaming rather
    // than buffering the complete Ogg file.
    if (on_packet_) {
        for (const auto& packet : pending_page_packets_) {
            on_packet_(packet.payload.data(), packet.sample_rate, packet.frame_duration_ms,
                       packet.payload.size());
        }
    }
    pending_page_packets_.clear();
    return true;
}

bool OggDemuxer::ReadOpusFrameLength(const uint8_t* data, size_t size, size_t& offset,
                                     size_t& frame_length) {
    if (offset >= size) {
        return false;
    }
    const uint8_t first = data[offset++];
    if (first < 252) {
        frame_length = first;
        return true;
    }
    if (offset >= size) {
        return false;
    }
    frame_length = static_cast<size_t>(first) + 4U * data[offset++];
    return frame_length <= 1275;
}

int OggDemuxer::GetOpusPacketDurationMs(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        return -1;
    }

    const uint8_t toc = data[0];
    const uint8_t config = toc >> 3;
    int frame_duration_us = 0;
    if (config < 12) {
        static constexpr int kSilkDurationsUs[] = {10000, 20000, 40000, 60000};
        frame_duration_us = kSilkDurationsUs[config & 0x03];
    } else if (config < 16) {
        frame_duration_us = (config & 0x01) ? 20000 : 10000;
    } else {
        static constexpr int kCeltDurationsUs[] = {2500, 5000, 10000, 20000};
        frame_duration_us = kCeltDurationsUs[config & 0x03];
    }

    const uint8_t packet_code = toc & 0x03;
    int frame_count = 0;
    switch (packet_code) {
        case 0:
            frame_count = 1;
            if (size - 1 > 1275) {
                return -1;
            }
            break;
        case 1: {
            frame_count = 2;
            const size_t payload_size = size - 1;
            if ((payload_size & 1U) != 0 || payload_size / 2 > 1275) {
                return -1;
            }
            break;
        }
        case 2: {
            frame_count = 2;
            size_t offset = 1;
            size_t first_frame_size = 0;
            if (!ReadOpusFrameLength(data, size, offset, first_frame_size) ||
                first_frame_size > size - offset || size - offset - first_frame_size > 1275) {
                return -1;
            }
            break;
        }
        case 3: {
            if (size < 2) {
                return -1;
            }
            frame_count = data[1] & 0x3f;
            size_t offset = 2;
            size_t padding = 0;
            if ((data[1] & 0x40U) != 0) {
                uint8_t padding_byte = 0;
                do {
                    if (offset >= size) {
                        return -1;
                    }
                    padding_byte = data[offset++];
                    padding += padding_byte == 255 ? 254 : padding_byte;
                    if (padding > size - offset) {
                        return -1;
                    }
                } while (padding_byte == 255);
            }
            const size_t payload_end = size - padding;
            if ((data[1] & 0x80U) != 0) {
                size_t described_payload = 0;
                for (int frame = 0; frame + 1 < frame_count; ++frame) {
                    size_t frame_length = 0;
                    if (!ReadOpusFrameLength(data, payload_end, offset, frame_length) ||
                        described_payload > payload_end - offset ||
                        frame_length > payload_end - offset - described_payload) {
                        return -1;
                    }
                    described_payload += frame_length;
                }
                if (described_payload > payload_end - offset ||
                    payload_end - offset - described_payload > 1275) {
                    return -1;
                }
            } else {
                if (frame_count == 0 || (payload_end - offset) % frame_count != 0 ||
                    (payload_end - offset) / frame_count > 1275) {
                    return -1;
                }
            }
            break;
        }
    }

    const int packet_duration_us = frame_duration_us * frame_count;
    if (frame_count == 0 || packet_duration_us > 120000 || packet_duration_us % 1000 != 0) {
        return -1;
    }
    const int packet_duration_ms = packet_duration_us / 1000;
    switch (packet_duration_ms) {
        case 5:
        case 10:
        case 20:
        case 40:
        case 60:
        case 80:
        case 100:
        case 120:
            return packet_duration_ms;
        default:
            return -1;
    }
}

bool OggDemuxer::ValidateOpusHead(const uint8_t* data, size_t size, Opus_t& opus_info) {
    if (size != 19 || memcmp(data, "OpusHead", 8) != 0 || data[8] != 1 || data[9] != 1 ||
        data[18] != 0) {
        return false;
    }
    opus_info.head_seen = true;
    opus_info.mono = true;
    opus_info.pre_skip = static_cast<uint16_t>(data[10]) | (static_cast<uint16_t>(data[11]) << 8);
    const uint32_t input_sample_rate =
        static_cast<uint32_t>(data[12]) | (static_cast<uint32_t>(data[13]) << 8) |
        (static_cast<uint32_t>(data[14]) << 16) | (static_cast<uint32_t>(data[15]) << 24);
    switch (input_sample_rate) {
        case 8000:
        case 12000:
        case 16000:
        case 24000:
        case 48000:
            opus_info.sample_rate = input_sample_rate;
            break;
        default:
            opus_info.sample_rate = 48000;
            break;
    }
    return true;
}

bool OggDemuxer::ValidateOpusTags(const uint8_t* data, size_t size) {
    if (size < 16 || memcmp(data, "OpusTags", 8) != 0) {
        return false;
    }
    auto read_le32 = [](const uint8_t* value) {
        return static_cast<uint32_t>(value[0]) | (static_cast<uint32_t>(value[1]) << 8) |
               (static_cast<uint32_t>(value[2]) << 16) | (static_cast<uint32_t>(value[3]) << 24);
    };
    size_t offset = 12;
    const uint32_t vendor_length = read_le32(data + 8);
    if (vendor_length > size - offset) {
        return false;
    }
    offset += vendor_length;
    if (size - offset < 4) {
        return false;
    }
    const uint32_t comment_count = read_le32(data + offset);
    offset += 4;
    if (comment_count > (size - offset) / 4) {
        return false;
    }
    for (uint32_t comment = 0; comment < comment_count; ++comment) {
        if (size - offset < 4) {
            return false;
        }
        const uint32_t comment_length = read_le32(data + offset);
        offset += 4;
        if (comment_length > size - offset) {
            return false;
        }
        offset += comment_length;
    }
    return true;
}

bool OggDemuxer::HandleCompletedPacket() {
    switch (stream_stage_) {
        case OpusStreamStage::kExpectHead:
            if (!ValidateOpusHead(ctx_.packet_buf, ctx_.packet_len, opus_info_)) {
                ESP_LOGE(TAG, "Invalid or unsupported OpusHead packet");
                return false;
            }
            stream_stage_ = OpusStreamStage::kExpectTags;
            return true;
        case OpusStreamStage::kExpectTags:
            if (!ValidateOpusTags(ctx_.packet_buf, ctx_.packet_len)) {
                ESP_LOGE(TAG, "Invalid OpusTags packet");
                return false;
            }
            opus_info_.tags_seen = true;
            stream_stage_ = OpusStreamStage::kAudio;
            ctx_.tags_completed_on_page = true;
            return true;
        case OpusStreamStage::kAudio: {
            const int frame_duration_ms = GetOpusPacketDurationMs(ctx_.packet_buf, ctx_.packet_len);
            if (frame_duration_ms <= 0) {
                ESP_LOGE(TAG, "Invalid or unsupported Opus audio packet");
                return false;
            }
            ++packet_count_;
            ctx_.page_audio_samples += static_cast<uint64_t>(frame_duration_ms) * 48U;
            PendingPacket pending;
            pending.payload.assign(ctx_.packet_buf, ctx_.packet_buf + ctx_.packet_len);
            pending.sample_rate = opus_info_.sample_rate;
            pending.frame_duration_ms = frame_duration_ms;
            pending_page_packets_.push_back(std::move(pending));
            return true;
        }
    }
    return false;
}

bool OggDemuxer::Finish() const {
    return !has_error_ && stream_stage_ == OpusStreamStage::kAudio && opus_info_.head_seen &&
           opus_info_.tags_seen && opus_info_.mono && packet_count_ > 0 && eos_seen_ &&
           audio_granule_set_ && last_audio_granule_ >= opus_info_.pre_skip &&
           state_ == ParseState::FIND_PAGE && ctx_.bytes_needed == 4 && ctx_.packet_len == 0;
}

/// @brief Process an input block.
/// @param data Input data.
/// @param size Input size in bytes.
/// @return Number of bytes processed.
size_t OggDemuxer::Process(const uint8_t* data, size_t size) {
    size_t processed = 0;  // Number of bytes processed

    while (processed < size) {
        switch (state_) {
            case ParseState::FIND_PAGE: {
                // Find the "OggS" page capture pattern.
                if (ctx_.bytes_needed < 4) {
                    // Continue a partial "OggS" match across input blocks.
                    size_t to_copy = std::min(size - processed, ctx_.bytes_needed);
                    memcpy(ctx_.header + (4 - ctx_.bytes_needed), data + processed, to_copy);

                    processed += to_copy;
                    ctx_.bytes_needed -= to_copy;

                    if (ctx_.bytes_needed == 0) {
                        // Check whether the capture pattern matches "OggS".
                        if (memcmp(ctx_.header, "OggS", 4) == 0) {
                            state_ = ParseState::PARSE_HEADER;
                            ctx_.data_offset = 4;
                            ctx_.bytes_needed = 27 - 4;  // 23 more bytes complete the header
                        } else {
                            ESP_LOGE(TAG, "Invalid Ogg capture pattern");
                            has_error_ = true;
                            return processed;
                        }
                    } else {
                        // Wait for more data.
                        return processed;
                    }
                } else if (ctx_.bytes_needed == 4) {
                    size_t remaining = size - processed;
                    if (remaining >= 4) {
                        if (memcmp(data + processed, "OggS", 4) != 0) {
                            ESP_LOGE(TAG, "Invalid Ogg capture pattern");
                            has_error_ = true;
                            return processed;
                        }
                        memcpy(ctx_.header, data + processed, 4);
                        processed += 4;
                        state_ = ParseState::PARSE_HEADER;
                        ctx_.data_offset = 4;
                        ctx_.bytes_needed = 27 - 4;  // 23 more bytes are needed
                    } else {
                        memcpy(ctx_.header, data + processed, remaining);
                        ctx_.bytes_needed = 4 - remaining;
                        processed += remaining;
                        return processed;
                    }
                } else {
                    ESP_LOGE(TAG, "OggDemuxer run in error state: bytes_needed=%zu",
                             ctx_.bytes_needed);
                    has_error_ = true;
                    return processed;
                }
                break;
            }

            case ParseState::PARSE_HEADER: {
                size_t available = size - processed;

                if (available < ctx_.bytes_needed) {
                    // Copy the available bytes and wait for more data.
                    memcpy(ctx_.header + ctx_.data_offset, data + processed, available);

                    ctx_.data_offset += available;
                    ctx_.bytes_needed -= available;
                    processed += available;
                    return processed;
                } else {
                    // Complete the page header.
                    size_t to_copy = ctx_.bytes_needed;
                    memcpy(ctx_.header + ctx_.data_offset, data + processed, to_copy);

                    processed += to_copy;
                    ctx_.data_offset += to_copy;
                    ctx_.bytes_needed = 0;

                    // Validate the page header.
                    if (ctx_.header[4] != 0) {
                        ESP_LOGE(TAG, "Invalid Ogg version: %d", ctx_.header[4]);
                        has_error_ = true;
                        return processed;
                    }

                    const uint8_t flags = ctx_.header[5];
                    uint64_t granule_position = 0;
                    for (size_t byte = 0; byte < 8; ++byte) {
                        granule_position |= static_cast<uint64_t>(ctx_.header[6 + byte])
                                            << (byte * 8);
                    }
                    const uint32_t serial = static_cast<uint32_t>(ctx_.header[14]) |
                                            (static_cast<uint32_t>(ctx_.header[15]) << 8) |
                                            (static_cast<uint32_t>(ctx_.header[16]) << 16) |
                                            (static_cast<uint32_t>(ctx_.header[17]) << 24);
                    const uint32_t sequence = static_cast<uint32_t>(ctx_.header[18]) |
                                              (static_cast<uint32_t>(ctx_.header[19]) << 8) |
                                              (static_cast<uint32_t>(ctx_.header[20]) << 16) |
                                              (static_cast<uint32_t>(ctx_.header[21]) << 24);
                    if ((flags & ~0x07U) != 0 || eos_seen_ ||
                        (!stream_serial_set_ && ((flags & 0x02U) == 0 || sequence != 0)) ||
                        (stream_serial_set_ && ((flags & 0x02U) != 0 || serial != stream_serial_ ||
                                                sequence != expected_page_sequence_)) ||
                        (((flags & 0x01U) != 0) != ctx_.packet_continued)) {
                        ESP_LOGE(TAG, "Invalid Ogg stream/page sequence");
                        has_error_ = true;
                        return processed;
                    }
                    if (!stream_serial_set_) {
                        stream_serial_set_ = true;
                        stream_serial_ = serial;
                    }
                    if (stream_stage_ == OpusStreamStage::kExpectTags && !comment_started_ &&
                        sequence != 1) {
                        ESP_LOGE(TAG, "OpusTags did not start on the second page");
                        has_error_ = true;
                        return processed;
                    }
                    expected_page_sequence_ = sequence + 1;
                    ctx_.page_is_eos = (flags & 0x04U) != 0;
                    ctx_.page_flags = flags;
                    ctx_.page_sequence = sequence;
                    ctx_.granule_position = granule_position;
                    ctx_.page_completed_packets = 0;
                    ctx_.page_audio_samples = 0;
                    ctx_.page_start_stage = stream_stage_;
                    ctx_.tags_completed_on_page = false;
                    ctx_.expected_crc = static_cast<uint32_t>(ctx_.header[22]) |
                                        (static_cast<uint32_t>(ctx_.header[23]) << 8) |
                                        (static_cast<uint32_t>(ctx_.header[24]) << 16) |
                                        (static_cast<uint32_t>(ctx_.header[25]) << 24);
                    uint8_t crc_header[27];
                    memcpy(crc_header, ctx_.header, sizeof(crc_header));
                    memset(crc_header + 22, 0, 4);
                    ctx_.calculated_crc = UpdateCrc(0, crc_header, sizeof(crc_header));

                    ctx_.seg_count = ctx_.header[26];
                    if (ctx_.seg_count > 0 && ctx_.seg_count <= 255) {
                        state_ = ParseState::PARSE_SEGMENTS;
                        ctx_.bytes_needed = ctx_.seg_count;
                        ctx_.data_offset = 0;
                    } else if (ctx_.seg_count == 0) {
                        // Skip directly to the next page when there are no segments.
                        if (!FinishPage()) {
                            return processed;
                        }
                        state_ = ParseState::FIND_PAGE;
                        ctx_.bytes_needed = 4;
                        ctx_.data_offset = 0;
                    } else {
                        ESP_LOGE(TAG, "Invalid Ogg segment count: %u", ctx_.seg_count);
                        has_error_ = true;
                        return processed;
                    }
                }
                break;
            }

            case ParseState::PARSE_SEGMENTS: {
                size_t available = size - processed;

                if (available < ctx_.bytes_needed) {
                    memcpy(ctx_.seg_table + ctx_.data_offset, data + processed, available);
                    ctx_.calculated_crc =
                        UpdateCrc(ctx_.calculated_crc, data + processed, available);

                    ctx_.data_offset += available;
                    ctx_.bytes_needed -= available;
                    processed += available;
                    return processed;
                } else {
                    size_t to_copy = ctx_.bytes_needed;
                    memcpy(ctx_.seg_table + ctx_.data_offset, data + processed, to_copy);
                    ctx_.calculated_crc = UpdateCrc(ctx_.calculated_crc, data + processed, to_copy);

                    processed += to_copy;
                    ctx_.data_offset += to_copy;
                    ctx_.bytes_needed = 0;

                    state_ = ParseState::PARSE_DATA;
                    ctx_.seg_index = 0;
                    ctx_.data_offset = 0;

                    // Calculate the total page body size.
                    ctx_.body_size = 0;
                    for (size_t i = 0; i < ctx_.seg_count; ++i) {
                        ctx_.body_size += ctx_.seg_table[i];
                    }
                    ctx_.body_offset = 0;
                    ctx_.seg_remaining = 0;
                }
                break;
            }

            case ParseState::PARSE_DATA: {
                while (ctx_.seg_index < ctx_.seg_count) {
                    uint8_t seg_len = ctx_.seg_table[ctx_.seg_index];

                    // Continue a partially read segment.
                    if (ctx_.seg_remaining > 0) {
                        seg_len = ctx_.seg_remaining;
                    } else {
                        ctx_.seg_remaining = seg_len;
                    }

                    // A zero lacing value consumes no body bytes and may still need
                    // to terminate a packet after the caller's input is exhausted.
                    if (seg_len > 0 && processed == size) {
                        return processed;
                    }

                    // Check that the packet buffer has enough space.
                    if (ctx_.packet_len + seg_len > sizeof(ctx_.packet_buf)) {
                        ESP_LOGE(TAG, "Ogg packet buffer overflow: %zu + %u > %zu", ctx_.packet_len,
                                 seg_len, sizeof(ctx_.packet_buf));
                        has_error_ = true;
                        return processed;
                    }

                    // Copy segment data.
                    size_t to_copy = std::min(size - processed, (size_t)seg_len);
                    if (stream_stage_ == OpusStreamStage::kExpectTags && to_copy > 0) {
                        comment_started_ = true;
                    }
                    ctx_.calculated_crc = UpdateCrc(ctx_.calculated_crc, data + processed, to_copy);
                    memcpy(ctx_.packet_buf + ctx_.packet_len, data + processed, to_copy);

                    processed += to_copy;
                    ctx_.packet_len += to_copy;
                    ctx_.body_offset += to_copy;
                    ctx_.seg_remaining -= to_copy;

                    // Check whether the segment is complete.
                    if (ctx_.seg_remaining > 0) {
                        // Wait for the rest of the segment.
                        return processed;
                    }

                    // The segment is complete.
                    bool seg_continued = (ctx_.seg_table[ctx_.seg_index] == 255);

                    if (!seg_continued) {
                        // The packet ends at this segment.
                        if (ctx_.packet_len == 0) {
                            ESP_LOGE(TAG, "Empty Ogg packet is not valid Opus data");
                            has_error_ = true;
                            return processed;
                        }
                        ++ctx_.page_completed_packets;
                        if (!HandleCompletedPacket()) {
                            has_error_ = true;
                            return processed;
                        }
                        ctx_.packet_len = 0;
                        ctx_.packet_continued = false;
                    } else {
                        ctx_.packet_continued = true;
                    }

                    ctx_.seg_index++;
                    ctx_.seg_remaining = 0;
                }

                if (ctx_.seg_index == ctx_.seg_count) {
                    // Check whether the complete page body was read.
                    if (ctx_.body_offset < ctx_.body_size) {
                        ESP_LOGW(TAG, "Incomplete Ogg page body: %zu/%zu", ctx_.body_offset,
                                 ctx_.body_size);
                    }

                    // Preserve packet state when a packet continues on the next page.
                    if (!ctx_.packet_continued) {
                        ctx_.packet_len = 0;
                    }

                    if (!FinishPage()) {
                        return processed;
                    }

                    // Continue with the next page.
                    state_ = ParseState::FIND_PAGE;
                    ctx_.bytes_needed = 4;
                    ctx_.data_offset = 0;
                }
                break;
            }
        }
    }

    return processed;
}
