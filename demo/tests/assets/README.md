`color-bars.mp4` is a silent, synthetic H.264 test video generated for Composia
with FFmpeg's `testsrc2` filter. It contains no third-party footage.

```powershell
ffmpeg -f lavfi -i "testsrc2=size=320x180:rate=15:duration=3" -an -c:v libx264 -pix_fmt yuv420p -crf 28 -movflags +faststart color-bars.mp4
```
