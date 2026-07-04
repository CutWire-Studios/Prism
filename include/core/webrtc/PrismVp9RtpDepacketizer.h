#pragma once

#ifdef PRISM_HAVE_WEBRTC

#include <rtc/rtpdepacketizer.hpp>

/// VP9 RTP depacketizer (RFC 9628) with payload offset resync for mobile browsers.
class PrismVp9RtpDepacketizer final : public rtc::VideoRtpDepacketizer {
public:
    PrismVp9RtpDepacketizer();
    ~PrismVp9RtpDepacketizer() override;

private:
    rtc::message_ptr reassemble(rtc::VideoRtpDepacketizer::message_buffer &buffer) override;
};

#endif
