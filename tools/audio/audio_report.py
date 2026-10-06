"""
A sheet of what the audio look-dev recorded (docs/adr/0006): for each camera of a label captured
with run_lookdev.ps1 -Audio <seconds>, the still, the loudness over time (RMS in 100 ms windows,
dBFS, left and right), a spectrogram, and the sounds the audio system logged while there.

    python tools/audio/audio_report.py <label>         # build/lookdev/<label>/*.wav -> audio_sheet.png

It can't say how it sounds; it catches silence, clipping, level jumps, one-sided panning and
missing sounds. Also prints a table: peak, RMS and the loudest second per camera.
"""
import os
import re
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
W, ROW_H = 1500, 300


def read_wav(path):
    """-> (samples float [-1, 1] shape (n, channels), rate): PCM 16/24/32-bit or 32-bit float."""
    data = open(path, "rb").read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("not a WAV: %s" % path)
    pos, fmt, raw = 12, None, None
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            fmt = list(struct.unpack("<HHIIHH", body[:16]))
            if fmt[0] == 0xFFFE and len(body) >= 26:  # WAVE_FORMAT_EXTENSIBLE: the sub-format's tag
                fmt[0] = struct.unpack("<H", body[24:26])[0]
        elif cid == b"data":
            raw = body
        pos += 8 + size + (size & 1)
    tag, channels, rate, _, _, bits = fmt
    if tag == 3:
        x = np.frombuffer(raw, dtype="<f4")
    elif bits == 16:
        x = np.frombuffer(raw, dtype="<i2").astype(np.float32) / 32768.0
    elif bits == 24:
        b = np.frombuffer(raw[: len(raw) // 3 * 3], dtype=np.uint8).reshape(-1, 3)
        x = (b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16))
        x = np.where(x >= 1 << 23, x - (1 << 24), x).astype(np.float32) / float(1 << 23)
    elif bits == 32:
        x = np.frombuffer(raw, dtype="<i4").astype(np.float32) / float(1 << 31)
    else:
        raise ValueError("unsupported WAV format %s/%d bits" % (tag, bits))
    return x[: len(x) // channels * channels].reshape(-1, channels), rate


def db(v):
    return 20.0 * np.log10(np.maximum(v, 1e-6))


def loudness(x, rate, window=0.1):
    n = max(1, int(rate * window))
    m = len(x) // n
    if m == 0:
        return np.zeros((1, x.shape[1]))
    return db(np.sqrt((x[: m * n].reshape(m, n, -1) ** 2).mean(axis=1)))


def spectrogram(mono, rate, width, height):
    n = 2048
    hop = max(1, (len(mono) - n) // max(1, width))
    cols = []
    win = np.hanning(n)
    for i in range(width):
        seg = mono[i * hop: i * hop + n]
        if len(seg) < n:
            seg = np.pad(seg, (0, n - len(seg)))
        cols.append(db(np.abs(np.fft.rfft(seg * win)) / n))
    s = np.array(cols).T  # freq x time
    # log frequency axis 40 Hz .. 16 kHz
    freqs = np.fft.rfftfreq(n, 1.0 / rate)
    rows = np.geomspace(40, min(16000, rate / 2), height)[::-1]
    idx = np.clip(np.searchsorted(freqs, rows), 0, len(freqs) - 1)
    img = np.clip((s[idx] + 100) / 80.0, 0, 1)
    return (np.stack([img ** 0.7, img ** 1.5, 0.3 + 0.4 * img], axis=-1) * 255).astype(np.uint8)


def events(log_path):
    """-> {camera: [sound lines]} from the look-dev's audio.log: what started while the camera was set
    up (before its still) and while it recorded (until its .wav line)."""
    out, pending = {}, []
    if not os.path.exists(log_path):
        return out
    for line in open(log_path, encoding="utf-8-sig"):
        m = re.search(r"MRLookDevAudio: .*[\/]([^\/]+)\.wav", line)
        if m:
            out.setdefault(m.group(1), []).extend(pending)
            pending = []
            continue
        m = re.search(r"MRAudio: (.*)$", line.strip())
        if m:
            pending.append(m.group(1))
    return out


def main():
    label = sys.argv[1]
    d = os.path.join(ROOT, "build", "lookdev", label)
    wavs = sorted(f for f in os.listdir(d) if f.endswith(".wav"))
    if not wavs:
        print("no recordings in %s (run_lookdev.ps1 -Audio <seconds>)" % d)
        return 1
    ev = events(os.path.join(d, "audio.log"))
    sheet = Image.new("RGB", (W, ROW_H * len(wavs)), (18, 18, 20))
    draw = ImageDraw.Draw(sheet)
    print("%-22s %8s %8s %10s  %s" % ("camera", "peak dB", "rms dB", "L-R dB", "sounds started"))
    for r, f in enumerate(wavs):
        cam = f[:-4]
        x, rate = read_wav(os.path.join(d, f))
        y0 = r * ROW_H
        png = os.path.join(d, cam + ".png")
        if os.path.exists(png):
            sheet.paste(Image.open(png).convert("RGB").resize((400, 225)), (0, y0 + 30))
        lo = loudness(x, rate)
        peak, rms = float(db(np.abs(x).max())), float(db(np.sqrt((x ** 2).mean())))
        lr = float(db(np.sqrt((x[:, 0] ** 2).mean())) - db(np.sqrt((x[:, -1] ** 2).mean())))
        # loudness lanes (-60..0 dBFS)
        gx, gy, gw, gh = 410, y0 + 30, 520, 120
        draw.rectangle([gx, gy, gx + gw, gy + gh], outline=(70, 70, 80))
        for level in (-12, -24, -36, -48):
            yy = gy + gh * (-level) / 60.0
            draw.line([gx, yy, gx + gw, yy], fill=(40, 40, 48))
        for ch, col in zip(range(lo.shape[1]), ((120, 200, 255), (255, 170, 90))):
            pts = [(gx + gw * i / max(1, len(lo) - 1), gy + gh * min(1.0, max(0.0, -lo[i, ch] / 60.0))) for i in range(len(lo))]
            if len(pts) > 1:
                draw.line(pts, fill=col, width=1)
        draw.text((gx, gy + gh + 4), "loudness L (blue) R (orange), 0 to -60 dBFS over %.0f s" % (len(x) / rate), fill=(150, 150, 160))
        spec = spectrogram(x.mean(axis=1), rate, 540, 120)
        sheet.paste(Image.fromarray(spec), (gx + gw + 20, gy))
        draw.text((gx + gw + 20, gy + 124), "spectrogram 40 Hz - 16 kHz", fill=(150, 150, 160))
        draw.text((8, y0 + 8), "%s   peak %.1f dB  rms %.1f dB  L-R %+.1f dB" % (cam, peak, rms, lr), fill=(255, 220, 120))
        started = ev.get(cam, [])
        for i, line in enumerate(started[:8]):
            draw.text((gx, gy + gh + 22 + i * 13), line[:150], fill=(200, 200, 200))
        if len(started) > 8:
            draw.text((gx, gy + gh + 22 + 8 * 13), "... %d more" % (len(started) - 8), fill=(150, 150, 150))
        names = sorted({re.split(r"[ (]", s.split(" ", 1)[1] if " " in s else s)[0] for s in started})
        print("%-22s %8.1f %8.1f %+10.1f  %s" % (cam, peak, rms, lr, ", ".join(names)))
    out = os.path.join(d, "audio_sheet.png")
    sheet.save(out)
    print(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
