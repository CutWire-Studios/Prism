#include "core/webrtc/PrismVp9RtpDepacketizer.h"

#ifdef PRISM_HAVE_WEBRTC

#include <rtc/rtp.hpp>

#include <vector>

namespace {

constexpr uint8_t kBitI = 0b10000000;
constexpr uint8_t kBitP = 0b01000000;
constexpr uint8_t kBitL = 0b00100000;
constexpr uint8_t kBitF = 0b00010000;
constexpr uint8_t kBitB = 0b00001000;
constexpr uint8_t kBitV = 0b00000010;
constexpr uint8_t kBitM = 0b10000000;
constexpr uint8_t kBitN = 0b00000001;
constexpr int     kMaxRefPics = 3;

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

size_t vp9DescriptorSize(const uint8_t *payloadData, size_t payloadSize) {
    if (payloadSize < 1)
        return 0;

    size_t descriptorSize = 1;
    const uint8_t firstByte = payloadData[0];

    if (firstByte & kBitI) {
        if (payloadSize < descriptorSize + 1)
            return 0;
        const uint8_t pictureIdByte = payloadData[descriptorSize];
        ++descriptorSize;
        if (pictureIdByte & kBitM) {
            if (payloadSize < descriptorSize + 1)
                return 0;
            ++descriptorSize;
        }
    }

    if (firstByte & kBitL) {
        if (payloadSize < descriptorSize + 1)
            return 0;
        ++descriptorSize;
        if (!(firstByte & kBitF)) {
            if (payloadSize < descriptorSize + 1)
                return 0;
            ++descriptorSize;
        }
    }

    if ((firstByte & kBitP) && (firstByte & kBitF)) {
        for (int i = 0; i < kMaxRefPics; ++i) {
            if (payloadSize < descriptorSize + 1)
                break;
            const uint8_t refByte = payloadData[descriptorSize];
            ++descriptorSize;
            if (!(refByte & kBitN))
                break;
        }
    }

    if (firstByte & kBitV) {
        if (payloadSize < descriptorSize + 1)
            return 0;
        const uint8_t ssByte = payloadData[descriptorSize];
        ++descriptorSize;

        const int numSpatialLayers = (ssByte >> 5) + 1;
        const bool resolutionPresent = (ssByte >> 4) & 0x01;
        const bool pgPresent = (ssByte >> 3) & 0x01;

        if (resolutionPresent) {
            const size_t resolutionSize = 4 * static_cast<size_t>(numSpatialLayers);
            if (payloadSize < descriptorSize + resolutionSize)
                return 0;
            descriptorSize += resolutionSize;
        }

        if (pgPresent) {
            if (payloadSize < descriptorSize + 1)
                return 0;
            const uint8_t numPictureGroups = payloadData[descriptorSize];
            ++descriptorSize;

            for (int i = 0; i < numPictureGroups; ++i) {
                if (payloadSize < descriptorSize + 1)
                    return 0;
                const uint8_t pgByte = payloadData[descriptorSize];
                ++descriptorSize;
                const int numRefs = (pgByte >> 2) & 0x03;
                if (payloadSize < descriptorSize + static_cast<size_t>(numRefs))
                    return 0;
                descriptorSize += static_cast<size_t>(numRefs);
            }
        }
    }

    return descriptorSize <= payloadSize ? descriptorSize : 0;
}

} // namespace

PrismVp9RtpDepacketizer::PrismVp9RtpDepacketizer() = default;

PrismVp9RtpDepacketizer::~PrismVp9RtpDepacketizer() = default;

rtc::message_ptr PrismVp9RtpDepacketizer::reassemble(rtc::VideoRtpDepacketizer::message_buffer &buffer) {
    if (buffer.empty())
        return nullptr;

    const auto first = *buffer.begin();
    const auto *firstHeader = reinterpret_cast<const rtc::RtpHeader *>(first->data());
    const uint8_t payloadType = firstHeader->payloadType();
    const uint32_t timestamp  = firstHeader->timestamp();
    uint16_t nextSeqNumber    = firstHeader->seqNumber();

    rtc::binary frame;
    std::vector<std::pair<const uint8_t *, size_t>> payloads;
    bool continuousSequence = false;

    for (const auto &packet : buffer) {
        if (packet->size() < sizeof(rtc::RtpHeader))
            continue;

        const auto *rtpHeader = reinterpret_cast<const rtc::RtpHeader *>(packet->data());
        if (packet->size() < rtpHeader->getSize())
            continue;

        if (rtpHeader->seqNumber() < nextSeqNumber)
            continue;
        if (rtpHeader->seqNumber() > nextSeqNumber)
            continuousSequence = false;

        nextSeqNumber = static_cast<uint16_t>(rtpHeader->seqNumber() + 1);

        const size_t paddingSize = rtpPaddingSize(packet, rtpHeader);
        if (packet->size() <= paddingSize)
            continue;

        const size_t payloadEnd = packet->size() - paddingSize;
        const size_t payloadStart = payloadOffset(packet);
        if (payloadStart == SIZE_MAX || payloadStart >= payloadEnd)
            continue;

        const size_t payloadSize = payloadEnd - payloadStart;
        const uint8_t *payloadData = reinterpret_cast<const uint8_t *>(packet->data()) + payloadStart;
        const size_t descriptorSize = vp9DescriptorSize(payloadData, payloadSize);
        if (descriptorSize == 0 || payloadSize <= descriptorSize)
            continue;

        const uint8_t firstByte = payloadData[0];
        const uint8_t *frameData = payloadData + descriptorSize;
        const size_t framePayloadSize = payloadSize - descriptorSize;

        if ((firstByte & kBitB) || rtpHeader->marker()) {
            if (continuousSequence) {
                for (const auto &[data, size] : payloads) {
                    frame.insert(frame.end(),
                                 reinterpret_cast<const rtc::byte *>(data),
                                 reinterpret_cast<const rtc::byte *>(data + size));
                }

                if (rtpHeader->marker()) {
                    frame.insert(frame.end(),
                                 reinterpret_cast<const rtc::byte *>(frameData),
                                 reinterpret_cast<const rtc::byte *>(frameData + framePayloadSize));
                }
            } else if ((firstByte & kBitB) && rtpHeader->marker()) {
                frame.insert(frame.end(),
                             reinterpret_cast<const rtc::byte *>(frameData),
                             reinterpret_cast<const rtc::byte *>(frameData + framePayloadSize));
            }
            payloads.clear();
            continuousSequence = true;
        }

        if (!rtpHeader->marker())
            payloads.emplace_back(frameData, framePayloadSize);
    }

    if (frame.empty())
        return nullptr;

    return rtc::make_message(std::move(frame), createFrameInfo(timestamp, payloadType));
}

#endif
