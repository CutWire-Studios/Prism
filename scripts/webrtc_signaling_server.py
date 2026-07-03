#!/usr/bin/env python3
"""CutWire Prism public WebRTC signaling relay + browser camera page.

Install deps once:
    pip install fastapi uvicorn

Run with exactly **one worker** (in-memory rooms):
    uvicorn webrtc_signaling_server:app --host 127.0.0.1 --port 8765 --workers 1

Run with built-in TLS (Let's Encrypt cert paths):
    uvicorn webrtc_signaling_server:app --host 0.0.0.0 --port 443 \\
        --ssl-keyfile /etc/letsencrypt/live/example.com/privkey.pem \\
        --ssl-certfile /etc/letsencrypt/live/example.com/fullchain.pem

Production relay: wss://roboti.qzz.io/ws  (https://roboti.qzz.io)

Point CutWire Prism at: wss://your-host/ws  (or ws://… when using plain HTTP)
"""

from __future__ import annotations

import json
import logging
from dataclasses import dataclass, field
from typing import Dict, List, Optional

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import HTMLResponse, PlainTextResponse

log = logging.getLogger("prism.signaling")
logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")

app = FastAPI(title="CutWire Prism WebRTC Signaling", docs_url=None, redoc_url=None)


@dataclass
class Room:
    phone: Optional[WebSocket] = None
    desktop: Optional[WebSocket] = None
    pending_to_phone: List[str] = field(default_factory=list)
    pending_to_desktop: List[str] = field(default_factory=list)


rooms: Dict[str, Room] = {}
MAX_PENDING = 64


def _other_peer(room: Room, role: str) -> Optional[WebSocket]:
    """Return the peer socket for @p role (phone -> desktop, desktop -> phone)."""
    return room.desktop if role == "phone" else room.phone


def _clear_peer(room: Room, role: str, ws: WebSocket) -> None:
    if role == "desktop" and room.desktop is ws:
        room.desktop = None
    elif role == "phone" and room.phone is ws:
        room.phone = None


async def _flush_pending(room: Room, role: str) -> None:
    if role == "desktop":
        target, queue = room.desktop, room.pending_to_desktop
    else:
        target, queue = room.phone, room.pending_to_phone
    if not target or not queue:
        return
    count = len(queue)
    for raw in queue:
        await target.send_text(raw)
    queue.clear()
    log.info("flushed %d pending message(s) to %s", count, role)


async def _relay(room: Room, from_role: str, from_ws: WebSocket, raw: str, msg_type: str) -> None:
    peer = _other_peer(room, from_role)
    if peer is not None:
        await peer.send_text(raw)
        if msg_type in ("offer", "answer", "candidate"):
            log.info("relay %s -> %s", msg_type, "desktop" if from_role == "phone" else "phone")
        return

    queue = room.pending_to_desktop if from_role == "phone" else room.pending_to_phone
    if len(queue) < MAX_PENDING:
        queue.append(raw)
    if from_role == "phone" and msg_type in ("offer", "candidate"):
        await from_ws.send_json({
            "type": "waiting",
            "message": "Waiting for CutWire Prism desktop — keep the pairing dialog open on your PC",
        })
    log.info("queued %s (no %s yet)", msg_type, "desktop" if from_role == "phone" else "phone")


CAM_PAGE_HTML = r"""<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>CutWire Prism Phone Camera</title>
    <style>
        body { font-family: system-ui, sans-serif; background: #111; color: #eee; margin: 0; padding: 16px; text-align: center; }
        video { width: 100%; max-width: 480px; border-radius: 12px; background: #000; }
        .status { margin: 12px 0; font-size: 14px; color: #aaa; }
        .controls { max-width: 480px; margin: 0 auto 16px; text-align: left; display: grid; gap: 12px; }
        .field { display: grid; gap: 6px; }
        .field label { font-size: 13px; color: #bbb; font-weight: 600; }
        select {
            width: 100%; box-sizing: border-box; background: #1b1b1b; color: #eee;
            border: 1px solid #333; border-radius: 10px; padding: 10px 12px; font-size: 15px;
        }
        select:disabled { opacity: 0.5; }
        button { background: #e5a93b; color: #111; border: none; border-radius: 10px; padding: 12px 20px; font-weight: 700; font-size: 16px; }
        button:disabled { opacity: 0.5; }
    </style>
</head>
<body>
    <h1>CutWire Prism Phone Camera</h1>
    <p class="status" id="status">Loading…</p>
    <div class="controls">
        <div class="field">
            <label for="cameraSelect">Camera</label>
            <select id="cameraSelect" disabled></select>
        </div>
        <div class="field">
            <label for="resolutionSelect">Resolution</label>
            <select id="resolutionSelect" disabled></select>
        </div>
    </div>
    <video id="preview" autoplay playsinline muted></video>
    <p><button id="startBtn" disabled>Start streaming</button></p>
    <script>
        const RESOLUTIONS = [
            { label: '1280×720 (HD)', width: 1280, height: 720 },
            { label: '1920×1080 (Full HD)', width: 1920, height: 1080 },
            { label: '960×540', width: 960, height: 540 },
            { label: '640×480', width: 640, height: 480 },
            { label: 'Device default', width: 0, height: 0 }
        ];
        const statusEl = document.getElementById('status');
        const preview = document.getElementById('preview');
        const startBtn = document.getElementById('startBtn');
        const cameraSelect = document.getElementById('cameraSelect');
        const resolutionSelect = document.getElementById('resolutionSelect');
        let pairing = null;
        let ws = null;
        let pc = null;
        let previewStream = null;
        let streaming = false;

        function setStatus(text) { statusEl.textContent = text; }

        function setControlsEnabled(enabled) {
            cameraSelect.disabled = !enabled;
            resolutionSelect.disabled = !enabled;
            startBtn.disabled = !enabled || streaming;
        }

        function base64UrlDecode(value) {
            let b64 = value.replace(/-/g, '+').replace(/_/g, '/');
            while (b64.length % 4) b64 += '=';
            return atob(b64);
        }

        function loadPairing() {
            const params = new URLSearchParams(location.search);
            const d = params.get('d');
            if (!d) throw new Error('Missing pairing data (?d=…) in URL.');
            const obj = JSON.parse(base64UrlDecode(d));
            if (obj.app !== 'prism') throw new Error('Invalid pairing payload.');
            if (!obj.token) throw new Error('Pairing payload missing token.');
            return obj;
        }

        function wsUrl() {
            if (pairing.relay) return pairing.relay;
            const scheme = location.protocol === 'https:' ? 'wss' : 'ws';
            if (pairing.host && pairing.host === location.hostname)
                return `${scheme}://${location.host}/ws`;
            const sig = pairing.sig || 0;
            if (sig > 0) {
                const host = pairing.host || location.hostname || '127.0.0.1';
                const defaultPort = scheme === 'wss' ? 443 : 80;
                const port = sig === defaultPort ? '' : `:${sig}`;
                return `${scheme}://${host}${port}/ws`;
            }
            return `${scheme}://${location.host}/ws`;
        }

        const ICE_SERVERS = [{ urls: 'stun:stun.l.google.com:19302' }, { urls: 'stun:stun1.l.google.com:19302' }];

        function selectedResolution() {
            return RESOLUTIONS[resolutionSelect.selectedIndex] || RESOLUTIONS[0];
        }

        function buildVideoConstraints() {
            const res = selectedResolution();
            const video = { frameRate: { ideal: 30, max: 30 } };
            if (cameraSelect.value)
                video.deviceId = { exact: cameraSelect.value };
            if (res.width > 0) {
                video.width = { ideal: res.width, max: res.width };
                video.height = { ideal: res.height, max: res.height };
            }
            return { video, audio: false };
        }

        function populateResolutions() {
            resolutionSelect.innerHTML = '';
            RESOLUTIONS.forEach((res, index) => {
                const opt = document.createElement('option');
                opt.value = String(index);
                opt.textContent = res.label;
                resolutionSelect.appendChild(opt);
            });
        }

        async function populateCameras() {
            const devices = await navigator.mediaDevices.enumerateDevices();
            const cameras = devices.filter(d => d.kind === 'videoinput');
            cameraSelect.innerHTML = '';
            cameras.forEach((cam, index) => {
                const opt = document.createElement('option');
                opt.value = cam.deviceId;
                opt.textContent = cam.label || ('Camera ' + (index + 1));
                cameraSelect.appendChild(opt);
            });
            if (!cameras.length)
                throw new Error('No cameras found');
        }

        function stopPreview() {
            if (previewStream) {
                previewStream.getTracks().forEach(t => t.stop());
                previewStream = null;
            }
            preview.srcObject = null;
        }

        async function openPreview() {
            if (streaming) return;
            stopPreview();
            previewStream = await navigator.mediaDevices.getUserMedia(buildVideoConstraints());
            preview.srcObject = previewStream;
            const track = previewStream.getVideoTracks()[0];
            if (track) {
                const settings = track.getSettings();
                if (settings.width && settings.height)
                    setStatus('Preview: ' + settings.width + '×' + settings.height);
                else
                    setStatus('Preview ready');
            }
        }

        async function initPage() {
            try {
                pairing = loadPairing();
            } catch (err) {
                setStatus(String(err));
                return;
            }
            populateResolutions();
            if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
                setStatus('Camera API not available in this browser.');
                return;
            }
            setStatus('Requesting camera access…');
            try {
                const tmp = await navigator.mediaDevices.getUserMedia({ video: true, audio: false });
                tmp.getTracks().forEach(t => t.stop());
            } catch (err) {
                setStatus('Camera access denied: ' + err);
                return;
            }
            try {
                await populateCameras();
            } catch (err) {
                setStatus(String(err));
                return;
            }
            setControlsEnabled(true);
            try {
                await openPreview();
                setStatus('Choose camera and resolution, then tap Start.');
            } catch (err) {
                setStatus('Could not open camera: ' + err);
            }
        }

        function send(msg) {
            if (ws && ws.readyState === WebSocket.OPEN)
                ws.send(JSON.stringify(msg));
        }

        function teardownConnection() {
            if (pc) { pc.close(); pc = null; }
            if (ws) { ws.close(); ws = null; }
            streaming = false;
            setControlsEnabled(true);
        }

        async function start() {
            if (streaming) return;
            if (!previewStream || !previewStream.getVideoTracks().length) {
                setStatus('No camera preview available.');
                return;
            }
            streaming = true;
            setControlsEnabled(false);
            setStatus('Connecting…');
            ws = new WebSocket(wsUrl());
            ws.onopen = async () => {
                send({ type: 'hello', token: pairing.token, role: 'phone' });
                pc = new RTCPeerConnection({ iceServers: ICE_SERVERS });
                const videoTrack = previewStream.getVideoTracks()[0];
                const tr = pc.addTransceiver(videoTrack, { direction: 'sendonly' });
                try {
                    if (tr.setCodecPreferences && window.RTCRtpSender && RTCRtpSender.getCapabilities) {
                        const caps = RTCRtpSender.getCapabilities('video');
                        const vp9 = caps.codecs.filter(c => /vp9/i.test(c.mimeType));
                        if (vp9.length) tr.setCodecPreferences(vp9);
                    }
                } catch (e) {}
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
                if (msg.type === 'hello-ok') return;
                if (msg.type === 'waiting') {
                    setStatus(msg.message || 'Waiting for CutWire Prism desktop…');
                    return;
                }
                if (msg.type === 'error') {
                    setStatus('Error: ' + (msg.message || 'unknown'));
                    return;
                }
                if (msg.type === 'answer' && pc) {
                    await pc.setRemoteDescription({ type: 'answer', sdp: msg.sdp });
                    const track = previewStream.getVideoTracks()[0];
                    const settings = track ? track.getSettings() : {};
                    const size = (settings.width && settings.height)
                        ? (' (' + settings.width + '×' + settings.height + ')')
                        : '';
                    setStatus('Streaming to CutWire Prism' + size);
                } else if (msg.type === 'candidate' && pc && msg.candidate) {
                    await pc.addIceCandidate({ candidate: msg.candidate, sdpMid: msg.mid });
                }
            };
            ws.onerror = () => {
                setStatus('WebSocket error');
                teardownConnection();
                openPreview().catch(() => {});
            };
            ws.onclose = () => {
                setStatus('Disconnected');
                teardownConnection();
                openPreview().catch(() => {});
            };
        }

        cameraSelect.addEventListener('change', () => {
            openPreview().catch(err => setStatus('Could not switch camera: ' + err));
        });
        resolutionSelect.addEventListener('change', () => {
            openPreview().catch(err => setStatus('Could not change resolution: ' + err));
        });
        startBtn.addEventListener('click', start);
        initPage();
    </script>
</body>
</html>"""


@app.get("/health")
async def health() -> dict:
    return {"status": "ok", "rooms": len(rooms)}


@app.get("/")
async def root() -> PlainTextResponse:
    return PlainTextResponse("CutWire Prism WebRTC signaling relay\n")


@app.get("/cam")
async def cam_page() -> HTMLResponse:
    return HTMLResponse(CAM_PAGE_HTML)


@app.websocket("/ws")
async def websocket_relay(ws: WebSocket) -> None:
    await ws.accept()
    token: Optional[str] = None
    role: Optional[str] = None

    try:
        while True:
            raw = await ws.receive_text()
            try:
                msg = json.loads(raw)
            except json.JSONDecodeError:
                await ws.send_json({"type": "error", "message": "invalid json"})
                continue

            msg_type = msg.get("type")

            if msg_type == "hello":
                token = msg.get("token")
                role = msg.get("role", "phone")
                if not token or role not in ("phone", "desktop"):
                    await ws.send_json({"type": "error", "message": "invalid hello"})
                    continue

                room = rooms.setdefault(token, Room())
                if role == "desktop":
                    if room.desktop and room.desktop is not ws:
                        await room.desktop.close(code=4000, reason="replaced")
                    room.desktop = ws
                else:
                    if room.phone and room.phone is not ws:
                        await room.phone.close(code=4000, reason="replaced")
                    room.phone = ws

                log.info("hello token=%s role=%s rooms=%d", token[:8], role, len(rooms))
                await ws.send_json({"type": "hello-ok"})
                await _flush_pending(room, role)
                continue

            if not token or not role:
                await ws.send_json({"type": "error", "message": "send hello first"})
                continue

            room = rooms.get(token)
            if not room:
                continue

            await _relay(room, role, ws, raw, msg_type or "")

    except WebSocketDisconnect:
        pass
    finally:
        if token and token in rooms:
            room = rooms[token]
            if role:
                _clear_peer(room, role, ws)
            if room.phone is None and room.desktop is None:
                del rooms[token]
                log.info("room closed token=%s", token[:8])
