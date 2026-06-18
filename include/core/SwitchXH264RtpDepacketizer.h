#pragma once

#include <rtc/rtpdepacketizer.hpp>

#ifdef SWITCHX_HAVE_WEBRTC

/// H.264 RTP depacketizer that resyncs payload offsets for mobile browser encoders.
class SwitchXH264RtpDepacketizer final : public rtc::VideoRtpDepacketizer {
public:
    SwitchXH264RtpDepacketizer();
    ~SwitchXH264RtpDepacketizer() override;

private:
    rtc::message_ptr reassemble(rtc::VideoRtpDepacketizer::message_buffer &buffer) override;
};

#endif
