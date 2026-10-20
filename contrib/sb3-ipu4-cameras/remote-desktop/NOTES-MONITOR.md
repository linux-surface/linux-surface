# ScreenCast MONITOR vs VIRTUAL (7.2 / Wayland KDE)

Chrome Remote Desktop’s host may request a **VIRTUAL** monitor (`types=4`) when
`create_virtual_monitor` is enabled. On an **existing** Plasma Wayland session
that has no virtual output, portal `RemoteDesktop.Start` fails with a missing
output and the stream never starts.

For **existing-session** share, prefer ScreenCast **MONITOR** (`types=1`).

This overlay does **not** ship a binary patch of Google’s
`libremoting_core.so`. If your host still forces VIRTUAL, patch or configure
the host locally so ScreenCast requests MONITOR for the attached desktop.

Verified on 7.2.11-surface-ipu4+: with MONITOR forced, Start succeeds, a stream
is added, and the host returns to “ready” after disconnect.
