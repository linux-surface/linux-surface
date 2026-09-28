#!/usr/bin/env python3
"""Surface Book 3 IPU4 — RAW→RGB/GREY (no SoftISP).

Quality notes:
- line_off=4: MIPI header at the start of each line
- bytesused = stride*h - 4 (stable colors in MP4)
- RAW10: full 10 bits → percentile to 8-bit
- RGB: Bayer RG (BG swapped red↔blue), rot180+flipH,
  WB from near-whites + HSV orange→red
- IR: rot90 + CLAHE (plumbing present; STREAMON disabled in webcamd)
"""
from __future__ import annotations

import os
import struct
import subprocess
import time
from dataclasses import dataclass
from typing import Optional

import numpy as np

try:
    import cv2
except ImportError as e:
    raise SystemExit("python3-opencv is required") from e

MD = "/dev/media0"
# IPU4 packed RAW10: 4 B CSI-2 header at the start of each line (do not confuse
# with plane data_offset — v4l2-ctl drops 4 B at the end of the frame).
LINE_OFF = 4


@dataclass
class Cam:
    tag: str
    sensor: str
    csi: str
    cap: str
    mbus: str
    fourcc: str
    modes: list[tuple[int, int]]
    bayer: Optional[int] = None
    rotate90: int = 0
    rotate180: bool = False
    flip_h: bool = False
    flip_v: bool = False
    ir: bool = False
    exposure: int = 800
    gain: int = 64
    target_mean: float = 110.0
    # CSI source pad → capture (IR Howdy: pad 2 → capture 1)
    csi_src_pad: int = 1


CAMS = {
    "back": Cam(
        "back",
        "ov8865 3-0010",
        "Intel IPU4 CSI-2 0",
        "Intel IPU4 CSI-2 0 capture 0",
        "SBGGR10_1X10",
        "pBAA",
        [(1632, 1224), (800, 600), (3264, 2448)],
        # LINE_OFF=4 (CSI-2 header). Phase: RG — BG swaps red↔blue
        # (grey hoodie → yellow, brown wallpaper → blue). Fourcc pBAA lies.
        cv2.COLOR_BayerRG2BGR_EA,
        # rotate180+flip_h ≡ flip_v — keep flip_v directly
        flip_v=True,
        # ov8865: exposure max≈632, analogue_gain step 128 (128..2048)
        exposure=560,
        gain=384,
        target_mean=100.0,
    ),
    "front": Cam(
        "front",
        "ov5693 2-0036",
        "Intel IPU4 CSI-2 2",
        "Intel IPU4 CSI-2 2 capture 0",
        "SBGGR10_1X10",
        "pBAA",
        [(1296, 972), (2592, 1944)],
        cv2.COLOR_BayerRG2BGR_EA,
        flip_v=True,
        # same light dose as 90/32 (90*32=360*8), 4× lower ISO.
        exposure=360,
        gain=8,
        target_mean=100.0,
    ),
    "ir": Cam(
        "ir",
        "ov7251 3-0060",
        "Intel IPU4 CSI-2 1",
        "Intel IPU4 CSI-2 1 capture 1",
        "Y10_1X10",
        "Y10 ",
        [(640, 480)],
        None,
        rotate90=1,
        flip_v=False,
        flip_h=False,
        rotate180=False,
        ir=True,
        # ov7251 — brighter for webcam (LED must be ON before stream)
        exposure=90,
        gain=18,
        target_mean=70.0,
        csi_src_pad=2,
    ),
}


def run(cmd: list[str], check=True, timeout=60) -> subprocess.CompletedProcess:
    if cmd and cmd[0] == "media-ctl":
        sudo_cmd = ["sudo", "-n", "/usr/bin/media-ctl", *cmd[1:]]
        try:
            return subprocess.run(
                sudo_cmd, check=check, timeout=timeout, capture_output=True, text=True
            )
        except (subprocess.CalledProcessError, FileNotFoundError, PermissionError):
            pass
    return subprocess.run(cmd, check=check, timeout=timeout, capture_output=True, text=True)


def find_subdev(name_substr: str) -> Optional[str]:
    for ent in sorted(os.listdir("/sys/class/video4linux")):
        if not ent.startswith("v4l-subdev"):
            continue
        try:
            with open(f"/sys/class/video4linux/{ent}/name") as f:
                if name_substr in f.read():
                    return f"/dev/{ent}"
        except OSError:
            pass
    return None


SENSOR_PM_DIR = {
    "ov5693": "/sys/bus/i2c/devices/i2c-INT33BE:00/power",
    "ov8865": "/sys/bus/i2c/devices/i2c-INT347A:00/power",
    "ov7251": "/sys/bus/i2c/devices/i2c-INT347E:00/power",
}


def _sensor_pm_write(sensor_name: str, value: str) -> None:
    d = SENSOR_PM_DIR.get(sensor_name)
    if not d or not os.path.exists(d):
        return
    path = f"{d}/control"
    try:
        subprocess.run(
            ["sudo", "-n", "/usr/bin/tee", path],
            input=f"{value}\n",
            text=True,
            check=False,
            capture_output=True,
            timeout=3,
        )
    except Exception:
        try:
            with open(path, "w") as f:
                f.write(f"{value}\n")
        except OSError:
            pass


def sensor_runtime_status(sensor_name: str) -> str:
    d = SENSOR_PM_DIR.get(sensor_name)
    if not d:
        return "unknown"
    try:
        with open(f"{d}/runtime_status") as f:
            return f.read().strip()
    except OSError:
        return "unknown"


def sensor_runtime_cycle(sensor_name: str, timeout: float = 3.0) -> bool:
    """Restore control=auto and wait until the sensor is `suspended`.

    CRITICAL (ov5693 and ov8865): mode registers (size/binning/PLL) are written
    ONLY on runtime-resume (sensor_init → mode_configure from the remembered fmt).
    set_fmt on an active sensor changes software state only → sensor streams the
    old mode, ISYS waits for the new one → FIFO overflow / DPHY error → isys EIO.
    Set fmt on a SUSPENDED sensor, then power it on.
    """
    d = SENSOR_PM_DIR.get(sensor_name)
    if not d or not os.path.exists(d):
        return True
    if sensor_runtime_status(sensor_name) == "suspended":
        return True
    _sensor_pm_write(sensor_name, "auto")
    t0 = time.time()
    while time.time() - t0 < timeout:
        if sensor_runtime_status(sensor_name) == "suspended":
            return True
        time.sleep(0.05)
    return sensor_runtime_status(sensor_name) == "suspended"


def sensor_runtime_on(sensor_name: str, settle: float = 0.3) -> None:
    """Wake the sensor (resume = sensor_init with current fmt) and wait for `active`.

    Call ONLY after media-ctl -V (set_fmt) — see sensor_runtime_cycle().
    """
    d = SENSOR_PM_DIR.get(sensor_name)
    if not d or not os.path.exists(d):
        return
    _sensor_pm_write(sensor_name, "on")
    t0 = time.time()
    while time.time() - t0 < 2.0:
        if sensor_runtime_status(sensor_name) == "active":
            break
        time.sleep(0.02)
    time.sleep(settle)


def ov5693_force_mipi_stream(on: bool) -> None:
    """0x0100 before CSI RX — MIPI clock must be on the lane at STREAMON."""
    val = "0x01" if on else "0x00"
    r = subprocess.run(
        ["i2cset", "-f", "-y", "2", "0x36", "0x01", "0x00", val, "i"],
        check=False,
        capture_output=True,
        text=True,
        timeout=2,
    )
    print(
        f"ov5693 0x0100={'1' if on else '0'} rc={r.returncode} { (r.stderr or '').strip()[:80]}",
        flush=True,
    )
    if on:
        time.sleep(0.012)


def set_sensor_exposure(sensor_name: str, exposure: int, gain: int) -> None:
    sub = find_subdev(sensor_name)
    if not sub:
        return
    sensor_runtime_on(sensor_name)
    subprocess.run(
        ["v4l2-ctl", "-d", sub, "--set-ctrl", f"exposure={exposure},analogue_gain={gain}"],
        check=False,
        capture_output=True,
    )
    # warm bias only if the sensor has those controls (ov8865)
    if sensor_name == "ov8865":
        subprocess.run(
            ["v4l2-ctl", "-d", sub, "--set-ctrl", "red_balance=1200,blue_balance=1100"],
            check=False,
            capture_output=True,
        )


def auto_expose(
    cam: Cam,
    dev: str,
    w: int,
    h: int,
    stride: int,
    target: Optional[float] = None,
    rounds: int = 4,
) -> tuple[int, int]:
    """Simple AE: pick exposure/gain for target mean (after demosaic/grey)."""
    target = float(target if target is not None else cam.target_mean)
    exp, gain = cam.exposure, cam.gain
    name = cam.sensor.split()[0]
    fl = frame_bytes(stride, h, cam, w)
    # per-sensor limits
    if cam.ir or name == "ov7251":
        exp_lo, exp_hi, gain_lo, gain_hi, gstep = 30, 90, 6, 18, 2
    elif name == "ov5693":
        # Front: exposure max≈1030, analogue_gain 1..127
        exp_lo, exp_hi, gain_lo, gain_hi, gstep = 200, 1030, 8, 120, 8
    else:  # ov8865 — prefer exposure, keep gain low (less noise)
        exp_lo, exp_hi, gain_lo, gain_hi, gstep = 200, 632, 128, 1024, 128

    for _ in range(rounds):
        set_sensor_exposure(name, exp, gain)
        time.sleep(0.08)
        try:
            raw = capture_raw(dev, 1, f"/tmp/ae-{cam.tag}.raw", timeout=12)
        except Exception:
            break
        chunks = split_frames(raw, fl)
        if not chunks and len(raw) >= fl - 8:
            chunks = [raw[:fl] if len(raw) >= fl else raw]
        if not chunks:
            break
        if cam.ir:
            # measure RAW 10-bit — 8-bit stretch always has "white" by percentile
            img16 = unpack_mipi10_u16(chunks[-1], w, h, stride)
            hh, ww = img16.shape[:2]
            crop16 = img16[hh // 4 : (3 * hh) // 4, ww // 4 : (3 * ww) // 4]
            sat = float((crop16 > 880).mean() * 100)
            mean = float(crop16.mean()) / 4.0  # ~8-bit scale
            if sat > 1.5:
                mean = max(mean, 150.0 + sat * 3.0)
        else:
            img = unpack_mipi10(chunks[-1], w, h, stride)
            # measure the frame center, skip a blown-out window
            hh, ww = img.shape[:2]
            crop = img[hh // 4 : (3 * hh) // 4, ww // 4 : (3 * ww) // 4]
            sample = crop[(crop > 20) & (crop < 210)]
            mean = float(sample.mean()) if sample.size > 100 else float(crop.mean())
        err = target - mean
        if abs(err) < 8:
            break
        factor = float(np.clip(target / max(mean, 1.0), 0.6, 1.55))
        new_exp = int(np.clip(exp * factor, exp_lo, exp_hi))
        if new_exp == exp and abs(err) > 12:
            if err > 0:
                gain = min(gain_hi, gain + gstep)
            else:
                gain = max(gain_lo, gain - gstep)
        else:
            exp = new_exp
            # if still dark at max exp — only then raise gain
            if err > 15 and exp >= exp_hi - 5:
                gain = min(gain_hi, gain + gstep)
            elif err < -15 and exp <= exp_lo + 5:
                gain = max(gain_lo, gain - gstep)
    set_sensor_exposure(name, exp, gain)
    cam.exposure, cam.gain = exp, gain
    print(f"AE {cam.tag}: exposure={exp} gain={gain} (target≈{target})", flush=True)
    return exp, gain


def ir_led(on: bool = True) -> None:
    bin_path = "/usr/local/bin/ir-led-on"
    if not os.path.isfile(bin_path):
        return
    subprocess.run([bin_path] if on else [bin_path, "off"], check=False, capture_output=True)


def rearm_sb3_afe(bb: int = 10) -> None:
    """Buttress AFE/DLL + CPHY_RX_CONTROL1.en_crc1 (Intel IPU6 JSL PHY).

    bb 6=Back, 8=IR, 10=Front. Fallback gdy .ko ISYS nie ma rearm przy STREAMON.
    """
    path = "/sys/bus/pci/devices/0000:00:05.0/resource0"
    half = (bb >> 1) * 0x100
    cphy = 0x10100 + half
    rx1 = 0x10110 + half
    dphy = 0x1014c + half
    afe = 0x10174 + half
    try:
        fd = os.open(path, os.O_RDWR | os.O_SYNC)
    except OSError as e:
        print(f"AFE rearm: no BAR ({e})", flush=True)
        return
    try:
        def rd(off: int) -> int:
            return struct.unpack("<I", os.pread(fd, 4, off))[0]

        def wr(off: int, val: int) -> None:
            os.pwrite(fd, struct.pack("<I", val), off)

        val = rd(cphy)
        wr(cphy, (val & ~0x7E) | (13 << 1) | 1)
        wr(rx1, rd(rx1) | (1 << 31))
        val = rd(dphy)
        wr(dphy, val | 1 | (32 << 1))
        wr(afe, 0x15 | (2 << 29))
        print(
            f"AFE rearm bb{bb} cphy=0x{rd(cphy):x} rx1=0x{rd(rx1):x} "
            f"dphy=0x{rd(dphy):x} afe=0x{rd(afe):x}",
            flush=True,
        )
    except OSError as e:
        print(f"AFE rearm BAR: {e}", flush=True)
    finally:
        os.close(fd)
    time.sleep(0.00005)


def rearm_sb3_front_afe() -> None:
    rearm_sb3_afe(10)


def media_setup(cam: Cam, w: int, h: int) -> str:
    """Order is mandatory: sensor asleep → set_fmt → power on → ctrls.

    Reverse order (power on before set_fmt) = sensor in default mode
    (ov5693 2592x1944, ov8865 3264x2448) vs ISYS at the requested size
    → DPHY/FIFO → isys EIO.
    """
    res = f"{w}x{h}"
    pad = getattr(cam, "csi_src_pad", 1)
    sensor_name = cam.sensor.split()[0]
    if not sensor_runtime_cycle(sensor_name):
        raise RuntimeError(
            f"{sensor_name}: sensor is not suspended before set_fmt "
            f"(status={sensor_runtime_status(sensor_name)}) — refuse STREAMON (mismatch risk)"
        )
    run(["media-ctl", "-d", MD, "-r"], check=False)
    for ent in (cam.sensor, cam.csi):
        run(["media-ctl", "-d", MD, "-V", f'"{ent}":0 [fmt:{cam.mbus}/{res} field:none]'])
    run(["media-ctl", "-d", MD, "-V", f'"{cam.csi}":{pad} [fmt:{cam.mbus}/{res} field:none]'])
    run(["media-ctl", "-d", MD, "-l", f'"{cam.sensor}":0 -> "{cam.csi}":0 [1]'])
    run(["media-ctl", "-d", MD, "-l", f'"{cam.csi}":{pad} -> "{cam.cap}":0 [1]'])
    r = run(["media-ctl", "-d", MD, "-e", cam.cap])
    dev = r.stdout.strip()
    # IR fourcc has a trailing space ("Y10 ") — argv list keeps it
    run(
        ["v4l2-ctl", "-d", dev, "--set-fmt-video", f"width={w},height={h},pixelformat={cam.fourcc}"],
        check=False,
    )
    set_sensor_exposure(cam.sensor.split()[0], cam.exposure, cam.gain)
    # AFE/DLL only in isys_setup_hw (resume). Userspace pwrite at
    # STREAMON locked the DLL with no MIPI clock → Front n=0 + EIO on BAR.
    return dev


def get_stride(dev: str, w: int) -> int:
    out = run(["v4l2-ctl", "-d", dev, "--get-fmt-video"], check=False)
    for line in (out.stdout or "").splitlines():
        if "Bytes per Line" in line:
            return int(line.split(":")[-1].strip())
    row = w * 10 // 8
    return ((row + 63) // 64) * 64


def get_size_image(dev: str) -> Optional[int]:
    out = run(["v4l2-ctl", "-d", dev, "--get-fmt-video"], check=False)
    for line in (out.stdout or "").splitlines():
        if "Size Image" in line:
            try:
                return int(line.split(":")[-1].strip())
            except ValueError:
                return None
    return None


def v4l_stream_bytes(stride: int, h: int) -> int:
    """Actual v4l2-ctl --stream-to payload size.

    IPU sets bytesused = stride*h, but data_offset = 4 (CSI-2 header of
    the first line). v4l2-ctl dumps bytesused-data_offset = stride*h-4.
    Splitting on stride*h steals 4 B from the next frame → X drift
    and Bayer phase jump (RGB flicker). Size Image = stride*(h+1) is
    DMA slack, not payload length.
    """
    return stride * h - 4


def frame_bytes(stride: int, h: int, cam: Optional[Cam] = None, w: int = 0, dev: Optional[str] = None) -> int:
    if cam and cam.ir and (w, h) == (640, 480):
        return 399356
    return v4l_stream_bytes(stride, h)


def front_dphy_kick() -> bool:
    """FORBIDDEN. Full-res STREAMON (2592×1944) kills ISYS (EIO) and can hang boot.

    Left as a no-op so old callers cannot fire the kick.
    """
    print("front DPHY kick REFUSED (never hang policy)", flush=True)
    return False


def brighten_bgr(bgr: np.ndarray, target: float = 120.0) -> np.ndarray:
    """Raise RGB brightness (CLAHE + gamma + scale) when the scene is too dark."""
    m = float(bgr.mean())
    if m >= target * 0.85:
        return bgr
    lab = cv2.cvtColor(bgr, cv2.COLOR_BGR2LAB)
    l, a, b = cv2.split(lab)
    clip = 2.5 if m > 50 else 4.0
    clahe = cv2.createCLAHE(clipLimit=clip, tileGridSize=(8, 8))
    l = clahe.apply(l)
    out = cv2.cvtColor(cv2.merge([l, a, b]), cv2.COLOR_LAB2BGR)
    g = 1.2 if m > 60 else 1.55
    out = np.clip((out.astype(np.float32) / 255.0) ** (1.0 / g) * 255.0, 0, 255).astype(np.uint8)
    m2 = float(out.mean())
    if m2 < target * 0.7:
        alpha = min(target / max(m2, 1.0), 6.0)
        out = cv2.convertScaleAbs(out, alpha=alpha, beta=8)
    return out

def unpack_mipi10_u16(raw: bytes, w: int, h: int, stride: int, line_off: int = LINE_OFF) -> np.ndarray:
    """MIPI RAW10 → uint16 (0..1023), no stretch."""
    need = stride * h
    if len(raw) < need:
        raw = raw + bytes(need - len(raw))
    elif len(raw) > need:
        raw = raw[:need]
    row = w * 10 // 8
    if line_off + row > stride:
        line_off = 0
    buf = np.frombuffer(raw, dtype=np.uint8).reshape(h, stride)
    packed = np.ascontiguousarray(buf[:, line_off : line_off + row])
    g = packed.reshape(h, row // 5, 5).astype(np.uint16)
    img16 = np.empty((h, w), dtype=np.uint16)
    img16[:, 0::4] = (g[:, :, 0] << 2) | (g[:, :, 4] & 0x3)
    img16[:, 1::4] = (g[:, :, 1] << 2) | ((g[:, :, 4] >> 2) & 0x3)
    img16[:, 2::4] = (g[:, :, 2] << 2) | ((g[:, :, 4] >> 4) & 0x3)
    img16[:, 3::4] = (g[:, :, 3] << 2) | ((g[:, :, 4] >> 6) & 0x3)
    return img16


def unpack_mipi10(raw: bytes, w: int, h: int, stride: int, line_off: int = LINE_OFF) -> np.ndarray:
    """MIPI RAW10 → uint8 (percentile from full 10 bits) — RGB."""
    img16 = unpack_mipi10_u16(raw, w, h, stride, line_off)
    lo, hi = np.percentile(img16, (1.0, 99.5))
    if hi <= lo + 1:
        return (img16 >> 2).astype(np.uint8)
    return np.clip((img16.astype(np.float32) - lo) * (255.0 / (hi - lo)), 0, 255).astype(np.uint8)


def unpack_mipi10_ir(raw: bytes, w: int, h: int, stride: int, line_off: int = LINE_OFF) -> np.ndarray:
    """IR: fixed 10-bit→8-bit scale (percentile always blows the forehead with vignetting)."""
    img16 = unpack_mipi10_u16(raw, w, h, stride, line_off)
    # typical LED peak ≈600–750; leave headroom instead of stretching to 255
    return np.clip(img16.astype(np.float32) * (255.0 / 620.0), 0, 255).astype(np.uint8)


unpack_mipi10_grey = unpack_mipi10


def _orient(img: np.ndarray, cam: Cam) -> np.ndarray:
    # rot90 first (sensor on its side), then 180/flips
    if cam.rotate90:
        img = np.ascontiguousarray(np.rot90(img, cam.rotate90))
    if cam.rotate180:
        img = cv2.rotate(img, cv2.ROTATE_180)
    if cam.flip_h:
        img = cv2.flip(img, 1)
    if cam.flip_v:
        img = cv2.flip(img, 0)
    return img


def finish_bgr(cam: Cam, grey: np.ndarray) -> np.ndarray:
    """WB + neutral white lights (no magenta) + denoise + unsharp."""
    code = cam.bayer or cv2.COLOR_BayerRG2BGR_EA
    try:
        bgr = cv2.cvtColor(grey, code)
    except cv2.error:
        bgr = cv2.cvtColor(grey, cv2.COLOR_BayerRG2BGR)
    bgr = _orient(bgr, cam)

    # tonal percentile
    p1, p99 = np.percentile(bgr, (1.0, 99.2))
    if p99 > p1 + 3:
        bgr = np.clip((bgr.astype(np.float32) - p1) * (255.0 / (p99 - p1)), 0, 255).astype(np.uint8)

    # midtone grey-world
    f = bgr.astype(np.float32)
    flat = f.reshape(-1, 3)
    lum = flat.mean(axis=1)
    mid = flat[(lum > 40) & (lum < 200)]
    if len(mid) < 100:
        mid = flat
    means = np.maximum(mid.mean(axis=0), 1.0)
    scales = np.clip(float(means.mean()) / means, 0.7, 1.55)
    f = f * scales

    # Highlight neutralization: bright pixels → true white (drops mauve/magenta windows)
    lum2 = f.mean(axis=2)
    # desaturate from ~180 (window halo), full white from ~215
    t = np.clip((lum2 - 180.0) / 45.0, 0.0, 1.0)[..., None]
    white = np.full_like(f, 255.0)
    grey_hi = np.mean(f, axis=2, keepdims=True)
    f = f * (1.0 - t) + grey_hi * t
    t2 = np.clip((lum2 - 215.0) / 25.0, 0.0, 1.0)[..., None]
    f = f * (1.0 - t2) + white * t2
    bgr = np.clip(f, 0, 255).astype(np.uint8)

    # CLAHE on L only; a/b in highlights → 128 (neutral)
    lab = cv2.cvtColor(bgr, cv2.COLOR_BGR2LAB)
    l, a, b = cv2.split(lab)
    clahe = cv2.createCLAHE(clipLimit=1.35, tileGridSize=(8, 8))
    l = cv2.addWeighted(l, 0.55, clahe.apply(l), 0.45, 0)
    hi = l > 210
    a = a.astype(np.float32)
    b = b.astype(np.float32)
    a[hi] = 128 + (a[hi] - 128) * 0.15
    b[hi] = 128 + (b[hi] - 128) * 0.15
    # hard lights → L=255, a=b=128
    clipped = l > 245
    l = l.copy()
    l[clipped] = 255
    a[clipped] = 128
    b[clipped] = 128
    bgr = cv2.cvtColor(
        cv2.merge([l, np.clip(a, 0, 255).astype(np.uint8), np.clip(b, 0, 255).astype(np.uint8)]),
        cv2.COLOR_LAB2BGR,
    )

    bgr = cv2.bilateralFilter(bgr, 7, 20, 20)
    # light sharpen
    blur = cv2.GaussianBlur(bgr, (0, 0), 1.1)
    bgr = cv2.addWeighted(bgr, 1.35, blur, -0.35, 0)
    if bgr.shape[1] > 32:
        bgr = bgr[:, 4:-4].copy()
    return bgr


def find_focus_subdev() -> Optional[str]:
    """DW9719 VCM (OV8865 autofocus) — focus_absolute 0..1023."""
    for ent in sorted(os.listdir("/sys/class/video4linux")):
        if not ent.startswith("v4l-subdev"):
            continue
        try:
            with open(f"/sys/class/video4linux/{ent}/name") as f:
                name = f.read().strip()
        except OSError:
            continue
        if "dw9719" in name.lower():
            return f"/dev/{ent}"
    return None


_focus_fd = None  # keep VCM powered (dw9719 applies focus only when runtime-active)


def focus_open() -> Optional[str]:
    """Open the VCM subdev — without it focus_absolute is a no-op (PM)."""
    global _focus_fd
    lens = find_focus_subdev()
    if not lens:
        return None
    if _focus_fd is None:
        _focus_fd = os.open(lens, os.O_RDWR | os.O_NONBLOCK)
    return lens


def focus_close() -> None:
    global _focus_fd
    if _focus_fd is not None:
        try:
            os.close(_focus_fd)
        except OSError:
            pass
        _focus_fd = None


def set_focus(pos: int) -> bool:
    lens = focus_open()
    if not lens:
        return False
    pos = int(max(0, min(1023, pos)))
    r = subprocess.run(
        ["v4l2-ctl", "-d", lens, "--set-ctrl", f"focus_absolute={pos}"],
        check=False,
        capture_output=True,
        text=True,
    )
    return r.returncode == 0


def contrast_score(img: np.ndarray, roi: Optional[tuple[int, int, int, int]] = None) -> float:
    """Laplace variance — higher = sharper."""
    if img.ndim == 3:
        g = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    else:
        g = img
    if roi:
        y0, y1, x0, x1 = roi
        g = g[y0:y1, x0:x1]
    return float(cv2.Laplacian(g, cv2.CV_64F).var())


def autofocus_back(
    cam: Cam,
    w: int,
    h: int,
    stride: int,
    dev: str,
    positions: Optional[list[int]] = None,
) -> tuple[int, bytes]:
    """Contrast-detect AF — separate capture; score on a light demosaic (no denoise)."""
    if positions is None:
        positions = list(range(64, 1024, 64)) + [1023]
    # ROI: lower-left (avoid window)
    roi = (h // 2, (5 * h) // 6, w // 6, w // 2)
    fl = frame_bytes(stride, h, cam, w)
    best_pos, best_score, best_raw = 0, -1.0, b""
    for pos in positions:
        focus_open()
        if not set_focus(pos):
            focus_close()
            break
        time.sleep(0.2)
        # capture with VCM open; if it fails — close and retry quickly
        raw = b""
        try:
            raw = capture_raw(dev, 1, f"/tmp/af-{pos}.raw", timeout=10)
        except Exception:
            focus_close()
            time.sleep(0.05)
            try:
                raw = capture_raw(dev, 1, f"/tmp/af-{pos}.raw", timeout=10)
            except Exception:
                continue
        finally:
            focus_close()
        if not raw:
            continue
        chunks = split_frames(raw, fl)
        if not chunks:
            if len(raw) >= fl - 8:
                chunks = [raw[:fl]]
            else:
                continue
        frame = chunks[-1]
        img = frame_from_raw_sharp(cam, frame, w, h, stride)
        sc = contrast_score(img, roi)
        if sc > best_score:
            best_score, best_pos, best_raw = sc, pos, frame
    if best_raw:
        focus_open()
        set_focus(best_pos)
        time.sleep(0.2)
        try:
            raw2 = capture_raw(dev, 1, "/tmp/af-final.raw", timeout=12)
            chunks = split_frames(raw2, fl)
            if chunks:
                best_raw = chunks[-1]
        except Exception:
            pass
        focus_close()
    return best_pos, best_raw


def finish_ir(grey: np.ndarray, cam: Cam) -> np.ndarray:
    """Light post — aggressive nlmeans/bilateral turned the face into streaks."""
    grey = _orient(grey, cam)
    grey = cv2.medianBlur(grey, 3)
    # soft LED hotspot rolloff (do not smooth the whole face)
    gf = grey.astype(np.float32)
    hi = gf > 200
    gf[hi] = 200 + (gf[hi] - 200) * 0.35
    grey = np.clip(gf, 0, 255).astype(np.uint8)
    clahe = cv2.createCLAHE(clipLimit=1.05, tileGridSize=(8, 8))
    grey = cv2.addWeighted(grey, 0.82, clahe.apply(grey), 0.18, 0)
    # very light sharpen (Howdy likes eye/nose edges)
    blur = cv2.GaussianBlur(grey, (0, 0), 0.6)
    grey = cv2.addWeighted(grey, 1.2, blur, -0.2, 0)
    return grey


def frame_from_raw_sharp(cam: Cam, raw: bytes, w: int, h: int, stride: int) -> np.ndarray:
    """Light demosaic+orient for AF — no bilateral/CLAHE (they wreck Laplace)."""
    if cam.ir:
        return _orient(unpack_mipi10_ir(raw, w, h, stride), cam)
    grey = unpack_mipi10(raw, w, h, stride)
    code = cam.bayer or cv2.COLOR_BayerRG2BGR_EA
    try:
        bgr = cv2.cvtColor(grey, code)
    except cv2.error:
        bgr = cv2.cvtColor(grey, cv2.COLOR_BayerRG2BGR)
    return _orient(bgr, cam)


def frame_from_raw(cam: Cam, raw: bytes, w: int, h: int, stride: int) -> np.ndarray:
    if cam.ir:
        return finish_ir(unpack_mipi10_ir(raw, w, h, stride), cam)
    return finish_bgr(cam, unpack_mipi10(raw, w, h, stride))


def capture_raw(dev: str, count: int = 1, dest: str = "/tmp/surface-frame.raw", timeout: int = 25) -> bytes:
    if os.path.exists(dest):
        os.remove(dest)
    run(
        [
            "timeout", "-k", "3", str(timeout),
            "v4l2-ctl", "-d", dev,
            "--stream-mmap=4", f"--stream-count={count}",
            f"--stream-to={dest}", "--stream-poll",
        ],
        check=True,
        timeout=timeout + 5,
    )
    with open(dest, "rb") as f:
        return f.read()


def split_frames(raw: bytes, frame_len: int) -> list[bytes]:
    n = len(raw) // frame_len
    return [raw[i * frame_len : (i + 1) * frame_len] for i in range(n)]


def still(cam_name: str, w: Optional[int] = None, h: Optional[int] = None) -> np.ndarray:
    cam = CAMS[cam_name]
    if w is None or h is None:
        w, h = cam.modes[0]
    set_sensor_exposure(cam.sensor.split()[0], cam.exposure, cam.gain)
    dev = media_setup(cam, w, h)
    stride = get_stride(dev, w)
    fl = frame_bytes(stride, h, cam, w)

    if cam.ir:
        dest = "/tmp/ir-still.raw"
        if os.path.exists(dest):
            os.remove(dest)
        set_sensor_exposure("ov7251", cam.exposure, cam.gain)
        # LED only works after STREAMON (I2C Remote I/O before that)
        proc = subprocess.Popen(
            ["v4l2-ctl", "-d", dev, "--stream-mmap=4", "--stream-count=0",
             f"--stream-to={dest}", "--stream-poll"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        time.sleep(0.4)
        for _ in range(14):
            ir_led(True)
            time.sleep(0.12)
        time.sleep(0.2)
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
        ir_led(False)
        with open(dest, "rb") as f:
            raw = f.read()
        scored = []
        for chunk in split_frames(raw, fl):
            u16 = unpack_mipi10_u16(chunk, w, h, stride)
            raw_m = float(u16.mean())
            if raw_m < 80:
                continue
            img = frame_from_raw(cam, chunk, w, h, stride)
            m = float(img.mean())
            white = float((img > 245).mean() * 100.0)
            if white > 6:
                continue
            # prefer bright, little clip
            scored.append((-raw_m + white * 5.0, img))
        scored.sort(key=lambda x: x[0])
        imgs = [im for _, im in scored[:6]]
        if not imgs:
            best_c, best_m = None, -1.0
            for chunk in split_frames(raw, fl) or ([raw[:fl]] if len(raw) >= fl else []):
                u16 = unpack_mipi10_u16(chunk, w, h, stride)
                mm = float(u16.mean())
                if mm > best_m:
                    best_m, best_c = mm, chunk
            imgs = [frame_from_raw(cam, best_c, w, h, stride)] if best_c else []
        if not imgs:
            raise RuntimeError("ir: no bright frames (LED?)")
        stack = np.stack(imgs, axis=0).astype(np.float32)
        return np.clip(stack.mean(axis=0), 0, 255).astype(np.uint8)

    auto_expose(cam, dev, w, h, stride)

    # RGB: back — contrast-detect AF (DW9719), then fine
    if cam_name == "back" and find_focus_subdev():
        pos, af_raw = autofocus_back(cam, w, h, stride, dev)
        if af_raw:
            fine = list(range(max(64, pos - 80), min(1024, pos + 81), 16))
            if len(fine) > 2:
                pos2, af2 = autofocus_back(cam, w, h, stride, dev, fine)
                if af2:
                    af_raw = af2
                    pos = pos2
            print(f"AF focus={pos}", flush=True)
            return frame_from_raw(cam, af_raw, w, h, stride)

    imgs = []
    ntry = 3 if cam_name == "back" else 3
    for i in range(ntry):
        try:
            if cam_name == "front":
                # stream-count=N on ov5693 often hangs; continuous stream + kill
                dest = f"/tmp/still-{cam_name}-{i}.raw"
                if os.path.exists(dest):
                    os.remove(dest)
                proc = subprocess.Popen(
                    ["v4l2-ctl", "-d", dev, "--stream-mmap=6", "--stream-count=0",
                     f"--stream-to={dest}", "--stream-poll"],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                )
                time.sleep(1.2)
                proc.terminate()
                try:
                    proc.wait(timeout=4)
                except subprocess.TimeoutExpired:
                    proc.kill()
                with open(dest, "rb") as f:
                    raw = f.read()
            else:
                raw = capture_raw(dev, 1, f"/tmp/still-{cam_name}-{i}.raw", timeout=40)
        except (subprocess.CalledProcessError, OSError):
            if cam_name == "front" and (w, h) != (1296, 972):
                return still(cam_name, 1296, 972)
            if imgs:
                break
            raise
        chunks = split_frames(raw, fl)
        if not chunks and len(raw) >= fl - 8:
            chunks = [raw[:fl] if len(raw) >= fl else raw]
        if chunks:
            imgs.append(frame_from_raw(cam, chunks[-1], w, h, stride))
        time.sleep(0.08)
    if not imgs:
        raise RuntimeError(f"{cam_name}: no frames")
    if len(imgs) >= 2:
        stack = np.stack(imgs[-2:], axis=0).astype(np.float32)
        return np.clip(stack.mean(axis=0), 0, 255).astype(np.uint8)
    return imgs[-1]


def save_image(path: str, img: np.ndarray) -> None:
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    cv2.imwrite(path, img)


def bgr_or_grey_to_yuyv(img: np.ndarray) -> bytes:
    if img.ndim == 2:
        bgr = cv2.cvtColor(img, cv2.COLOR_GRAY2BGR)
    else:
        bgr = img
    yuv = cv2.cvtColor(bgr, cv2.COLOR_BGR2YUV)
    y, u, v = cv2.split(yuv)
    hh, ww = y.shape
    out = np.empty((hh, ww, 2), dtype=np.uint8)
    out[:, :, 0] = y
    out[:, 0::2, 1] = u[:, 0::2]
    out[:, 1::2, 1] = v[:, 1::2]
    return out.tobytes()


def write_mp4(path: str, frames: list[np.ndarray], fps: int = 12) -> None:
    if not frames:
        raise RuntimeError("no frames for MP4")
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    color = frames[0].ndim == 3
    h, w = frames[0].shape[:2]
    pix = "bgr24" if color else "gray"
    cmd = [
        "ffmpeg", "-y", "-loglevel", "error",
        "-f", "rawvideo", "-pix_fmt", pix, "-s", f"{w}x{h}", "-r", str(fps),
        "-i", "-",
        "-c:v", "libx264", "-preset", "veryfast", "-crf", "17",
        "-pix_fmt", "yuv420p", path,
    ]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    assert proc.stdin is not None
    for fr in frames:
        if fr.shape[0] != h or fr.shape[1] != w:
            fr = cv2.resize(fr, (w, h))
        if color and fr.ndim == 2:
            fr = cv2.cvtColor(fr, cv2.COLOR_GRAY2BGR)
        if not color and fr.ndim == 3:
            fr = cv2.cvtColor(fr, cv2.COLOR_BGR2GRAY)
        proc.stdin.write(np.ascontiguousarray(fr).tobytes())
    proc.stdin.close()
    err = proc.stderr.read().decode() if proc.stderr else ""
    rc = proc.wait(timeout=120)
    if rc != 0:
        raise RuntimeError(f"ffmpeg: {err or rc}")


def record_mp4(cam_name: str, path: str, seconds: float = 3.0, w=None, h=None, fps: int = 12) -> None:
    cam = CAMS[cam_name]
    if w is None or h is None:
        w, h = cam.modes[0]
    set_sensor_exposure(cam.sensor.split()[0], cam.exposure, cam.gain)
    dev = media_setup(cam, w, h)
    stride = get_stride(dev, w)
    fl = frame_bytes(stride, h, cam, w)
    nframes = max(1, int(seconds * fps))
    dest = f"/tmp/rec-{cam_name}.raw"

    if cam.ir:
        if os.path.exists(dest):
            os.remove(dest)
        proc = subprocess.Popen(
            ["v4l2-ctl", "-d", dev, "--stream-mmap=4", "--stream-count=0",
             f"--stream-to={dest}", "--stream-poll"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        time.sleep(0.3)
        t0 = time.time()
        while time.time() - t0 < seconds + 0.4:
            ir_led(True)
            time.sleep(0.2)
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
        ir_led(False)
        with open(dest, "rb") as f:
            raw = f.read()
        frames = []
        for chunk in split_frames(raw, fl):
            img = frame_from_raw(cam, chunk, w, h, stride)
            if float(img.mean()) < 45:
                continue
            frames.append(img)
            if len(frames) >= nframes:
                break
        if not frames:
            raise RuntimeError("IR: no bright frames")
        write_mp4(path, frames, fps=fps)
        return

    capture_raw(dev, nframes + 4, dest, timeout=max(50, nframes + 25))
    with open(dest, "rb") as f:
        raw = f.read()
    if len(raw) >= fl * 2 and len(raw) % fl != 0:
        for cand in (fl, fl + 4, stride * h, stride * h - 4):
            if cand > 0 and len(raw) % cand == 0:
                fl = cand
                break
    chunks = split_frames(raw, fl)
    if len(chunks) < 2:
        raise RuntimeError(f"{cam_name}: too few frames ({len(raw)} B, fl={fl})")
    use = chunks[3 : 3 + nframes] if len(chunks) > 3 else chunks
    frames = [frame_from_raw(cam, c, w, h, stride) for c in use]
    write_mp4(path, frames, fps=fps)


def iter_live_frames(cam_name: str, w: int, h: int, with_led: bool = True):
    cam = CAMS[cam_name]
    set_sensor_exposure(cam.sensor.split()[0], cam.exposure, cam.gain)
    dev = media_setup(cam, w, h)
    stride = get_stride(dev, w)
    fl = frame_bytes(stride, h, cam, w)
    dest = f"/tmp/live-{cam_name}.raw"
    if os.path.exists(dest):
        os.remove(dest)
    proc = subprocess.Popen(
        ["v4l2-ctl", "-d", dev, "--stream-mmap=4", "--stream-count=0",
         f"--stream-to={dest}", "--stream-poll"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    last_n = 0
    t_led = 0.0
    try:
        while True:
            if cam.ir and with_led and time.time() - t_led > 0.25:
                ir_led(True)
                t_led = time.time()
            time.sleep(0.04)
            if not os.path.exists(dest):
                continue
            sz = os.path.getsize(dest)
            n = sz // fl
            if n <= last_n:
                continue
            with open(dest, "rb") as f:
                f.seek((n - 1) * fl)
                chunk = f.read(fl)
            last_n = n
            if sz > fl * 30:
                with open(dest, "wb") as f:
                    f.write(chunk)
                last_n = 1
            img = frame_from_raw(cam, chunk, w, h, stride)
            if cam.ir and float(img.mean()) < 45:
                continue
            yield img
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()
        if cam.ir:
            ir_led(False)
        try:
            os.remove(dest)
        except OSError:
            pass
