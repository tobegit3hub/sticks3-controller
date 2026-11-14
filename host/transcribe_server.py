#!/usr/bin/env python3
"""StickS3 v15 transcription server.

Receives the device's continuous 16 kHz mono PCM over WebSocket (the v15
pipeline), detects speech endpoints with WebRTC VAD, batch-transcribes each
utterance via a cloud ASR (SenseVoice or Qwen), lightly cleans the text, and
appends it to the clipboard while printing to the terminal.

This is the "dictation tool" front-end: talk into the stick, text appears on
the Mac. The VAD/ASR/clean logic is ported from the (now-retired) v12 app; only
the transport (HTTP->WebSocket) and UI (tkinter->terminal) changed.

Security: no auth, binds 0.0.0.0. Trusted LAN only.

Usage:
    conda activate stick
    python transcribe_server.py                 # needs ASR_KEY in .env
    python transcribe_server.py --no-clean
"""
import argparse
import asyncio
import base64
import json
import math
import os
import pathlib
import queue
import shutil
import struct
import subprocess
import threading
import time
import wave
from collections import deque
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime
from urllib import request as urlrequest

import websockets

# ASR endpoint, OpenAI-compatible /chat/completions. Provider-agnostic: point
# ASR_BASE_URL / ASR_MODEL / ASR_KEY at any compatible backend, no code changes.
# ASR_BASE_URL is the base URL; "/chat/completions" is appended at call time.
ASR_BASE_URL = "https://llm-twybn472bigenxpi.cn-beijing.maas.aliyuncs.com/compatible-mode/v1"
ASR_MODEL = "qwen3-asr-flash"
# Optional text-polish model on the same base URL. Polish is OFF unless the
# user sets CLEANUP_MODEL in .env (so a different ASR vendor that has no
# qwen-flash is never called with an unknown model name).
CLEANUP_MODEL = "qwen-flash"

SAMPLE_RATE = 16000
CHANNELS = 1
SAMPLE_WIDTH = 2
BYTES_PER_SECOND = SAMPLE_RATE * SAMPLE_WIDTH * CHANNELS
WEBRTC_FRAME_MS = 30
WEBRTC_FRAME_BYTES = int(BYTES_PER_SECOND * WEBRTC_FRAME_MS / 1000)

DICTATION_PROMPT = (
    "你是中文语音转写校对器，处理的是连续口述中的一个片段。请忠实保留用户表达，"
    "只做轻量清洗：删除嗯、啊、呃、就是、然后等明显填充词和重复起头；修正常见语音识别错字；"
    "把明显断错的短句合并成自然句子；补全中文标点。不要改变原意，不要扩写，"
    "不要总结，不要改成笔记格式。只输出校对后的正文。"
)


def load_env_file(path: pathlib.Path):
    if not path.exists():
        return
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip().strip('"').strip("'")
        if key and key not in os.environ:
            os.environ[key] = value


def ensure_env_file(env_file: pathlib.Path) -> pathlib.Path:
    """If .env is missing, copy the .env.example beside it. First run then tells
    the user exactly what to fill instead of failing cryptically."""
    if env_file.exists():
        return env_file
    example = env_file.with_name(".env.example")
    if example.exists():
        try:
            shutil.copy(example, env_file)
            print("[config] created %s from .env.example — fill in ASR_KEY and restart."
                  % env_file)
        except OSError:
            pass
    return env_file


def load_host_env(here: pathlib.Path) -> None:
    """Load .env from the same directory as this script (ble_client.py and .env
    sit together). Fresh run with no .env materializes the .env.example beside
    it so the "fill in ASR_KEY" hint shows instead of a cryptic failure."""
    env_file = here / ".env"
    ensure_env_file(env_file)
    load_env_file(env_file)


def set_clipboard_text(text: str) -> bool:
    """Cross-platform clipboard write (macOS/Windows/Linux via pyperclip)."""
    try:
        import pyperclip
        pyperclip.copy(text)
        return True
    except Exception as exc:
        print(f"[clipboard error] {exc}")
        return False


def write_wav(path: pathlib.Path, pcm: bytes, sample_rate: int = SAMPLE_RATE):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(CHANNELS)
        w.setsampwidth(SAMPLE_WIDTH)
        w.setframerate(sample_rate)
        w.writeframes(pcm)


def pcm_ac_rms(pcm: bytes) -> float:
    usable = len(pcm) - (len(pcm) % 2)
    if usable <= 0:
        return 0.0
    count = usable // 2
    samples = struct.unpack("<%dh" % count, pcm[:usable])
    mean = sum(samples) / count
    return math.sqrt(sum((s - mean) * (s - mean) for s in samples) / count)


def append_text(draft: str, segment: str) -> str:
    segment = segment.strip()
    if not segment:
        return draft
    if not draft:
        return segment
    sep = "" if draft.endswith(("，", "。", "？", "！", ",", ".", "?", "!", "\n")) else " "
    return draft + sep + segment


def join_segments(segments) -> str:
    draft = ""
    for segment in segments:
        draft = append_text(draft, segment)
    return draft


class SpeechDetector:
    """WebRTC VAD over 30ms sub-frames, gated by an RMS floor. Falls back to
    pure RMS if webrtcvad is unavailable."""

    def __init__(self, backend: str, speech_rms_threshold: float,
                 vad_aggressiveness: int, vad_ratio: float):
        self.backend = backend
        self.speech_rms_threshold = speech_rms_threshold
        self.vad_ratio = vad_ratio
        self.name = "rms"
        self._vad = None
        if backend in ("auto", "webrtc"):
            try:
                import warnings
                with warnings.catch_warnings():
                    warnings.simplefilter("ignore")
                    import webrtcvad
                self._vad = webrtcvad.Vad(max(0, min(3, vad_aggressiveness)))
                self.name = "webrtc"
            except Exception as exc:
                if backend == "webrtc":
                    raise RuntimeError("webrtcvad is not available") from exc

    def is_speech(self, pcm: bytes, rms: float):
        if self._vad is None:
            return rms >= self.speech_rms_threshold, 0.0
        voiced = total = 0
        usable = len(pcm) - (len(pcm) % WEBRTC_FRAME_BYTES)
        for offset in range(0, usable, WEBRTC_FRAME_BYTES):
            frame = pcm[offset:offset + WEBRTC_FRAME_BYTES]
            if len(frame) != WEBRTC_FRAME_BYTES:
                continue
            total += 1
            if self._vad.is_speech(frame, SAMPLE_RATE):
                voiced += 1
        if total == 0:
            return rms >= self.speech_rms_threshold, 0.0
        ratio = voiced / total
        return ratio >= self.vad_ratio and rms >= self.speech_rms_threshold, ratio


def asr_transcribe_wav(api_key: str, wav_path: pathlib.Path) -> str:
    wav_bytes = wav_path.read_bytes()
    data_uri = "data:audio/wav;base64," + base64.b64encode(wav_bytes).decode("ascii")
    body = {
        "model": os.environ.get("ASR_MODEL", ASR_MODEL),
        "messages": [{"role": "user", "content": [
            {"type": "input_audio", "input_audio": {"data": data_uri}}]}],
    }
    req = urlrequest.Request(
        os.environ.get("ASR_BASE_URL", ASR_BASE_URL).rstrip("/") + "/chat/completions",
        data=json.dumps(body, ensure_ascii=False).encode("utf-8"),
        headers={"Authorization": "Bearer " + api_key,
                 "Content-Type": "application/json"},
        method="POST")
    with urlrequest.urlopen(req, timeout=60) as resp:
        result = json.loads(resp.read().decode("utf-8"))
    return result["choices"][0]["message"].get("content", "").strip()


def polish_text(api_key: str, text: str, model: str,
                base_url: str = None) -> str:
    text = text.strip()
    if not text:
        return ""
    body = {
        "model": model,
        "messages": [{"role": "system", "content": DICTATION_PROMPT},
                     {"role": "user", "content": text}],
        "temperature": 0.1,
        "enable_thinking": False,
    }
    req = urlrequest.Request(
        (base_url or os.environ.get("ASR_BASE_URL", ASR_BASE_URL)).rstrip("/")
        + "/chat/completions",
        data=json.dumps(body, ensure_ascii=False).encode("utf-8"),
        headers={"Authorization": "Bearer " + api_key,
                 "Content-Type": "application/json"},
        method="POST")
    try:
        with urlrequest.urlopen(req, timeout=20) as resp:
            result = json.loads(resp.read().decode("utf-8"))
        return result["choices"][0]["message"].get("content", "").strip() or text
    except Exception as exc:
        print("[polish error]", exc)
        return text


class Segmenter:
    """Accumulates PCM chunks, cuts an utterance on a trailing silence (or a
    max length), and submits each cut to the thread pool for ASR. Results are
    emitted strictly in order, then the running draft is copied to the clipboard.
    Ported from v12 SegmentSession, minus the tkinter draft bookkeeping."""

    def __init__(self, cfg, folder: pathlib.Path, collect_full=False, tmp=False):
        """One recording session. `collect_full` additionally accumulates the raw
        PCM of the whole session and joins it into <folder>/memo.wav at finish()
        (memo notes keep one joined audio file instead of per-segment WAVs).
        `tmp` marks the folder as transient ASR staging (voice input) — the
        owner removes it once the trailing ASR has landed."""
        self.cfg = cfg
        self.folder = folder
        self.tmp = tmp
        self.detector = SpeechDetector(cfg.vad_backend, cfg.speech_rms,
                                       cfg.vad_aggressiveness, cfg.vad_ratio)
        # Full-session PCM, streamed through a temp file so a long memo never
        # buffers the whole recording in RAM. Joined into memo.wav in finish().
        self._full_pcm_path = None
        self._full_pcm_fh = None
        if collect_full:
            self._full_pcm_path = folder / ".session.pcm"
            self._full_pcm_fh = open(self._full_pcm_path, "wb")
        self.silence_ms_to_cut = cfg.silence_ms
        self.min_segment_bytes = int(BYTES_PER_SECOND * cfg.min_segment_seconds)
        self.max_segment_bytes = int(BYTES_PER_SECOND * cfg.max_segment_seconds)
        self.pre_roll_bytes = int(BYTES_PER_SECOND * cfg.pre_roll_seconds)
        self.buffer = bytearray()
        self.pre_roll = deque()
        self.pre_roll_len = 0
        self.speaking = False
        self.silence_ms = 0
        self.segment_index = 0
        self.next_emit_index = 0
        self.results = {}
        self.display_texts = []
        self.draft = ""
        self.bytes = 0
        self.started_at = time.time()
        self.lock = threading.Lock()

    def add_chunk(self, data: bytes):
        if self._full_pcm_fh:
            self._full_pcm_fh.write(bytes(data))  # memo: keep the whole session
        rms = pcm_ac_rms(data)
        chunk_ms = int((len(data) / BYTES_PER_SECOND) * 1000) if data else 0
        is_speech, _ = self.detector.is_speech(data, rms)
        submit = None
        reason = ""
        with self.lock:
            self.bytes += len(data)
            if not self.speaking:
                self._add_pre_roll(data)
                if is_speech:
                    self.speaking = True
                    self.silence_ms = 0
                    self.buffer.extend(self._consume_pre_roll())
            else:
                self.buffer.extend(data)
                if is_speech:
                    self.silence_ms = 0
                else:
                    self.silence_ms += chunk_ms
                if len(self.buffer) >= self.max_segment_bytes:
                    submit, reason = bytes(self.buffer), "max"
                elif (self.silence_ms >= self.silence_ms_to_cut
                      and len(self.buffer) >= self.min_segment_bytes):
                    submit, reason = bytes(self.buffer), "silence"
                if submit:
                    self.buffer.clear()
                    self.speaking = False
                    self.silence_ms = 0
            if submit:
                self._submit_segment(submit, reason)

    def _add_pre_roll(self, data: bytes):
        if not data or self.pre_roll_bytes <= 0:
            return
        self.pre_roll.append(data)
        self.pre_roll_len += len(data)
        while self.pre_roll_len > self.pre_roll_bytes and self.pre_roll:
            self.pre_roll_len -= len(self.pre_roll.popleft())

    def _consume_pre_roll(self) -> bytes:
        data = b"".join(self.pre_roll)
        self.pre_roll.clear()
        self.pre_roll_len = 0
        return data

    def _submit_segment(self, pcm: bytes, reason: str):
        if not pcm:
            return
        idx = self.segment_index
        self.segment_index += 1
        seg_dir = self.folder / "segments"
        seg_dir.mkdir(parents=True, exist_ok=True)
        wav_path = seg_dir / ("segment_%04d.wav" % idx)
        write_wav(wav_path, pcm)
        seconds = len(pcm) / BYTES_PER_SECOND
        print("[segment] #%d %.1fs (%s) -> ASR..." % (idx, seconds, reason))
        self.cfg.executor.submit(self._transcribe, idx, wav_path)

    def _transcribe(self, index: int, wav_path: pathlib.Path):
        try:
            start = time.time()
            raw = self.cfg.transcribe(wav_path)
            asr_elapsed = time.time() - start
            clean, clean_elapsed = raw, 0.0
            if self.cfg.clean_segments and self.cfg.cleanup_key and raw.strip():
                c0 = time.time()
                clean = polish_text(self.cfg.cleanup_key, raw, self.cfg.cleanup_model,
                                    self.cfg.cleanup_base_url)
                clean_elapsed = time.time() - c0
            print("[asr] #%d asr=%.1fs clean=%.1fs | %s" %
                  (index, asr_elapsed, clean_elapsed, clean))
            self._on_result(index, clean)
        except Exception as exc:
            print("[asr error] #%d %s" % (index, exc))
            self._on_result(index, "")

    def _on_result(self, index: int, text: str):
        new_segments = []
        with self.lock:
            self.results[index] = text
            while self.next_emit_index in self.results:
                seg = self.results.pop(self.next_emit_index)
                self.display_texts.append(seg)
                if seg.strip():
                    new_segments.append(seg)
                self.next_emit_index += 1
            self.draft = join_segments(self.display_texts)
            (self.folder / "transcript.txt").write_text(self.draft, encoding="utf-8")
        if self.draft:
            set_clipboard_text(self.draft)
            print("[draft] %s" % self.draft)
            # Optional live-draft hook (v16 GUI window). Absent in the v15 CLI.
            cb = getattr(self.cfg, "draft_callback", None)
            if cb:
                cb(self.draft)
        # Per-segment hook (v17 cursor injection): fire once per NEW segment so we
        # inject only the fresh text, not the whole accumulated draft.
        seg_cb = getattr(self.cfg, "segment_callback", None)
        if seg_cb:
            for seg in new_segments:
                seg_cb(seg)
        self.cfg.ui_push({"kind": "draft", "text": self.draft})

    def reset(self):
        """Drop any half-filled utterance state at the start of a new recording
        (BtnA press) so a fresh session never merges with stale audio. The draft
        text is kept — it accumulates across sessions on one connection."""
        with self.lock:
            self.buffer.clear()
            self.pre_roll.clear()
            self.pre_roll_len = 0
            self.speaking = False
            self.silence_ms = 0

    def clear(self):
        """Clear button: wipe the accumulated draft and transcript file."""
        with self.lock:
            self.display_texts = []
            self.results.clear()
            self.next_emit_index = self.segment_index
            self.draft = ""
            (self.folder / "transcript.txt").write_text("", encoding="utf-8")
        self.cfg.ui_push({"kind": "draft", "text": ""})

    def finish(self):
        with self.lock:
            tail = bytes(self.buffer)
            self.buffer.clear()
        if tail:
            self._submit_segment(tail, "stop")
        self._finalize_full_audio()

    def _finalize_full_audio(self):
        """Join the collected session PCM into <folder>/memo.wav and drop the
        temp PCM stream. Idempotent, so calling finish() twice is harmless."""
        fh, path = self._full_pcm_fh, self._full_pcm_path
        if fh is None or path is None:
            return
        self._full_pcm_fh = None
        self._full_pcm_path = None
        try:
            fh.close()
            if path.exists() and path.stat().st_size > 0:
                write_wav(self.folder / "memo.wav", path.read_bytes())
                print("[joined] whole-session audio -> %s"
                      % (self.folder / "memo.wav"))
            path.unlink(missing_ok=True)
        except OSError as exc:
            print("[joined wav error]", exc)

    def cleanup(self):
        """Remove the transient per-segment WAVs now that the trailing ASR has
        landed. The session's joined memo.wav and transcript.txt are kept."""
        seg_dir = self.folder / "segments"
        if seg_dir.is_dir():
            try:
                shutil.rmtree(seg_dir)
                print("[cleanup] dropped transient segments in %s" % seg_dir)
            except OSError as exc:
                print("[cleanup error]", exc)


class Config:
    """Runtime config + shared executor + ASR dispatch.

    ASR is configured by ASR_KEY / ASR_BASE_URL / ASR_MODEL env vars (loaded
    from .env before Config is built), so any OpenAI-compatible backend works.
    """

    def __init__(self, args, asr_key, clean_segments):
        self.asr_key = asr_key
        self.cleanup_key = os.environ.get("CLEANUP_KEY", "").strip() or asr_key
        self.cleanup_base_url = os.environ.get("CLEANUP_BASE_URL", "").strip()
        self.asr_model = os.environ.get("ASR_MODEL", ASR_MODEL)
        # Text polish only runs when CLEANUP_MODEL is explicitly set in .env.
        # Blank = no polish at all: a different ASR vendor that has no
        # qwen-flash is never called with a model name it doesn't know.
        self.cleanup_model = os.environ.get("CLEANUP_MODEL", "").strip()
        self.clean_segments = clean_segments and bool(self.cleanup_model)
        self.vad_backend = args.vad_backend
        self.vad_aggressiveness = args.vad_aggressiveness
        self.vad_ratio = args.vad_ratio
        self.speech_rms = args.speech_rms
        self.silence_ms = args.silence_ms
        self.min_segment_seconds = args.min_segment_seconds
        self.max_segment_seconds = args.max_segment_seconds
        self.pre_roll_seconds = args.pre_roll_seconds
        self.out_dir = pathlib.Path(args.out).resolve()
        self.out_dir.mkdir(parents=True, exist_ok=True)
        self.executor = ThreadPoolExecutor(max_workers=args.workers)
        # UI plumbing (set by main when running with a window). ui_queue receives
        # dicts the tkinter loop drains; active_segmenter lets the Clear button
        # reach whatever Segmenter the current connection owns.
        self.ui_queue = None
        self.active_segmenter = None
        # v17 cursor injection: called once per finalized segment with its text.
        self.segment_callback = None

    def ui_push(self, event: dict):
        if self.ui_queue is not None:
            self.ui_queue.put(event)

    def clear_draft(self):
        seg = self.active_segmenter
        if seg is not None:
            seg.clear()

    def transcribe(self, wav_path: pathlib.Path) -> str:
        return asr_transcribe_wav(self.asr_key, wav_path)


def server_hello():
    return json.dumps({
        "type": "hello", "transport": "websocket",
        "session_id": datetime.now().strftime("%Y%m%d_%H%M%S"),
        "audio_params": {"format": "pcm", "sample_rate": SAMPLE_RATE,
                         "channels": CHANNELS},
    })


async def make_handler(cfg):
    async def handle(ws):
        peer = ws.remote_address
        folder = cfg.out_dir / datetime.now().strftime("%Y-%m-%d") / "ws" \
            / datetime.now().strftime("%H%M%S")
        folder.mkdir(parents=True, exist_ok=True)
        seg = Segmenter(cfg, folder)
        cfg.active_segmenter = seg
        print("[+] connected %s -> %s" % (peer, folder))
        cfg.ui_push({"kind": "status", "connected": True, "recording": False})
        loop = asyncio.get_event_loop()
        try:
            async for msg in ws:
                if isinstance(msg, bytes):
                    # VAD is cheap but runs sync; offload so we never stall the
                    # WebSocket read loop (would back-pressure the device queue).
                    await loop.run_in_executor(None, seg.add_chunk, msg)
                else:
                    data = json.loads(msg)
                    mtype = data.get("type")
                    if mtype == "hello":
                        print("    device hello:", data.get("audio_params"))
                        await ws.send(server_hello())
                    elif mtype == "listen":
                        state = data.get("state")
                        if state == "start":
                            # BtnA pressed: fresh utterance session.
                            seg.reset()
                            print("[listen] start")
                            cfg.ui_push({"kind": "status", "connected": True,
                                         "recording": True})
                        elif state == "stop":
                            # BtnA pressed again: flush the tail through ASR.
                            await loop.run_in_executor(None, seg.finish)
                            print("[listen] stop")
                            cfg.ui_push({"kind": "status", "connected": True,
                                         "recording": False})
        except websockets.ConnectionClosed:
            pass
        finally:
            seg.finish()
            elapsed = time.time() - seg.started_at
            print("[-] closed %s (%.1fs, %d bytes)" % (peer, elapsed, seg.bytes))
            if seg.draft:
                print("[final draft] %s" % seg.draft)
            if cfg.active_segmenter is seg:
                cfg.active_segmenter = None
            cfg.ui_push({"kind": "status", "connected": False, "recording": False})
    return handle


async def amain(cfg, host, port):
    handler = await make_handler(cfg)
    print("StickS3 v15 transcription server on ws://%s:%d/  (%dHz mono)"
          % (host, port, SAMPLE_RATE))
    print("ASR model: %s | clean: %s | silence_cut: %dms | seg: %.0f-%.0fs"
          % (cfg.asr_model, cfg.clean_segments, cfg.silence_ms,
             cfg.min_segment_seconds, cfg.max_segment_seconds))
    print("Result is copied to clipboard after each utterance. Paste with Cmd+V.\n")
    async with websockets.serve(handler, host, port, max_size=None):
        await asyncio.Future()


class DraftWindow:
    """Minimal tkinter window: status line, live transcript, action buttons.
    Runs on the main thread; the asyncio server runs in a daemon thread and
    communicates via cfg.ui_queue (thread-safe). Polled with root.after."""

    def __init__(self, cfg):
        import tkinter as tk
        self.tk = tk
        self.cfg = cfg
        self.root = tk.Tk()
        self.root.title("StickS3 转写")
        self.root.geometry("460x420")
        self.status = tk.Label(self.root, text="● 等待设备…", anchor="w",
                               font=("Helvetica", 15, "bold"), fg="#888")
        self.status.pack(fill="x", padx=10, pady=(10, 4))
        self.text = tk.Text(self.root, wrap="word", font=("Helvetica", 14),
                            height=14, undo=True)
        self.text.pack(fill="both", expand=True, padx=10, pady=4)
        bar = tk.Frame(self.root)
        bar.pack(fill="x", padx=10, pady=(0, 10))
        for label, cmd in (("复制", self._copy), ("粘贴到前台", self._paste),
                           ("清空", self._clear)):
            tk.Button(bar, text=label, command=cmd).pack(side="left", padx=4)
        self.root.after(100, self._poll)

    def _poll(self):
        try:
            while True:
                ev = self.cfg.ui_queue.get_nowait()
                self._apply(ev)
        except queue.Empty:
            pass
        self.root.after(100, self._poll)

    def _apply(self, ev):
        if ev["kind"] == "status":
            if not ev.get("connected"):
                self.status.config(text="● 设备离线", fg="#888")
            elif ev.get("recording"):
                self.status.config(text="● 录音中…", fg="#e03030")
            else:
                self.status.config(text="● 就绪 (按 BtnA 开始)", fg="#30a030")
        elif ev["kind"] == "draft":
            self.text.delete("1.0", self.tk.END)
            self.text.insert("1.0", ev["text"])
            self.text.see(self.tk.END)

    def _copy(self):
        set_clipboard_text(self.text.get("1.0", self.tk.END).strip())

    def _paste(self):
        self._copy()
        # Cmd+V into the frontmost app (needs Accessibility permission for
        # Terminal/Python once). Falls back silently if not granted.
        subprocess.run(["osascript", "-e",
                        'tell application "System Events" to keystroke "v" using command down'],
                       capture_output=True)

    def _clear(self):
        self.cfg.clear_draft()
        self.text.delete("1.0", self.tk.END)

    def run(self):
        self.root.mainloop()


def main():
    here = pathlib.Path(__file__).resolve().parent
    load_host_env(here)

    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int,
                        default=int(os.environ.get("MAC_PORT", "8766")))
    parser.add_argument("--out", default="transcripts")
    parser.add_argument("--vad-backend", choices=("auto", "webrtc", "rms"), default="auto")
    parser.add_argument("--vad-aggressiveness", type=int, default=2)
    parser.add_argument("--vad-ratio", type=float, default=0.50)
    parser.add_argument("--speech-rms", type=float, default=1200.0)
    parser.add_argument("--silence-ms", type=int, default=800)
    parser.add_argument("--min-segment-seconds", type=float, default=3.0)
    parser.add_argument("--max-segment-seconds", type=float, default=14.0)
    parser.add_argument("--pre-roll-seconds", type=float, default=0.5)
    parser.add_argument("--workers", type=int, default=3)
    parser.add_argument("--no-clean", action="store_true")
    parser.add_argument("--no-ui", action="store_true",
                        help="headless: terminal only, no tkinter window")
    args = parser.parse_args()

    asr_key = os.environ.get("ASR_KEY", "").strip()
    if not asr_key:
        raise SystemExit("Missing ASR_KEY (put it in the package .env).")
    clean_segments = not args.no_clean
    if clean_segments and not asr_key:
        clean_segments = False
        print("Clean disabled: no ASR_KEY for cleanup.")

    cfg = Config(args, asr_key, clean_segments)

    if args.no_ui:
        try:
            asyncio.run(amain(cfg, args.host, args.port))
        except KeyboardInterrupt:
            print("\nbye")
        finally:
            cfg.executor.shutdown(wait=False, cancel_futures=True)
        return

    # Windowed mode: asyncio server on a daemon thread, tkinter on the main
    # thread (macOS requires GUI on the main thread). They share cfg.ui_queue.
    cfg.ui_queue = queue.Queue()
    server_ready = threading.Event()

    def run_server():
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        server_ready.set()
        try:
            loop.run_until_complete(amain(cfg, args.host, args.port))
        except Exception as exc:
            print("[server thread stopped] %s" % exc)

    threading.Thread(target=run_server, daemon=True).start()
    server_ready.wait()
    try:
        DraftWindow(cfg).run()
    finally:
        cfg.executor.shutdown(wait=False, cancel_futures=True)


if __name__ == "__main__":
    main()
