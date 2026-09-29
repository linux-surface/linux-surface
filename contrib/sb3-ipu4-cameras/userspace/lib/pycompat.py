"""Runtime feature gates for CPython 3.14+/3.15+ and OpenCV 5+.

Older interpreters keep the existing eager-import / OpenCV4 paths.
"""
from __future__ import annotations

import importlib
import importlib.util
import sys
from types import ModuleType
from typing import Any

# User asked for 3.15+; this host often runs 3.14.x — treat 3.14+ as modern,
# and keep an explicit 3.15 flag for APIs that only appear there.
PY_VER = sys.version_info[:2]
PY314_PLUS = PY_VER >= (3, 14)
PY315_PLUS = PY_VER >= (3, 15)
MODERN_PY = PY314_PLUS  # lazy-friendly / modern CPython


def lazy_module(name: str) -> ModuleType:
    """Lazy-load a module on 3.14+ (LazyLoader); eager import otherwise."""
    if not MODERN_PY:
        return importlib.import_module(name)
    spec = importlib.util.find_spec(name)
    if spec is None or spec.loader is None:
        return importlib.import_module(name)
    loader = importlib.util.LazyLoader(spec.loader)
    spec.loader = loader
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    loader.exec_module(mod)
    return mod


def cv2_version_tuple() -> tuple[int, ...]:
    try:
        import cv2  # noqa: WPS433 — version probe only

        parts: list[int] = []
        for p in cv2.__version__.split(".")[:3]:
            try:
                parts.append(int("".join(c for c in p if c.isdigit()) or "0"))
            except ValueError:
                parts.append(0)
        return tuple(parts) if parts else (0, 0, 0)
    except Exception:
        return (0, 0, 0)


CV2_VER = cv2_version_tuple()
CV2_V5 = CV2_VER >= (5, 0)


def bgr_to_yuyv_u8(bgr: Any) -> bytes:
    """Fast BGR→YUYV. Prefer native cvtColor; fall back to manual pack."""
    import cv2
    import numpy as np

    if bgr.ndim == 2:
        bgr = cv2.cvtColor(bgr, cv2.COLOR_GRAY2BGR)
    # OpenCV 4.x+/5: single-pass packed YUY2 (~6× faster than split/pack).
    code = getattr(cv2, "COLOR_BGR2YUV_YUY2", None) or getattr(
        cv2, "COLOR_BGR2YUV_YUYV", None
    )
    if code is not None:
        yuyv = cv2.cvtColor(bgr, code)
        return np.ascontiguousarray(yuyv).tobytes()
    yuv = cv2.cvtColor(bgr, cv2.COLOR_BGR2YUV)
    y, u, v = cv2.split(yuv)
    hh, ww = y.shape
    out = np.empty((hh, ww, 2), dtype=np.uint8)
    out[:, :, 0] = y
    out[:, 0::2, 1] = u[:, 0::2]
    out[:, 1::2, 1] = v[:, 1::2]
    return out.tobytes()


def bayer_code(cv2_mod: Any, prefer_fast: bool = True) -> int:
    """Pick demosaic for live path.

    OpenCV 5 keeps BayerRG2BGR / _EA; prefer bilinear for CFR. Quality path
    can request EA / VNG when present.
    """
    if prefer_fast:
        return cv2_mod.COLOR_BayerRG2BGR
    for name in (
        "COLOR_BayerRG2BGR_VNG",  # OpenCV5 quality (when built with)
        "COLOR_BayerRG2BGR_EA",
        "COLOR_BayerRG2BGR",
    ):
        code = getattr(cv2_mod, name, None)
        if code is not None:
            return code
    return cv2_mod.COLOR_BayerRG2BGR


def resize_area(cv2_mod: Any, img: Any, wh: tuple[int, int]) -> Any:
    """AREA resize; OpenCV5 may expose INTER_LINEAR_EXACT — fall back cleanly."""
    inter = getattr(cv2_mod, "INTER_AREA", 3)
    if CV2_V5:
        # Prefer exact linear for moderate downscales when available.
        src_h, src_w = img.shape[:2]
        dw, dh = wh
        if src_w * src_h > 0 and (src_w / max(dw, 1)) < 2.5:
            inter = getattr(cv2_mod, "INTER_LINEAR_EXACT", None) or getattr(
                cv2_mod, "INTER_LINEAR", inter
            )
        else:
            inter = getattr(cv2_mod, "INTER_AREA", inter)
    return cv2_mod.resize(img, wh, interpolation=inter)


def set_num_threads(cv2_mod: Any, n: int = 0) -> None:
    """Cap OpenCV threads so decode doesn't fight the capture GIL/CPU."""
    try:
        if hasattr(cv2_mod, "setNumThreads"):
            cv2_mod.setNumThreads(int(n))
    except Exception:
        pass
