"""Run the portable NVDA and Windows SDK inspection checks on an inactive desktop."""
import argparse
from contextlib import ExitStack
import ctypes as ct
from ctypes import wintypes as w
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import uuid

user = ct.WinDLL("user32", use_last_error=True)
kernel = ct.WinDLL("kernel32", use_last_error=True)


class Startup(ct.Structure):
    _fields_ = [("cb", w.DWORD), ("reserved", w.LPWSTR), ("desktop", w.LPWSTR), ("title", w.LPWSTR),
                ("dimensions", w.DWORD * 8), ("show", w.WORD), ("reservedSize", w.WORD),
                ("reservedBytes", w.LPVOID), ("handles", w.HANDLE * 3)]


class Process(ct.Structure):
    _fields_ = [("process", w.HANDLE), ("thread", w.HANDLE), ("pid", w.DWORD), ("tid", w.DWORD)]


def api(dll, name, result, *arguments):
    fn = getattr(dll, name)
    fn.restype, fn.argtypes = result, list(arguments)
    return fn


create_desktop = api(user, "CreateDesktopW", w.HANDLE, w.LPCWSTR, w.LPCWSTR, w.LPVOID, w.DWORD, w.DWORD, w.LPVOID)
close_desktop = api(user, "CloseDesktop", w.BOOL, w.HANDLE)
create_process = api(kernel, "CreateProcessW", w.BOOL, w.LPCWSTR, w.LPWSTR, w.LPVOID, w.LPVOID,
                     w.BOOL, w.DWORD, w.LPVOID, w.LPCWSTR, ct.POINTER(Startup), ct.POINTER(Process))
close = api(kernel, "CloseHandle", w.BOOL, w.HANDLE)
wait = api(kernel, "WaitForSingleObject", w.DWORD, w.HANDLE, w.DWORD)
terminate = api(kernel, "TerminateProcess", w.BOOL, w.HANDLE, w.UINT)
exit_code = api(kernel, "GetExitCodeProcess", w.BOOL, w.HANDLE, ct.POINTER(w.DWORD))
window_pid = api(user, "GetWindowThreadProcessId", w.DWORD, w.HWND, ct.POINTER(w.DWORD))
title = api(user, "GetWindowTextW", ct.c_int, w.HWND, w.LPWSTR, ct.c_int)
post = api(user, "PostMessageW", w.BOOL, w.HWND, w.UINT, w.WPARAM, w.LPARAM)
callback = ct.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
enumerate_windows = api(user, "EnumDesktopWindows", w.BOOL, w.HANDLE, callback, w.LPARAM)


def check(ok):
    if not ok:
        raise ct.WinError(ct.get_last_error())


def start(stack, desktop, args):
    startup, proc = Startup(), Process()
    startup.cb, startup.desktop = ct.sizeof(startup), "WinSta0\\" + desktop
    command = ct.create_unicode_buffer(subprocess.list2cmdline(list(map(str, args))))
    check(create_process(str(args[0]), command, None, None, False, 0x08000000, None, None,
                         ct.byref(startup), ct.byref(proc)))
    close(proc.thread)

    def cleanup():
        if wait(proc.process, 0) == 258:
            terminate(proc.process, 1)
            wait(proc.process, 5000)
        close(proc.process)

    stack.callback(cleanup)
    return proc


def fixture_window(desk, proc):
    matches = []

    @callback
    def visit(hwnd, _):
        pid, name = w.DWORD(), ct.create_unicode_buffer(128)
        window_pid(hwnd, ct.byref(pid))
        title(hwnd, name, len(name))
        if pid.value == proc.pid and name.value == "Composia accessibility test":
            matches.append(hwnd)
        return True

    for _ in range(100):
        enumerate_windows(desk, visit, 0)
        if matches:
            return matches[0]
        if wait(proc.process, 100) == 0:
            break
    raise RuntimeError("Fixture window did not open")


def run(fixture, nvda, checker, playback):
    name = "ComposiaAccessibility" + uuid.uuid4().hex
    with tempfile.TemporaryDirectory() as work, ExitStack() as stack:
        desk = create_desktop(name, None, None, 0, 0x10000000, None)
        check(desk)
        stack.callback(close_desktop, desk)
        host = start(stack, name, [fixture, "fixture"])
        hwnd = fixture_window(desk, host)
        stack.callback(post, hwnd, 0x10, 0, 0)
        profile = Path(work) / "profile"
        plugins = profile / "scratchpad" / "globalPlugins"
        plugins.mkdir(parents=True)
        shutil.copy(Path(__file__).parent / "support/nvda_probe.py", plugins / "composiaProbe.py")
        (profile / "nvda.ini").write_text("[general]\nlanguage = en\nshowWelcomeDialogAtStartup = False\n"
            "playStartAndExitSounds = False\n[speech]\nsynth = silence\n[update]\nautoCheck = False\n"
            "[development]\nenableScratchpadDir = True\n", encoding="utf-8")
        result = Path(work) / "result.json"
        expected = ({"Output device": ("Output device", "COMBOBOX"), "Mute audio": ("Mute audio", "CHECKBOX"),
                     "Studio headphones": ("Studio headphones", "LISTITEM"), "Volume": ("Volume", "SLIDER")}
                    if playback else {"volume": ("Volume", "SLIDER"), "devices": ("Playback devices", "LIST"),
                                      "device1": ("Device 1", "LISTITEM"), "apply": ("Apply", "BUTTON")})
        os.environ.update(COMPOSIA_TEST_HWND=str(hwnd), COMPOSIA_NVDA_RESULT=str(result),
                          COMPOSIA_NVDA_TARGETS=json.dumps(list(expected)), COMPOSIA_NVDA_PLAYBACK=str(int(playback)))
        log = Path(work) / "nvda.log"
        reader = start(stack, name, [nvda, "--minimal", "--no-sr-flag", "--config-path", profile, "-f", log])
        for _ in range(300):
            if result.exists() or wait(reader.process, 100) == 0:
                break
        if not result.exists():
            raise RuntimeError(log.read_text(encoding="utf-8", errors="replace")[-5000:] if log.exists() else "NVDA did not start")
        report = json.loads(result.read_text(encoding="utf-8"))
        print(json.dumps(report, indent=2), flush=True)
        if "error" in report:
            raise RuntimeError(report["error"])
        for key, (label, role) in expected.items():
            entry = report[key]
            if entry["name"] != label or entry["role"] != role or label not in entry["speech"]:
                raise AssertionError(f"Unexpected NVDA announcement for {key}")
        if playback:
            if ("expanded" not in report["Output device"]["speech"].split()
                    or "selected" not in report["Studio headphones"]["speech"].split()):
                raise AssertionError("NVDA missed the expanded dropdown or its selected option")
        else:
            volume_words = report["volume"]["speech"].split()
            if ("25" not in volume_words or "focused" not in volume_words
                    or "selected" not in report["device1"]["speech"].split()
                    or report["document"].rstrip() != "Surround sound"):
                raise AssertionError("Unexpected NVDA value, focus, selection state or document text")
        check_log = Path(work) / "checker.xml"
        inspect = start(stack, name, [checker, "-hwnd", hex(hwnd), "-enable", "CheckName",
                        "-enable", "CheckRole", "-enable", "CheckTreeDepth", "-logfile", check_log])
        if wait(inspect.process, 30000) != 0:
            raise TimeoutError("Accessibility checker timed out")
        code = w.DWORD()
        check(exit_code(inspect.process, ct.byref(code)))
        if code.value:
            details = check_log.read_bytes()[:4000] if check_log.exists() else b"No log was created"
            raise RuntimeError(f"Accessibility checker exited with {code.value}: {details}")
        print("Windows SDK accessibility checks passed", flush=True)
        wait(reader.process, 3000)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--playback", action="store_true", help="Inspect the lookless playback controls")
    for option in ("fixture", "nvda", "checker"):
        parser.add_argument("--" + option, type=lambda path: Path(path).resolve(strict=True), required=True)
    run(**vars(parser.parse_args()))
