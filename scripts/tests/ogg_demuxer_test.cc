#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <utility>
#include <vector>

#define OGG_DEMUXER_TESTING
#include "audio/demuxer/ogg_demuxer.h"
#undef OGG_DEMUXER_TESTING

struct OggDemuxerTestPeer {
    static bool ValidateOpusTags(const uint8_t* data, size_t size) {
        return OggDemuxer::ValidateOpusTags(data, size);
    }
};

namespace {

uint32_t UpdateOggCrc(uint32_t crc, uint8_t byte) {
    crc ^= static_cast<uint32_t>(byte) << 24;
    for (int bit = 0; bit < 8; ++bit) {
        crc = (crc & 0x80000000U) != 0 ? (crc << 1) ^ 0x04c11db7U : crc << 1;
    }
    return crc;
}

void WriteLe32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
    bytes[offset + 2] = static_cast<uint8_t>(value >> 16);
    bytes[offset + 3] = static_cast<uint8_t>(value >> 24);
}

void WriteLe64(std::vector<uint8_t>& bytes, size_t offset, uint64_t value) {
    for (size_t i = 0; i < 8; ++i) {
        bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8));
    }
}

std::vector<uint8_t> ValidHead(uint8_t version = 1, uint8_t mapping_family = 0,
                               uint16_t pre_skip = 0) {
    return {'O',
            'p',
            'u',
            's',
            'H',
            'e',
            'a',
            'd',
            version,
            1,
            static_cast<uint8_t>(pre_skip),
            static_cast<uint8_t>(pre_skip >> 8),
            0x80,
            0xbb,
            0,
            0,
            0,
            0,
            mapping_family};
}

std::vector<uint8_t> ValidTags() {
    return {'O', 'p', 'u', 's', 'T', 'a', 'g', 's', 0, 0, 0, 0, 0, 0, 0, 0};
}

std::vector<uint8_t> BuildPage(uint8_t flags, uint32_t serial, uint32_t sequence,
                               uint64_t granule_position,
                               const std::vector<std::vector<uint8_t>>& packets) {
    size_t payload_size = 0;
    for (const auto& packet : packets) {
        assert(packet.size() < 255);
        payload_size += packet.size();
    }
    assert(packets.size() <= 255);
    std::vector<uint8_t> page(27 + packets.size() + payload_size, 0);
    page[0] = 'O';
    page[1] = 'g';
    page[2] = 'g';
    page[3] = 'S';
    page[4] = 0;
    page[5] = flags;
    WriteLe64(page, 6, granule_position);
    WriteLe32(page, 14, serial);
    WriteLe32(page, 18, sequence);
    page[26] = static_cast<uint8_t>(packets.size());
    size_t payload_offset = 27 + packets.size();
    for (size_t i = 0; i < packets.size(); ++i) {
        page[27 + i] = static_cast<uint8_t>(packets[i].size());
        std::copy(packets[i].begin(), packets[i].end(), page.begin() + payload_offset);
        payload_offset += packets[i].size();
    }
    uint32_t crc = 0;
    for (uint8_t byte : page) {
        crc = UpdateOggCrc(crc, byte);
    }
    WriteLe32(page, 22, crc);
    return page;
}

std::vector<uint8_t> BuildPage(uint8_t flags, uint32_t serial, uint32_t sequence,
                               uint64_t granule_position, const std::vector<uint8_t>& packet) {
    return BuildPage(flags, serial, sequence, granule_position,
                     std::vector<std::vector<uint8_t>>{packet});
}

std::vector<uint8_t> BuildRawPage(uint8_t flags, uint32_t serial, uint32_t sequence,
                                  uint64_t granule_position,
                                  const std::vector<uint8_t>& lacing_values,
                                  const std::vector<uint8_t>& payload) {
    const size_t described_size = [&]() {
        size_t size = 0;
        for (uint8_t value : lacing_values) {
            size += value;
        }
        return size;
    }();
    assert(lacing_values.size() <= 255);
    assert(described_size == payload.size());

    std::vector<uint8_t> page(27 + lacing_values.size() + payload.size(), 0);
    page[0] = 'O';
    page[1] = 'g';
    page[2] = 'g';
    page[3] = 'S';
    page[5] = flags;
    WriteLe64(page, 6, granule_position);
    WriteLe32(page, 14, serial);
    WriteLe32(page, 18, sequence);
    page[26] = static_cast<uint8_t>(lacing_values.size());
    std::copy(lacing_values.begin(), lacing_values.end(), page.begin() + 27);
    std::copy(payload.begin(), payload.end(), page.begin() + 27 + lacing_values.size());
    uint32_t crc = 0;
    for (uint8_t byte : page) {
        crc = UpdateOggCrc(crc, byte);
    }
    WriteLe32(page, 22, crc);
    return page;
}

void Append(std::vector<uint8_t>& stream, std::vector<uint8_t> page) {
    stream.insert(stream.end(), page.begin(), page.end());
}

std::vector<uint8_t> BuildStream(bool eos = true, uint32_t last_sequence = 2,
                                 uint32_t last_serial = 0x12345678U) {
    std::vector<uint8_t> audio = {0x00};
    auto first = BuildPage(0x02, 0x12345678U, 0, 0, ValidHead());
    auto second = BuildPage(0x00, 0x12345678U, 1, 0, ValidTags());
    auto third = BuildPage(eos ? 0x04 : 0x00, last_serial, last_sequence, 480, audio);
    Append(first, std::move(second));
    Append(first, std::move(third));
    return first;
}

bool Accepts(const std::vector<uint8_t>& stream) {
    OggDemuxer demuxer;
    return demuxer.Process(stream.data(), stream.size()) == stream.size() && demuxer.Finish();
}

bool AcceptsByteByByte(const std::vector<uint8_t>& stream) {
    OggDemuxer demuxer;
    for (const uint8_t& byte : stream) {
        if (demuxer.Process(&byte, 1) != 1) {
            return false;
        }
    }
    return demuxer.Finish();
}

void TestValidCompleteStreamIsAccepted() { assert(Accepts(BuildStream())); }

void TestValidCompleteStreamIsAcceptedByteByByte() { assert(AcceptsByteByByte(BuildStream())); }

void TestMissingEosIsRejected() { assert(!Accepts(BuildStream(false))); }

void TestEmptyEosPageCannotBypassGranuleValidation() {
    std::vector<uint8_t> stream;
    Append(stream, BuildPage(0x02, 1, 0, 0, ValidHead()));
    Append(stream, BuildPage(0x00, 1, 1, 0, ValidTags()));
    Append(stream, BuildPage(0x00, 1, 2, 480, std::vector<uint8_t>{0x00}));
    Append(stream, BuildPage(0x04, 1, 3, UINT64_MAX, std::vector<std::vector<uint8_t>>{}));
    assert(!Accepts(stream));
}

void TestBadCrcIsRejected() {
    auto stream = BuildStream();
    stream.back() ^= 0x01;
    assert(!Accepts(stream));
}

void TestChangedSerialIsRejected() { assert(!Accepts(BuildStream(true, 2, 0x87654321U))); }

void TestSkippedPageSequenceIsRejected() { assert(!Accepts(BuildStream(true, 3))); }

void TestTruncatedLastPageIsRejected() {
    auto stream = BuildStream();
    stream.pop_back();
    assert(!Accepts(stream));
}

void TestTagsBeforeHeadIsRejected() {
    std::vector<uint8_t> stream;
    Append(stream, BuildPage(0x02, 1, 0, 0, ValidTags()));
    Append(stream, BuildPage(0x00, 1, 1, 0, ValidHead()));
    Append(stream, BuildPage(0x04, 1, 2, 480, std::vector<uint8_t>{0x00}));
    assert(!Accepts(stream));
}

void TestPacketBetweenMandatoryHeadersIsRejected() {
    std::vector<uint8_t> stream;
    Append(stream, BuildPage(0x02, 1, 0, 0, ValidHead()));
    Append(stream, BuildPage(0x00, 1, 1, 0, std::vector<uint8_t>{0x00}));
    Append(stream, BuildPage(0x00, 1, 2, 0, ValidTags()));
    Append(stream, BuildPage(0x04, 1, 3, 480, std::vector<uint8_t>{0x00}));
    assert(!Accepts(stream));
}

void TestUnsupportedHeadVersionIsRejected() {
    auto stream = BuildStream();
    auto bad_head = BuildPage(0x02, 0x12345678U, 0, 0, ValidHead(0xff));
    const auto first_page_size = 27 + 1 + ValidHead().size();
    std::copy(bad_head.begin(), bad_head.end(), stream.begin());
    assert(bad_head.size() == first_page_size);
    assert(!Accepts(stream));
}

void TestUnsupportedMappingFamilyIsRejected() {
    std::vector<uint8_t> stream;
    Append(stream, BuildPage(0x02, 1, 0, 0, ValidHead(1, 1)));
    Append(stream, BuildPage(0x00, 1, 1, 0, ValidTags()));
    Append(stream, BuildPage(0x04, 1, 2, 480, std::vector<uint8_t>{0x00}));
    assert(!Accepts(stream));
}

void TestTruncatedTagsIsRejected() {
    std::vector<uint8_t> stream;
    Append(stream, BuildPage(0x02, 1, 0, 0, ValidHead()));
    Append(stream,
           BuildPage(0x00, 1, 1, 0, std::vector<uint8_t>{'O', 'p', 'u', 's', 'T', 'a', 'g', 's'}));
    Append(stream, BuildPage(0x04, 1, 2, 480, std::vector<uint8_t>{0x00}));
    assert(!Accepts(stream));
}

void TestTagsWithOutOfBoundsVendorLengthIsRejected() {
    auto tags = ValidTags();
    tags[8] = 0xff;
    tags[9] = 0xff;
    tags[10] = 0xff;
    tags[11] = 0x7f;
    std::vector<uint8_t> stream;
    Append(stream, BuildPage(0x02, 1, 0, 0, ValidHead()));
    Append(stream, BuildPage(0x00, 1, 1, 0, tags));
    Append(stream, BuildPage(0x04, 1, 2, 480, std::vector<uint8_t>{0x00}));
    assert(!Accepts(stream));
}

void TestTagsCommentLengthsNeverReadPastDeclaredPacket() {
    std::vector<uint8_t> storage(4096, 0);
    const auto signature = ValidTags();
    std::copy(signature.begin(), signature.end(), storage.begin());
    WriteLe32(storage, 12, 508);
    WriteLe32(storage, 16, 2028);

    // Only the first 2048 bytes belong to the packet. The readable zero guard
    // makes an unchecked parser deterministically accept instead of crashing.
    assert(!OggDemuxerTestPeer::ValidateOpusTags(storage.data(), 2048));
}

void TestMalformedCodeTwoPacketIsRejected() {
    std::vector<uint8_t> stream;
    Append(stream, BuildPage(0x02, 1, 0, 0, ValidHead()));
    Append(stream, BuildPage(0x00, 1, 1, 0, ValidTags()));
    Append(stream, BuildPage(0x04, 1, 2, 960, std::vector<uint8_t>{0x02}));
    assert(!Accepts(stream));
}

void TestDecoderUnsupportedFifteenMsPacketIsRejected() {
    std::vector<uint8_t> stream;
    Append(stream, BuildPage(0x02, 1, 0, 0, ValidHead()));
    Append(stream, BuildPage(0x00, 1, 1, 0, ValidTags()));
    Append(stream, BuildPage(0x04, 1, 2, 720, std::vector<uint8_t>{0x8b, 0x03, 1, 1, 1}));
    assert(!Accepts(stream));
}

void TestNonzeroHeaderGranuleIsRejected() {
    std::vector<uint8_t> stream;
    Append(stream, BuildPage(0x02, 1, 0, 1, ValidHead()));
    Append(stream, BuildPage(0x00, 1, 1, 0, ValidTags()));
    Append(stream, BuildPage(0x04, 1, 2, 480, std::vector<uint8_t>{0x00}));
    assert(!Accepts(stream));
}

void TestHeadMustBeAloneOnBosPage() {
    std::vector<uint8_t> stream;
    Append(stream, BuildPage(0x02, 1, 0, 0, {ValidHead(), ValidTags()}));
    Append(stream, BuildPage(0x04, 1, 1, 480, std::vector<uint8_t>{0x00}));
    assert(!Accepts(stream));
}

void TestEmptyPacketAfterHeadIsRejected() {
    std::vector<uint8_t> stream;
    Append(stream, BuildRawPage(0x02, 1, 0, 0, {19, 0}, ValidHead()));
    Append(stream, BuildPage(0x00, 1, 1, 0, ValidTags()));
    Append(stream, BuildPage(0x04, 1, 2, 480, std::vector<uint8_t>{0x00}));
    assert(!Accepts(stream));
}

void TestFinalPacketExactly255BytesIsAccepted() {
    std::vector<uint8_t> stream;
    Append(stream, BuildPage(0x02, 1, 0, 0, ValidHead()));
    Append(stream, BuildPage(0x00, 1, 1, 0, ValidTags()));
    std::vector<uint8_t> audio(255, 0);
    Append(stream, BuildRawPage(0x04, 1, 2, 480, {255, 0}, audio));
    assert(Accepts(stream));
}

void TestTagsMustFinishItsPage() {
    std::vector<uint8_t> stream;
    Append(stream, BuildPage(0x02, 1, 0, 0, ValidHead()));
    Append(stream, BuildPage(0x00, 1, 1, 480, {ValidTags(), std::vector<uint8_t>{0x00}}));
    Append(stream, BuildPage(0x04, 1, 2, 960, std::vector<uint8_t>{0x00}));
    assert(!Accepts(stream));
}

void TestDecreasingAudioGranuleIsRejected() {
    std::vector<uint8_t> stream;
    Append(stream, BuildPage(0x02, 1, 0, 0, ValidHead()));
    Append(stream, BuildPage(0x00, 1, 1, 0, ValidTags()));
    Append(stream, BuildPage(0x00, 1, 2, 960, std::vector<uint8_t>{0x00}));
    Append(stream, BuildPage(0x04, 1, 3, 480, std::vector<uint8_t>{0x00}));
    assert(!Accepts(stream));
}

void TestCompleteStreamShorterThanPreSkipIsRejected() {
    std::vector<uint8_t> stream;
    Append(stream, BuildPage(0x02, 1, 0, 0, ValidHead(1, 0, 65535)));
    Append(stream, BuildPage(0x00, 1, 1, 0, ValidTags()));
    Append(stream, BuildPage(0x04, 1, 2, 480, std::vector<uint8_t>{0x00}));
    assert(!Accepts(stream));
}

void TestFinalGranuleCannotExceedDecodedTimelineAfterEarlierAudio() {
    std::vector<uint8_t> stream;
    Append(stream, BuildPage(0x02, 1, 0, 0, ValidHead()));
    Append(stream, BuildPage(0x00, 1, 1, 0, ValidTags()));
    Append(stream, BuildPage(0x00, 1, 2, 480, std::vector<uint8_t>{0x00}));
    Append(stream, BuildPage(0x04, 1, 3, 2000, std::vector<uint8_t>{0x00}));
    assert(!Accepts(stream));
}

void TestNonFinalGranuleRejectsAnUnboundedForwardJump() {
    std::vector<uint8_t> stream;
    Append(stream, BuildPage(0x02, 1, 0, 0, ValidHead()));
    Append(stream, BuildPage(0x00, 1, 1, 0, ValidTags()));
    Append(stream, BuildPage(0x00, 1, 2, 480, std::vector<uint8_t>{0x00}));
    Append(stream, BuildPage(0x00, 1, 3, 7000, std::vector<uint8_t>{0x00}));
    Append(stream, BuildPage(0x04, 1, 4, 7480, std::vector<uint8_t>{0x00}));
    assert(!Accepts(stream));
}

void TestFirstEosGranuleMayEstablishALaterTimelineOrigin() {
    std::vector<uint8_t> stream;
    Append(stream, BuildPage(0x02, 1, 0, 0, ValidHead()));
    Append(stream, BuildPage(0x00, 1, 1, 0, ValidTags()));
    Append(stream, BuildPage(0x04, 1, 2, 1000000, std::vector<uint8_t>{0x00}));
    assert(Accepts(stream));
}

void TestBadCrcPageDoesNotEmitPackets() {
    auto stream = BuildStream();
    stream.back() ^= 0x01;
    size_t packet_count = 0;
    OggDemuxer demuxer;
    demuxer.OnPacket([&](const uint8_t*, int, int, size_t) { ++packet_count; });
    assert(demuxer.Process(stream.data(), stream.size()) == stream.size());
    assert(!demuxer.Finish());
    assert(packet_count == 0);
}

void TestOfficialPopupAssetIsAccepted() {
    std::ifstream input("main/assets/common/popup.ogg", std::ios::binary);
    assert(input.good());
    std::vector<uint8_t> stream((std::istreambuf_iterator<char>(input)),
                                std::istreambuf_iterator<char>());
    assert(!stream.empty());
    size_t packet_count = 0;
    OggDemuxer demuxer;
    demuxer.OnPacket([&](const uint8_t*, int, int, size_t) { ++packet_count; });
    assert(demuxer.Process(stream.data(), stream.size()) == stream.size());
    assert(demuxer.Finish());
    assert(packet_count > 0);
}

void TestStreamingDemuxerDoesNotImposeTheCustomDurationLimit() {
    std::vector<uint8_t> stream;
    const std::vector<std::vector<uint8_t>> first_audio_packets(255, std::vector<uint8_t>{0x00});
    const std::vector<std::vector<uint8_t>> final_audio_packets(46, std::vector<uint8_t>{0x00});
    Append(stream, BuildPage(0x02, 1, 0, 0, ValidHead()));
    Append(stream, BuildPage(0x00, 1, 1, 0, ValidTags()));
    Append(stream, BuildPage(0x00, 1, 2, 255U * 480U, first_audio_packets));
    Append(stream, BuildPage(0x04, 1, 3, 301U * 480U, final_audio_packets));

    assert(Accepts(stream));
}

void TestAllFirmwareOggAssetsAreAccepted() {
    size_t checked = 0;
    size_t rejected = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator("main/assets")) {
        if (!entry.is_regular_file() || entry.path().extension() != ".ogg") {
            continue;
        }
        std::ifstream input(entry.path(), std::ios::binary);
        std::vector<uint8_t> stream((std::istreambuf_iterator<char>(input)),
                                    std::istreambuf_iterator<char>());
        if (stream.empty() || !Accepts(stream)) {
            std::cerr << "Rejected firmware asset: " << entry.path() << std::endl;
            ++rejected;
        }
        ++checked;
    }
    assert(checked >= 600);
    assert(rejected == 0);
}

}  // namespace

int main() {
    TestValidCompleteStreamIsAccepted();
    TestValidCompleteStreamIsAcceptedByteByByte();
    TestMissingEosIsRejected();
    TestEmptyEosPageCannotBypassGranuleValidation();
    TestBadCrcIsRejected();
    TestChangedSerialIsRejected();
    TestSkippedPageSequenceIsRejected();
    TestTruncatedLastPageIsRejected();
    TestTagsBeforeHeadIsRejected();
    TestPacketBetweenMandatoryHeadersIsRejected();
    TestUnsupportedHeadVersionIsRejected();
    TestUnsupportedMappingFamilyIsRejected();
    TestTruncatedTagsIsRejected();
    TestTagsWithOutOfBoundsVendorLengthIsRejected();
    TestTagsCommentLengthsNeverReadPastDeclaredPacket();
    TestMalformedCodeTwoPacketIsRejected();
    TestDecoderUnsupportedFifteenMsPacketIsRejected();
    TestNonzeroHeaderGranuleIsRejected();
    TestHeadMustBeAloneOnBosPage();
    TestEmptyPacketAfterHeadIsRejected();
    TestFinalPacketExactly255BytesIsAccepted();
    TestTagsMustFinishItsPage();
    TestDecreasingAudioGranuleIsRejected();
    TestCompleteStreamShorterThanPreSkipIsRejected();
    TestFinalGranuleCannotExceedDecodedTimelineAfterEarlierAudio();
    TestNonFinalGranuleRejectsAnUnboundedForwardJump();
    TestFirstEosGranuleMayEstablishALaterTimelineOrigin();
    TestBadCrcPageDoesNotEmitPackets();
    TestOfficialPopupAssetIsAccepted();
    TestStreamingDemuxerDoesNotImposeTheCustomDurationLimit();
    TestAllFirmwareOggAssetsAreAccepted();
    std::cout << "ogg_demuxer tests passed" << std::endl;
    return 0;
}
