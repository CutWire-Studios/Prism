#include "core/WebRtcCamPage.h"

namespace WebRtcCamPage {

QString html(const QString &token, quint16 sigPort) {
    const QString safeToken = token.toHtmlEscaped();
    const QString sigPortStr = QString::number(sigPort);
    return QString(R"html(<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>SwitchX Phone Camera</title>
    <style>
        body { font-family: system-ui, sans-serif; background: #111; color: #eee; margin: 0; padding: 16px; text-align: center; }
        video { width: 100%; max-width: 480px; border-radius: 12px; background: #000; }
        .status { margin: 12px 0; font-size: 14px; color: #aaa; }
        button { background: #e5a93b; color: #111; border: none; border-radius: 10px; padding: 12px 20px; font-weight: 700; font-size: 16px; }
        button:disabled { opacity: 0.5; }
    </style>
</head>
<body>
    <h1>SwitchX Phone Camera</h1>
    <p class="status" id="status">Tap Start to stream your camera to SwitchX.</p>
    <video id="preview" autoplay playsinline muted></video>
    <p><button id="startBtn">Start streaming</button></p>
    <script>
        const TOKEN = "%1";
        const SIG_PORT = %2;
        const statusEl = document.getElementById('status');
        const preview = document.getElementById('preview');
        const startBtn = document.getElementById('startBtn');
        let ws = null;
        let pc = null;

        function setStatus(text) { statusEl.textContent = text; }

        function wsUrl() {
            const scheme = location.protocol === 'https:' ? 'wss' : 'ws';
            if (SIG_PORT > 0) {
                const host = location.hostname || '127.0.0.1';
                return `${scheme}://${host}:${SIG_PORT}/`;
            }
            return `${scheme}://${location.host}/ws`;
        }

        function send(msg) {
            if (ws && ws.readyState === WebSocket.OPEN)
                ws.send(JSON.stringify(msg));
        }

        async function start() {
            startBtn.disabled = true;
            setStatus('Requesting camera…');
            try {
                const stream = await navigator.mediaDevices.getUserMedia({
                    video: {
                        facingMode: 'environment',
                        width: { ideal: 1280, max: 1280 },
                        height: { ideal: 720, max: 720 },
                        frameRate: { ideal: 30, max: 30 }
                    },
                    audio: false
                });
                preview.srcObject = stream;
            } catch (err) {
                setStatus('Camera access denied: ' + err);
                startBtn.disabled = false;
                return;
            }

            setStatus('Connecting…');
            ws = new WebSocket(wsUrl());
            ws.onopen = async () => {
                send({ type: 'hello', token: TOKEN });
                pc = new RTCPeerConnection({ iceServers: [] });
                const videoTrack = preview.srcObject.getVideoTracks()[0];
                const tr = pc.addTransceiver(videoTrack, { direction: 'sendonly' });
                try {
                    if (tr.setCodecPreferences && window.RTCRtpSender && RTCRtpSender.getCapabilities) {
                        const caps = RTCRtpSender.getCapabilities('video');
                        const h264 = caps.codecs.filter(c => /h264/i.test(c.mimeType));
                        if (h264.length) tr.setCodecPreferences(h264);
                    }
                } catch (e) { /* older browsers: fall back to default negotiation */ }
                pc.onicecandidate = (ev) => {
                    if (ev.candidate) {
                        send({
                            type: 'candidate',
                            candidate: ev.candidate.candidate,
                            mid: ev.candidate.sdpMid
                        });
                    }
                };
                const offer = await pc.createOffer();
                await pc.setLocalDescription(offer);
                send({ type: 'offer', sdp: pc.localDescription.sdp });
            };
            ws.onmessage = async (ev) => {
                const msg = JSON.parse(ev.data);
                if (msg.type === 'answer' && pc) {
                    await pc.setRemoteDescription({ type: 'answer', sdp: msg.sdp });
                    setStatus('Streaming to SwitchX');
                } else if (msg.type === 'candidate' && pc && msg.candidate) {
                    await pc.addIceCandidate({ candidate: msg.candidate, sdpMid: msg.mid });
                }
            };
            ws.onerror = () => { setStatus('WebSocket error'); startBtn.disabled = false; };
            ws.onclose = () => { setStatus('Disconnected'); startBtn.disabled = false; };
        }

        startBtn.addEventListener('click', start);
    </script>
</body>
</html>)html").arg(safeToken, sigPortStr);
}

} // namespace WebRtcCamPage
