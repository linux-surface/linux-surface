# SPDX-License-Identifier: MIT
"""Render the final (not staging) executable path as a quoted systemd argument."""
from pathlib import Path
import sys

binary, output = sys.argv[1:]
if not Path(binary).is_absolute() or any(ord(char) < 32 for char in binary):
    raise SystemExit("bindir must be an absolute path with no control characters")
escaped = binary.replace("\\", "\\\\").replace('"', '\\"').replace("%", "%%").replace("$", "$$")
unit = Path(__file__).with_name("surface-laptop1-ir.service.in").read_text()
target = Path(output)
target.write_text(unit.replace("@LAUNCHER@", escaped))
target.chmod(0o644)
