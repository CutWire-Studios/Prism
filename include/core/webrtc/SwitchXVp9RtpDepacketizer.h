#pragma once

#include <rtc/rtpdepacketizer.hpp>

#ifdef SWITCHX_HAVE_WEBRTC

/// VP9 RTP depacketizer (RFC 9628) with payload offset resync for mobile browsers.
class SwitchXVp9RtpDepacketizer final : public rtc::VideoRtpDepacketizer {
public:
    SwitchXVp9RtpDepacketizer();
    ~SwitchXVp9RtpDepacketizer() override;

private:
    rtc::message_ptr reassemble(rtc::VideoRtpDepacketizer::message_buffer &buffer) override;
};

#endif
