#include "core/SwitchXH264RtpDepacketizer.h"

#ifdef SWITCHX_HAVE_WEBRTC

#include <rtc/nalunit.hpp>
#include <rtc/rtp.hpp>

namespace {

constexpr uint8_t kNaluTypeStapA = 24;
constexpr uint8_t kNaluTypeFua   = 28;

const rtc::binary kNaluLongStartCode = {rtc::byte{0}, rtc::byte{0}, rtc::byte{0}, rtc::byte{1}};

size_t rtpPaddingSize(const rtc::message_ptr &packet, const rtc::RtpHeader *header) {
    if (!header->padding() || packet->empty())
        return 0;
    return std::to_integer<uint8_t>(packet->back());
}

size_t payloadOffset(const rtc::message_ptr &packet) {
    if (packet->size() < sizeof(rtc::RtpHeader))
        return SIZE_MAX;

    const auto *header = reinterpret_cast<const rtc::RtpHeader *>(packet->data());
    if (packet->size() < header->getSize())
        return SIZE_MAX;

    const char *base = reinterpret_cast<const char *>(packet->data());
    const char *body = header->getBody();
    if (body < base || body >= base + static_cast<ptrdiff_t>(packet->size()))
        return SIZE_MAX;

    return static_cast<size_t>(body - base);
}

void addSeparator(rtc::binary &frame) {
    frame.insert(frame.end(), kNaluLongStartCode.begin(), kNaluLongStartCode.end());
}

void appendStapA(rtc::binary &frame, const rtc::message_ptr &packet, size_t offset, size_t payloadEnd) {
    offset += 1;
    while (offset + 2 < payloadEnd) {
        const uint16_t naluSize =
            static_cast<uint16_t>(std::to_integer<uint8_t>(packet->at(offset)) << 8
                                  | std::to_integer<uint8_t>(packet->at(offset + 1)));
        offset += 2;
        if (naluSize == 0 || offset + naluSize > payloadEnd)
            break;

        addSeparator(frame);
        frame.insert(frame.end(), packet->begin() + offset, packet->begin() + offset + naluSize);
        offset += naluSize;
    }
}

} // namespace

SwitchXH264RtpDepacketizer::SwitchXH264RtpDepacketizer() = default;

SwitchXH264RtpDepacketizer::~SwitchXH264RtpDepacketizer() = default;

rtc::message_ptr SwitchXH264RtpDepacketizer::reassemble(rtc::VideoRtpDepacketizer::message_buffer &buffer) {
    if (buffer.empty())
        return nullptr;

    const auto first = *buffer.begin();
    const auto *firstHeader = reinterpret_cast<const rtc::RtpHeader *>(first->data());
    const uint8_t payloadType = firstHeader->payloadType();
    const uint32_t timestamp  = firstHeader->timestamp();
    uint16_t nextSeqNumber    = firstHeader->seqNumber();

    rtc::binary frame;
    bool continuousFragments = false;

    for (const auto &packet : buffer) {
        if (packet->size() < sizeof(rtc::RtpHeader))
            continue;

        const auto *rtpHeader = reinterpret_cast<const rtc::RtpHeader *>(packet->data());
        if (packet->size() < rtpHeader->getSize())
            continue;

        if (rtpHeader->seqNumber() < nextSeqNumber)
            continue;
        if (rtpHeader->seqNumber() > nextSeqNumber)
            continuousFragments = false;

        nextSeqNumber = static_cast<uint16_t>(rtpHeader->seqNumber() + 1);

        const size_t paddingSize = rtpPaddingSize(packet, rtpHeader);
        if (packet->size() <= paddingSize)
            continue;

        const size_t payloadEnd = packet->size() - paddingSize;
        const size_t payloadStart = payloadOffset(packet);
        if (payloadStart == SIZE_MAX || payloadStart >= payloadEnd)
            continue;

        const uint8_t nalHeaderByte = std::to_integer<uint8_t>(packet->at(payloadStart));
        const uint8_t nalType       = nalHeaderByte & 0x1F;

        if (nalType == kNaluTypeFua) {
            if (payloadEnd <= payloadStart + 1)
                continue;

            const rtc::NalUnitHeader nalUnitHeader{nalHeaderByte};
            const rtc::NalUnitFragmentHeader fragHeader{
                std::to_integer<uint8_t>(packet->at(payloadStart + 1))};

            if (fragHeader.isStart()) {
                addSeparator(frame);
                frame.emplace_back(rtc::byte(nalUnitHeader.idc() | fragHeader.unitType()));
                continuousFragments = true;
            }

            if (continuousFragments) {
                frame.insert(frame.end(), packet->begin() + payloadStart + 2,
                             packet->begin() + payloadEnd);
            }

            if (fragHeader.isEnd())
                continuousFragments = false;
            continue;
        }

        continuousFragments = false;

        if (nalType == kNaluTypeStapA) {
            appendStapA(frame, packet, payloadStart, payloadEnd);
            continue;
        }

        if (nalType >= 1 && nalType < 24) {
            addSeparator(frame);
            frame.insert(frame.end(), packet->begin() + payloadStart, packet->begin() + payloadEnd);
        }
    }

    if (frame.empty())
        return nullptr;

    return rtc::make_message(std::move(frame), createFrameInfo(timestamp, payloadType));
}

#endif
