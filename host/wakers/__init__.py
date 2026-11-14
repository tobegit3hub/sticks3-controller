"""Generic wake launcher: `open -a <app>` for the app named in .env.

The stick's B long-press (control cmd=3) calls wakers.wake(), which launches
the app whose name is in LINKO_WAKE_APP in .env — whatever you fill in there is
what gets opened. No per-app plugin files anymore: switching target app = edit
one line in .env.

On macOS the name can be:
  * the registered name   LINKO_WAKE_APP=DingTalk   -> open -a DingTalk
  * a bundle id (has .)   LINKO_WAKE_APP=com.alibaba.DingTalkMac -> open -b ...
  * the display name      LINKO_WAKE_APP=钉钉         -> resolved via Spotlight
                         LINKO_WAKE_APP=微信          -> same

Examples:
    LINKO_WAKE_APP=WeChat              -> open -a WeChat
    LINKO_WAKE_APP=钉钉                 -> open /Applications/DingTalk.app
    LINKO_WAKE_APP=Linghui Workbench   -> open -a Linghui Workbench
"""
import os
import subprocess
import sys


def _find_app_path(display_name: str):
    """Resolve a Finder display name (incl. CJK) to an app path via Spotlight.

    Returns the first *.app path whose kMDItemDisplayName matches exactly, or
    None. Used because `open -a` only resolves LaunchServices registered names
    (e.g. "DingTalk"), not the Chinese display name users are used to ("钉钉")."""
    q = f"kMDItemDisplayName == '{display_name.replace(chr(39), chr(39) + chr(92) + chr(39))}'c"
    try:
        r = subprocess.run(["mdfind", q], capture_output=True, text=True, timeout=10)
    except subprocess.TimeoutExpired:
        return None
    for line in r.stdout.splitlines():
        line = line.strip()
        if line.endswith(".app"):
            return line
    return None


def wake(app=None):
    """Open the app named by `app` or LINKO_WAKE_APP in .env.

    On macOS: display names are resolved via Spotlight to an app path (so CJK
    names work); anything with a dot is treated as a bundle id (`open -b`);
    otherwise `open -a <name>` (brings a running app to front, or launches it).
    Never raises — failures are logged as hints so the voice pipeline keeps
    working."""
    name = (app or os.environ.get("LINKO_WAKE_APP", "") or "").strip()
    if not name:
        print("[wake] LINKO_WAKE_APP is empty in .env — nothing to wake")
        return
    try:
        if sys.platform == "darwin":
            if "." in name:
                args = ["open", "-b", name]
            else:
                path = _find_app_path(name)
                args = ["open", path] if path else ["open", "-a", name]
            subprocess.Popen(args)
        elif sys.platform == "win32":
            subprocess.Popen(["start", "", name], shell=True)
        else:
            subprocess.Popen([name])
        print(f"[wake] opening {name!r} ...")
    except Exception as exc:
        print(f"[wake] failed to open {name!r}: {exc}")
