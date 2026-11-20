# Drawing benchmark on Windows 11 x64

Each executable initializes one graphics backend, paints the same 640 × 360 image
with 96 colored rectangles, and reads a changing pixel back to the CPU. The two
Direct2D paths draw on GPU-backed surfaces. GDI draws into a memory bitmap.

| Backend | Startup to first verified frame | Private memory | Executable | Frame p50 | Frame p95 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Composia Composition surface | 240.8 ms | 55.48 MiB | 312.5 KiB | 0.094 ms | 0.722 ms |
| Direct2D on D3D11 | 236.4 ms | 55.32 MiB | 197.0 KiB | 0.060 ms | 0.230 ms |
| Win32 GDI | 22.1 ms | 1.84 MiB | 194.5 KiB | 0.131 ms | 0.136 ms |

GDI had the lowest startup time and memory use on this workload. Direct2D had
the fastest median redraw. Composia's median redraw fell between the other two
and its 95th percentile was the highest. The Composition path copies the sampled
pixel out of its surface atlas before the common GPU staging read. Applications
that keep their output on the GPU avoid this synchronous readback.

**Environment.** November 20, 2026. Windows 11 Pro 26H2 build 26300.9974, Ryzen 9
5900X, GeForce RTX 3090 driver 32.0.16.2124, clang-cl 21.1.1, x64 Release,
static CRT. The measured source is commit `fda7457`. [Per-process results](windows11-x64.csv)

A [device creation investigation](d3d11-device-startup.md) isolates two
driver-initiated operations on this machine before the first draw.

**Method.** The driver uses one inactive desktop and a seeded, interleaved order.
It discards one fresh process per backend and measures 11 more. Startup spans
process creation through validation of the first rendered pixel with a warm
filesystem cache. Private committed bytes are sampled after 12 additional
frames. Executable size excludes OS DLLs, graphics drivers, and symbols. Each
process times 120 further full repaints with QueryPerformanceCounter, including
completion and pixel validation. The table gives medians across processes,
including the median of each process's p95. It describes offscreen drawing on
one machine. Window presentation latency, animation throughput, cold boot,
GPU memory, and complete editor startup require different measurements.

**Reproduce** from a checkout with the pinned vcpkg submodule:

```powershell
.\scripts\build.ps1 -Preset release -Benchmarks
python benchmarks/run.py out/build/release/benchmarks out/results.csv
```

The executable returns a failure if a readback has the wrong color. The runner
writes the results after all three backends complete successfully.
