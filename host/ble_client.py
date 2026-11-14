#!/usr/bin/env python3
"""StickS3 v17 BLE client with cursor injection.

v17 = v16's audio streaming + cursor text injection. The device streams Opus over BLE,
we decode/transcribe, then inject the recognized text at the active cursor position
(clipboard + Cmd+V on macOS) instead of just displaying in a window.

Buttons (Controller spec):
  A short = memo note (press to start, press to stop)
  A long  = voice input (hold to talk, release to stop)
  B short = confirm/send (simulates Enter)
  B long  = wake the app configured in .env via open -a (0.5s)

Frame protocol (matches firmware):
  audio char (notify): [2-byte big-endian seq][opus frame bytes]
  control char (notify): 1 byte, 1=voice start, 0=voice stop, 2=confirm (B short),
                         3=wake app (B long), 4=memo start, 5=memo stop

Usage:
    conda activate stick
    pip install -r requirements.txt    # bleak, opuslib, pyperclip, pyautogui
    python ble_client.py
"""
import argparse
import asyncio
import ctypes.util
import os
import pathlib
import platform
import shutil
import struct
import threading
import time
from datetime import datetime

# Transcripts are organized by day (transcripts/YYYY-MM-DD/...). Under each day:
#   dictation/dictation.md   — voice log, one "## HH:MM:SS" entry per voice
#                              session (text only, no audio). Trimmed by entry
#                              count once it exceeds VOICE_KEEP_ENTRIES.
#   memo/memo.md             — joined memo digest for the day (never trimmed).
#   memo/HHMMSS/             — one session folder per memo: a joined memo.wav
#                              (whole-session audio) + transcript.txt.
# Voice sessions never persist a folder — their segment WAVs are transient ASR
# staging under transcripts/.tmp/ and are removed when the log entry flushes.
# Memo is kept forever; only the voice log drops its oldest entries.
VOICE_KEEP_ENTRIES = 200


def load_opus():
    """Return an opus module (with Decoder/Encoder), trying two sources in order:

    1. opuslib_next (opuslib-next-bundled): ships a prebuilt libopus binary for
       Windows/macOS/Linux inside the wheel — `pip install` and it just works,
       no system library, no manual opus.dll. This is the preferred path.
    2. opuslib + system libopus: fallback if the bundled package isn't present.
       Patches find_library to locate Homebrew dylibs (macOS) or opus.dll
       (Windows, from cwd/PATH) since the default search often misses them.

    Same API either way (opuslib_next is an API-compatible fork of opuslib), so
    callers just use the returned module's .Decoder/.Encoder."""
    # Path 1: bundled binary, no system dependency. NOTE: on Windows the bundled
    # wheel sometimes ships without the opus binary, in which case `import
    # opuslib_next` raises a plain Exception (not ImportError) at import time —
    # catch broadly so we still fall through to the system-libopus path below.
    try:
        import opuslib_next
        return opuslib_next
    except Exception:
        pass

    # Path 2: system libopus. Patch find_library for locations it tends to miss.
    if not ctypes.util.find_library("opus"):
        candidates = []
        if platform.system() == "Darwin":
            candidates = ["/opt/homebrew/lib/libopus.dylib",   # Apple Silicon
                          "/usr/local/lib/libopus.dylib"]      # Intel Homebrew
        elif platform.system() == "Windows":
            candidates = [os.path.join(os.getcwd(), "opus.dll"), "opus.dll"]
        for cand in candidates:
            if os.path.exists(cand):
                _orig = ctypes.util.find_library

                def _patched(name, _orig=_orig, _cand=cand):
                    return _cand if name == "opus" else _orig(name)

                ctypes.util.find_library = _patched
                break

    import opuslib
    return opuslib

from bleak import BleakClient, BleakScanner

# --- Windows-only BLE workaround -------------------------------------------
# On some Windows machines Bleak's WinRT backend fails in start() at
# `radio = await adapter.get_radio_async()` with
# `OSError: [WinError -2147023728] 找不到元素` — the Radio enumeration returns
# empty even though the BLE adapter and hardware work fine. We (a) scan via a
# direct WinRT BluetoothLEAdvertisementWatcher, and (b) monkey-patch Bleak's
# scanner start() to skip the radio check so BleakClient can still connect.
# All of this is guarded by platform.system()=="Windows" — macOS/Linux are
# untouched and keep using plain Bleak.
_WINRT_SCANNER = False
if platform.system() == "Windows":
    try:
        from winrt.windows.devices.bluetooth.advertisement import (
            BluetoothLEAdvertisementWatcher,
            BluetoothLEScanningMode,
        )
        _WINRT_SCANNER = True
    except ImportError:
        pass


def _patch_bleak_winrt():
    """Replace the WinRT scanner's start() to skip the failing radio check."""
    try:
        from bleak.backends.winrt.scanner import (
            BleakScannerWinRT,
            BluetoothLEAdvertisementWatcher as _BleakWatcher,
            BluetoothAdapter as _BleakAdapter,
            BluetoothLEAdvertisementWatcherStatus,
            assert_mta,
            BleakError,
            BleakBluetoothNotAvailableError,
            BleakBluetoothNotAvailableReason,
            asyncio as _bleak_asyncio,
            logger,
        )

        async def _patched_start(self):
            if self.watcher:
                raise BleakError("Scanner already started")
            await assert_mta()

            adapter = await _BleakAdapter.get_default_async()
            if adapter is None:
                raise BleakBluetoothNotAvailableError(
                    "No Bluetooth adapter found",
                    BleakBluetoothNotAvailableReason.NO_BLUETOOTH,
                )
            if not adapter.is_central_role_supported:
                raise BleakBluetoothNotAvailableError(
                    "BLE 'central' role not supported on this adapter",
                    BleakBluetoothNotAvailableReason.NO_BLE_CENTRAL_ROLE,
                )
            # Skip get_radio_async() — fails on some machines where the Radio API
            # returns empty even though BLE works. Adapter exists; just proceed.
            self.seen_devices = {}
            self._advertisement_pairs.clear()

            self.watcher = _BleakWatcher()
            self.watcher.scanning_mode = self._scanning_mode
            try:
                self.watcher.allow_extended_advertisements = True
            except AttributeError:
                logger.warning("Extended advertisements not available on this OS.")

            event_loop = _bleak_asyncio.get_running_loop()
            self._stopped_event = _bleak_asyncio.Event()

            def on_received(sender, args):
                event_loop.call_soon_threadsafe(self._received_handler, sender, args)

            self._received_token = self.watcher.add_received(on_received)

            def on_stopped(sender, args):
                event_loop.call_soon_threadsafe(self._stopped_handler, sender, args)

            self._stopped_token = self.watcher.add_stopped(on_stopped)

            if self._signal_strength_filter is not None:
                self.watcher.signal_strength_filter = self._signal_strength_filter
            if self._advertisement_filter is not None:
                self.watcher.advertisement_filter = self._advertisement_filter

            self.watcher.start()
            while self.watcher.status == BluetoothLEAdvertisementWatcherStatus.CREATED:
                await _bleak_asyncio.sleep(0.01)
            if self.watcher.status == BluetoothLEAdvertisementWatcherStatus.ABORTED:
                raise BleakError("Failed to start scanner. Is Bluetooth turned on?")
            if self.watcher.status != BluetoothLEAdvertisementWatcherStatus.STARTED:
                raise BleakError("Unexpected watcher status: %s"
                                 % self.watcher.status.name)

        BleakScannerWinRT.start = _patched_start
        print("[bleak] patched WinRT scanner (skipped radio check)")
    except Exception as e:
        print("[bleak] patch failed: %s" % e)


if platform.system() == "Windows":
    _patch_bleak_winrt()


def _int_to_mac(addr_int: int) -> str:
    """WinRT BluetoothAddress (uint64) -> MAC string 'XX:XX:XX:XX:XX:XX'."""
    return ":".join(
        reversed([f"{(addr_int >> (i * 8)) & 0xFF:02X}" for i in range(6)]))


# Reuse the whole v15 dictation pipeline unchanged.
from transcribe_server import (
    Config, Segmenter, load_host_env, SAMPLE_RATE, CHANNELS,
)

try:
    import pyperclip
    from pynput.keyboard import Controller, Key
    _kbd = Controller()
    _INJECTION_AVAILABLE = True
except ImportError:
    _INJECTION_AVAILABLE = False
    _kbd = None
    print("[warn] pyperclip/pynput not installed; cursor injection disabled")

# Paste modifier: Cmd on macOS, Ctrl on Windows/Linux.
_PASTE_MODIFIER = Key.cmd if (_INJECTION_AVAILABLE and platform.system() == "Darwin") else (
    Key.ctrl if _INJECTION_AVAILABLE else None)

# Must match the firmware UUIDs (main.cpp kSvcUuid/kAudioUuid/kCtrlUuid).
SVC_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
AUDIO_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
CTRL_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

DEFAULT_NAME = "StickS3-Mic"
OPUS_FRAME_SAMPLES = SAMPLE_RATE * 60 // 1000  # 960 samples per 60ms frame


# ========== Cursor Injection (cross-platform via pynput) ==========
_last_injected = {"text": "", "ts": 0}


def inject_text_at_cursor(text: str):
    """Inject text at the active cursor (clipboard + Cmd/Ctrl+V). Deduplicates."""
    if not _INJECTION_AVAILABLE:
        print(f"[inject skip] {text}")
        return
    text = text.strip()
    if not text:
        return
    # Dedupe: if same text injected <2s ago, skip.
    now = time.time()
    if _last_injected["text"] == text and now - _last_injected["ts"] < 2.0:
        print(f"[inject dup] {text}")
        return
    _last_injected["text"] = text
    _last_injected["ts"] = now
    try:
        pyperclip.copy(text)
        time.sleep(0.1)  # let clipboard settle
        with _kbd.pressed(_PASTE_MODIFIER):
            _kbd.press("v")
            _kbd.release("v")
        print(f"[inject] {text}")
    except Exception as exc:
        print(f"[inject error] {exc}")


def send_enter():
    """Simulate pressing Enter (for B button confirm)."""
    if not _INJECTION_AVAILABLE:
        return
    try:
        _kbd.press(Key.enter)
        _kbd.release(Key.enter)
        print("[inject] Enter")
    except Exception as exc:
        print(f"[inject enter error] {exc}")


def wake_app(cfg=None):
    """Wake the app configured in .env (B long-press, control cmd=3).

    Runs wakers.wake(), which does `open -a <LINKO_WAKE_APP in .env>` — fill in
    whichever app you want to wake. Imported lazily so a missing/broken waker
    never blocks startup."""
    from wakers import wake as wake_plugin
    wake_plugin()


class AudioLink:
    """Decodes incoming Opus notifications to PCM and drives one Segmenter per
    recording session. control start opens a session folder; stop flushes the
    tail so the last utterance gets transcribed."""

    def __init__(self, cfg, status_cb=None, inject_mode=True):
        opus = load_opus()  # prefers bundled binary, falls back to system libopus
        self.cfg = cfg
        self.status_cb = status_cb or (lambda _t: None)
        self.inject_ok = inject_mode and _INJECTION_AVAILABLE
        self.mode = "voice"  # "voice" (cursor inject) or "memo" (saved note)
        self.decoder = opus.Decoder(SAMPLE_RATE, CHANNELS)
        self.seg = None
        self.expected_seq = None
        self.lost = 0
        # Segments from the current memo session accumulate here and are joined
        # into ONE memo.md entry when the session stops. Voice mode never uses it.
        self._memo_buf = []
        self._voice_buf = []  # finalized voice segments for today's dictation log
        self._memo_day = datetime.now().strftime("%Y-%m-%d")
        self._purge_tmp()
        # Route finalized segments through our dispatcher: voice -> cursor
        # inject, memo -> accumulate for one joined memo entry (never touches
        # the cursor or the clipboard).
        cfg.segment_callback = self._on_segment
        self._open_session()  # allow audio before an explicit start

    def _on_segment(self, text: str):
        """Dispatch a finalized transcription segment by current mode."""
        if self.mode == "memo":
            if text and text.strip():
                self._memo_buf.append(text)
            # Memo also types at the cursor, like voice input — the note is both
            # saved to memo/memo.md (on stop) and shown where the user works.
            if self.inject_ok:
                inject_text_at_cursor(text)
            return
        # Voice: inject at the cursor and keep a copy for the dictation log.
        if text and text.strip():
            self._voice_buf.append(text)
        if self.inject_ok:
            inject_text_at_cursor(text)

    def _flush_memo(self, seg=None):
        """Join the accumulated memo segments into one entry and append it to
        memo/memo.md, then drop the session's transient segment WAVs. Called on
        memo stop (delayed a bit for async ASR to finish trailing segments), on
        session switch, and on disconnect. `seg` is the session just stopped."""
        buf = [t for t in self._memo_buf if t and t.strip()]
        self._memo_buf = []
        if buf:
            # One recording session -> one joined paragraph. Join with "" so
            # Chinese text stays seamless; switch to " ".join if English-first.
            text = "".join(buf)
            memo_dir = self.cfg.out_dir / self._memo_day / "memo"
            memo_dir.mkdir(parents=True, exist_ok=True)
            memo_file = memo_dir / "memo.md"
            ts = datetime.now().strftime("%Y-%m-%d %H:%M")
            try:
                with memo_file.open("a", encoding="utf-8") as fh:
                    fh.write("\n## %s\n%s\n" % (ts, text))
            except OSError as exc:
                print("[memo write error]", exc)
            print("[memo] saved %d chars -> %s" % (len(text), memo_file))
        self._finalize_session(seg)

    def _finalize_session(self, seg=None):
        """After a session's trailing ASR has landed: remove its transient
        segment WAVs, and drop the session folder if it produced nothing."""
        seg = seg or getattr(self, "seg", None)
        if seg is None:
            return
        seg.cleanup()
        try:
            if seg.folder.is_dir() and not any(seg.folder.iterdir()):
                seg.folder.rmdir()
        except OSError:
            pass

    def _open_session(self):
        # Per-day layout: transcripts/YYYY-MM-DD/dictation/ holds the voice log
        # (dictation.md), transcripts/YYYY-MM-DD/memo/ holds memo/memo.md plus
        # one memo/HHMMSS/ session folder (joined memo.wav + transcript.txt).
        # Voice keeps no persistent folder — its segment WAVs are ASR staging in
        # .tmp/voice_HHMMSS and are removed once the log entry is flushed.
        day = datetime.now().strftime("%Y-%m-%d")
        stamp = datetime.now().strftime("%H%M%S")
        if self.mode == "voice":
            self._voice_buf = []  # one buffer per voice session
            folder = self._unique_dir(self.cfg.out_dir / ".tmp" / ("voice_" + stamp))
        else:
            folder = self._unique_dir(self.cfg.out_dir / day / "memo" / stamp)
        folder.mkdir(parents=True, exist_ok=True)
        self.seg = Segmenter(self.cfg, folder,
                             collect_full=(self.mode == "memo"),
                             tmp=(self.mode == "voice"))
        self.expected_seq = None
        print("[session] %s -> %s" % (self.mode, folder))

    def _unique_dir(self, base: pathlib.Path) -> pathlib.Path:
        """Bump a HHMMSS name if a same-second session already used it."""
        p, i = base, 2
        while p.exists():
            p = base.with_name("%s_%d" % (base.name, i))
            i += 1
        return p

    def _purge_tmp(self):
        """Clear leftover ASR staging folders from a previous run (startup only,
        so no session's ASR can still be reading them)."""
        tmp_root = self.cfg.out_dir / ".tmp"
        if not tmp_root.is_dir():
            return
        for stale in tmp_root.iterdir():
            try:
                if stale.is_dir():
                    shutil.rmtree(stale)
            except OSError:
                pass

    def _flush_voice(self, seg=None, buf=None):
        """Append one voice session's text to today's dictation log, then remove
        its transient ASR folder. `seg`/`buf` are captured at stop time so a new
        session opening before the delayed flush never mixes sessions."""
        buf = buf if buf is not None else getattr(self, "_voice_buf", [])
        text = "".join(t for t in buf if t and t.strip())
        if text:
            self._append_voice_log(text)
        if seg is not None and getattr(seg, "tmp", False):
            try:
                shutil.rmtree(seg.folder)
            except OSError as exc:
                print("[voice tmp cleanup error]", exc)

    def _append_voice_log(self, text: str):
        """Append one voice session to today's dictation log, then trim the log
        to the newest VOICE_KEEP_ENTRIES entries across all days."""
        log_dir = self.cfg.out_dir / datetime.now().strftime("%Y-%m-%d") / "dictation"
        log_dir.mkdir(parents=True, exist_ok=True)
        log_file = log_dir / "dictation.md"
        ts = datetime.now().strftime("%H:%M:%S")
        try:
            with log_file.open("a", encoding="utf-8") as fh:
                fh.write("## %s\n%s\n\n" % (ts, text))
            print("[dictation] logged %d chars -> %s" % (len(text), log_file))
        except OSError as exc:
            print("[dictation write error]", exc)
        self._trim_voice_log()

    def _trim_voice_log(self, keep: int = VOICE_KEEP_ENTRIES):
        """Queue-style retention for the voice log only (memo is never trimmed):
        keep the newest `keep` "## HH:MM:SS" entries across all day folders,
        dropping the oldest by rewriting the files that hold them."""
        if keep <= 0:
            return
        files = sorted(self.cfg.out_dir.glob("*/dictation/dictation.md"))
        # The file is a sequence of "## HH:MM:SS\nTEXT\n\n" entries. Splitting on
        # "\n\n## " (the boundary between entries) keeps every entry: parts[0]
        # already carries the file's leading "## ", later parts get it back from
        # the separator on rewrite. (Splitting on "\n## " instead would lose the
        # first entry into parts[0] and undercount by one.)
        entries = []  # (day, index-within-file, path, entry-text)
        for f in files:
            try:
                content = f.read_text(encoding="utf-8").strip()
            except OSError:
                continue
            parts = content.split("\n\n## ")
            day = f.parent.parent.name
            for i, body in enumerate(parts):
                if body.strip():
                    entries.append((day, i, f, body))
        if len(entries) <= keep:
            return
        entries.sort(key=lambda e: (e[0], e[1]))  # oldest day/entry first
        dropped = entries[:len(entries) - keep]
        by_file = {}
        for _day, i, f, _text in dropped:
            by_file.setdefault(f, []).append(i)
        for f, idxs in by_file.items():
            drop_idx = set(idxs)
            try:
                parts = f.read_text(encoding="utf-8").strip().split("\n\n## ")
                # Only parts[0] carries the file's leading "## " header; later
                # parts get it back from the "\n\n## " separator. Normalize the
                # header out so "## " always comes from the join prefix — that
                # way the new first entry keeps its header even when parts[0]
                # itself was the dropped one.
                kept = []
                for i, b in enumerate(parts):
                    if i in drop_idx:
                        continue
                    if i == 0 and b.startswith("## "):
                        b = b[3:]
                    kept.append(b)
                new_text = ("## " + "\n\n## ".join(kept)).rstrip() + "\n"
                if kept:
                    f.write_text(new_text, encoding="utf-8")
                else:
                    f.unlink(missing_ok=True)
                try:
                    f.parent.rmdir()  # drop now-empty dictation/ dirs
                except OSError:
                    pass
            except OSError as exc:
                print("[dictation trim error]", exc)

    def on_audio(self, _sender, data: bytearray):
        if self.seg is None:
            return  # no active session (e.g. trailing BLE audio after a stop)
        if len(data) < 3:
            return
        seq = struct.unpack(">H", bytes(data[:2]))[0]
        if self.expected_seq is not None and seq != self.expected_seq:
            gap = (seq - self.expected_seq) & 0xFFFF
            self.lost += gap
            print("[loss] seq jump %d->%d (+%d, total %d)"
                  % (self.expected_seq, seq, gap, self.lost))
        self.expected_seq = (seq + 1) & 0xFFFF
        try:
            pcm = self.decoder.decode(bytes(data[2:]), OPUS_FRAME_SAMPLES)
        except Exception as exc:
            print("[opus decode error]", exc)
            return
        self.seg.add_chunk(pcm)

    def on_control(self, _sender, data: bytearray):
        if not data:
            return
        cmd = data[0]
        if cmd == 1:  # voice start (A long-press, hold-to-talk)
            print("[ctrl] voice start")
            self.status_cb("● 录音中…")
            self.mode = "voice"
            if self.seg and self.seg.bytes > 0:
                self.seg.finish()
            self._open_session()
        elif cmd == 0:  # voice stop (A release)
            print("[ctrl] voice stop")
            self.status_cb("■ 已停止，转写中…")
            seg, buf = self.seg, self._voice_buf
            if seg:
                seg.finish()
            # ASR is async; trailing segments land after finish(). Log the voice
            # session and remove its staging folder shortly after, so a fast
            # re-press never mixes the two sessions.
            threading.Timer(2.5, self._flush_voice, args=(seg, buf)).start()
            # Session is closed — drop any straggling audio so it can't spawn a
            # segment after the staging folder is removed (see memo/162643 leak).
            self.seg = None
        elif cmd == 2:  # confirm (B short)
            print("[ctrl] confirm (B button)")
            send_enter()
        elif cmd == 3:  # wake app (B long)
            print("[ctrl] wake app (B long-press)")
            wake_app(self.cfg)
        elif cmd == 4:  # memo start (A short)
            print("[ctrl] memo start")
            self.status_cb("◎ 碎碎念中…")
            self._flush_memo()  # any previous memo that didn't get a clean stop
            self.mode = "memo"
            self._memo_buf = []
            self._memo_day = datetime.now().strftime("%Y-%m-%d")
            if self.seg and self.seg.bytes > 0:
                self.seg.finish()
            self._open_session()
        elif cmd == 5:  # memo stop (A short again)
            print("[ctrl] memo stop")
            self.status_cb("■ 已存入速记")
            seg = self.seg
            if seg:
                seg.finish()
            # ASR is async; trailing segments land after finish(). Flush the
            # joined memo shortly after so nothing is dropped.
            threading.Timer(3.0, self._flush_memo, args=(seg,)).start()
            self.seg = None  # drop straggling audio after this memo session
        else:
            print(f"[ctrl] unknown command {cmd}")

    def finish(self):
        if self.seg:
            self.seg.finish()
        # Best-effort flushes on disconnect; the Timer paths cover normal stops
        # (this only catches whatever had already transcribed). Voice staging
        # folders are left for the next startup's _purge_tmp, never removed
        # while the trailing ASR may still be reading them.
        self._flush_memo()
        self._flush_voice()


# ========== Device Pairing (scan + remember binding) ==========
# Per-host pairing: each computer scans nearby StickS3 devices, picks one, and
# remembers it. No server. The AI platform's settings page can drive this via
# `--scan --json` (list) and `--address` (connect to the chosen one).

import re

CONFIG_PATH = pathlib.Path.home() / ".sticks3" / "config.json"
DEVICE_PREFIX = "StickS3"  # advertised names look like StickS3-Mic or StickS3-Mic-D4D7C6
CODE_RE = re.compile(r"^[0-9A-Fa-f]{4,8}$")  # a pairing code, e.g. D4D7C6


def name_to_code(name: str):
    """Extract the pairing code (hex MAC suffix) from a device name, or None.
    'StickS3-Mic-D4D7C6' -> 'D4D7C6'. The code is what the device shows on its
    LCD, so the user pairs by typing that instead of a long address/UUID."""
    tail = name.rsplit("-", 1)[-1] if "-" in name else ""
    return tail.upper() if CODE_RE.match(tail) else None


def load_bound_device():
    """Return the saved {'address','name','code'} binding, or None if unpaired."""
    try:
        import json
        return json.loads(CONFIG_PATH.read_text(encoding="utf-8")).get("device")
    except Exception:
        return None


def save_bound_device(address: str, name: str, code: str = ""):
    """Persist the chosen device so future runs auto-connect."""
    import json
    CONFIG_PATH.parent.mkdir(parents=True, exist_ok=True)
    dev = {"address": address, "name": name, "code": code or name_to_code(name) or ""}
    CONFIG_PATH.write_text(
        json.dumps({"device": dev}, ensure_ascii=False, indent=2), encoding="utf-8")
    print("[pair] bound %s (code %s) -> %s" % (name, dev["code"] or "?", CONFIG_PATH))


async def scan_devices(prefix: str = DEVICE_PREFIX, timeout: float = 5.0):
    """Scan and return matching devices: [{name, code, address, rssi}], strongest
    signal first. `code` is the human-facing pairing code shown on the LCD.

    On Windows we use a direct WinRT watcher (Bleak's discover() can fail at the
    radio check); addresses come back as uint64 and are normalized to MAC-format
    strings so the rest of the code is platform-agnostic. macOS/Linux use plain
    Bleak (address is a per-host UUID string there)."""
    out = []

    if _WINRT_SCANNER:
        seen = set()
        done = asyncio.Event()
        watcher = BluetoothLEAdvertisementWatcher()
        watcher.scanning_mode = BluetoothLEScanningMode.ACTIVE

        def _on_received(_w, args):
            nm = args.advertisement.local_name or ""
            if nm.startswith(prefix) and args.bluetooth_address not in seen:
                seen.add(args.bluetooth_address)
                out.append({"name": nm, "code": name_to_code(nm),
                            "address": _int_to_mac(args.bluetooth_address),
                            "rssi": args.raw_signal_strength_in_dbm})

        def _on_stopped(_w, _e):
            done.set()

        watcher.add_received(_on_received)
        watcher.add_stopped(_on_stopped)
        watcher.start()
        try:
            await asyncio.wait_for(done.wait(), timeout=timeout)
        except asyncio.TimeoutError:
            pass
        watcher.stop()
    else:
        found = await BleakScanner.discover(timeout=timeout, return_adv=True)
        for dev, adv in found.values():
            nm = adv.local_name or dev.name or ""
            if nm.startswith(prefix):
                out.append({"name": nm, "code": name_to_code(nm),
                            "address": dev.address, "rssi": adv.rssi})

    out.sort(key=lambda d: (d["rssi"] is None, -(d["rssi"] or -999)))
    return out


async def resolve_by_code(code: str, timeout: float = 6.0):
    """Scan and return the device whose pairing code matches (case-insensitive),
    or None. Lets the user pair by the short code shown on the LCD."""
    code = code.upper()
    for d in await scan_devices(timeout=timeout):
        if d["code"] == code:
            return d
    return None


async def find_device(name: str, address: str):
    """Resolve a connectable target. On Windows we return a MAC-format string
    (BleakClient accepts it directly, and our patched scanner backs the name
    lookup); on macOS/Linux we return a BLEDevice from Bleak's helpers."""
    if _WINRT_SCANNER:
        if address:
            print("[scan] using address %s ..." % address)
            return address  # BleakClient(WinRT) accepts a MAC-format string
        print("[scan] looking for \"%s\" ..." % name)
        devices = await scan_devices(name, timeout=5.0)
        return devices[0]["address"] if devices else None

    if address:
        print("[scan] looking for address %s ..." % address)
        return await BleakScanner.find_device_by_address(address, timeout=15.0)
    print("[scan] looking for \"%s\" ..." % name)
    return await BleakScanner.find_device_by_name(name, timeout=15.0)


async def run(cfg, name: str, address: str, status_cb=None, inject_mode=True):
    """Main BLE connection loop. If inject_mode=True, transcribed text is injected
    at the cursor; otherwise it's only copied to clipboard (v16 behavior)."""
    status_cb = status_cb or (lambda _t: None)

    # Voice segments are injected at the cursor via AudioLink's dispatcher (it
    # also handles memo notes, which never touch the cursor). The Segmenter fires
    # segment_callback once per finalized segment on the transcription thread, so
    # we inject only the fresh text, not the whole accumulated draft.
    if inject_mode and _INJECTION_AVAILABLE:
        print("[inject] cursor injection enabled (paste + %s+V)"
              % ("Cmd" if platform.system() == "Darwin" else "Ctrl"))
    elif inject_mode:
        print("[warn] inject_mode requested but pynput/pyperclip unavailable")

    while True:
        status_cb("扫描设备 \"%s\"…" % name)
        device = await find_device(name, address)
        if device is None:
            print("[scan] not found, retrying...")
            continue
        link = AudioLink(cfg, status_cb, inject_mode=inject_mode)
        disconnected = asyncio.Event()

        def on_disconnect(_c):
            print("[-] disconnected")
            status_cb("设备断开，重连中…")
            disconnected.set()

        print("[+] connecting %s ..." % getattr(device, "address", device))
        status_cb("连接中…")
        try:
            async with BleakClient(device, disconnected_callback=on_disconnect) as client:
                await client.start_notify(CTRL_UUID, link.on_control)
                await client.start_notify(AUDIO_UUID, link.on_audio)
                print("[+] subscribed; talk into the stick (Ctrl+C to quit)")
                status_cb("已连接 · 按 BtnA 开始录音")
                await disconnected.wait()
        except Exception as exc:
            print("[ble error]", exc)
        finally:
            link.finish()
        print("[reconnect] retrying in 2s...")
        await asyncio.sleep(2)


def build_config(args):
    """transcribe_server.Config expects an argparse namespace with the VAD/seg
    knobs. We reuse its defaults so BLE and WS transcription behave identically.
    ASR comes from ASR_KEY / ASR_BASE_URL / ASR_MODEL in .env (provider-agnostic)."""
    import os
    asr_key = os.environ.get("ASR_KEY", "").strip()
    if not asr_key:
        raise SystemExit("Missing ASR_KEY (put it in the package .env).")
    clean = not args.no_clean and bool(asr_key)
    return Config(args, asr_key, clean)


def main():
    here = pathlib.Path(__file__).resolve().parent
    load_host_env(here)

    p = argparse.ArgumentParser()
    p.add_argument("--name", default=DEFAULT_NAME, help="BLE advertised name (prefix match)")
    p.add_argument("--address", default="", help="BLE address/UUID (skips name scan + saved binding)")
    p.add_argument("--scan", action="store_true",
                   help="scan for StickS3 devices and exit (pairing helper)")
    p.add_argument("--json", action="store_true",
                   help="with --scan: print results as JSON (for the AI platform UI)")
    p.add_argument("--pair", default="",
                   help="bind a device by its pairing code (the 6-hex code shown "
                        "on the stick's screen, e.g. D4D7C6) or a full address, then exit")
    p.add_argument("--out", default="transcripts")
    p.add_argument("--vad-backend", choices=("auto", "webrtc", "rms"), default="auto")
    p.add_argument("--vad-aggressiveness", type=int, default=2)
    p.add_argument("--vad-ratio", type=float, default=0.50)
    p.add_argument("--speech-rms", type=float, default=1200.0)
    p.add_argument("--silence-ms", type=int, default=800)
    p.add_argument("--min-segment-seconds", type=float, default=3.0)
    p.add_argument("--max-segment-seconds", type=float, default=14.0)
    p.add_argument("--pre-roll-seconds", type=float, default=0.5)
    p.add_argument("--workers", type=int, default=3)
    p.add_argument("--no-clean", action="store_true")
    p.add_argument("--no-inject", action="store_true", help="disable cursor injection (only clipboard)")
    args = p.parse_args()

    # --scan: list nearby StickS3 devices and exit. --json for the platform UI.
    if args.scan:
        devices = asyncio.run(scan_devices(args.name))
        if args.json:
            import json
            print(json.dumps(devices, ensure_ascii=False))
        else:
            if not devices:
                print("No StickS3 devices found. Power on the stick and retry.")
            print("  %-4s %-10s %-8s %s" % ("#", "CODE", "RSSI", "NAME"))
            for i, d in enumerate(devices):
                print("  [%d]  %-10s %-8s %s"
                      % (i, d["code"] or "-", d["rssi"], d["name"]))
            print("\nThe CODE matches what's shown on the stick's screen.")
            print("To bind one:  python ble_client.py --pair <CODE>")
        return

    # --pair: bind a device by its pairing code (preferred) or a full address.
    if args.pair:
        if CODE_RE.match(args.pair):
            print("[pair] scanning for code %s ..." % args.pair.upper())
            dev = asyncio.run(resolve_by_code(args.pair))
            if not dev:
                raise SystemExit("No device with code %s found. Check the stick's "
                                 "screen and that it's powered on." % args.pair.upper())
            save_bound_device(dev["address"], dev["name"], dev["code"])
        else:
            # Treat as a raw address/UUID (advanced / scripted use).
            save_bound_device(args.pair, args.name)
        return

    # Device resolution priority: --address > saved binding > name scan.
    address = args.address
    if not address:
        bound = load_bound_device()
        if bound and bound.get("address"):
            address = bound["address"]
            print("[pair] using bound device %s (code %s)"
                  % (bound.get("name", "?"), bound.get("code", "?")))

    cfg = build_config(args)
    inject_mode = not args.no_inject
    print("StickS3 v17 BLE client | ASR: %s | clean: %s | inject: %s"
          % (cfg.asr_model, cfg.clean_segments, inject_mode))
    if inject_mode:
        print("Text will be injected at cursor after each utterance (Cmd+V).")
        print("Press B button to send (Enter).\n")
    else:
        print("Text copied to clipboard only. Paste manually with Cmd+V.\n")

    # v17 is a cursor-injection tool: no draft window. Status goes to the terminal.
    try:
        asyncio.run(run(cfg, args.name, address, inject_mode=inject_mode))
    except KeyboardInterrupt:
        print("\nbye")
    finally:
        cfg.executor.shutdown(wait=False, cancel_futures=True)


if __name__ == "__main__":
    main()
