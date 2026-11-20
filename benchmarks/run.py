"""Collect interleaved, fresh-process measurements on an inactive Windows desktop."""
import argparse
import csv
import ctypes as ct
from ctypes import wintypes
from contextlib import ExitStack
from pathlib import Path
import random
import statistics
import sys
import tempfile
import uuid

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tests"))
from accessibility_tools import check, close_desktop, create_desktop, exit_code, start, wait


def measure(directory, output, rounds):
    rng = random.Random(303)
    backends = ["Composia", "Direct2D", "Gdi"]
    rows = []
    with ExitStack() as resources, tempfile.TemporaryDirectory() as temp:
        desktop = "ComposiaBenchmark" + uuid.uuid4().hex
        handle = create_desktop(desktop, None, None, 0, 0x10000000, None)
        check(handle)
        resources.callback(close_desktop, handle)
        for trial in range(rounds + 1):
            rng.shuffle(backends)
            for backend in backends:
                exe = (directory / f"bench-{backend}.exe").resolve(strict=True)
                result = Path(temp) / f"{backend}-{trial}.csv"
                with ExitStack() as process:
                    child = start(process, desktop, [exe, result])
                    if wait(child.process, 60000) != 0:
                        raise TimeoutError(backend)
                    code = wintypes.DWORD()
                    check(exit_code(child.process, ct.byref(code)))
                    if code.value:
                        raise RuntimeError(f"{backend} exited with {code.value}")
                startup, private, p50, p95 = result.read_text().strip().split(",")
                if trial:
                    rows.append([trial, backend, float(startup), int(private), exe.stat().st_size, float(p50), float(p95)])
    with output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(["trial", "backend", "startup_ms", "private_bytes", "exe_bytes", "frame_p50_ms", "frame_p95_ms"])
        writer.writerows(rows)
    for backend in sorted(backends):
        samples = [row[2:] for row in rows if row[1] == backend]
        print(backend, *(round(statistics.median(column), 4) for column in zip(*samples)))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="Release benchmarks binary directory")
    parser.add_argument("output", type=Path)
    parser.add_argument("--rounds", type=int, default=11)
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error("--rounds must be positive")
    measure(args.directory, args.output, args.rounds)
