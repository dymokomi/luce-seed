#!/usr/bin/env python3
"""Record the actual host, compiler versions and dependency revisions used by a gate."""
import json
import os
from pathlib import Path
import platform
import subprocess


def command(args, cwd=None):
    try:
        r = subprocess.run(args, cwd=cwd, capture_output=True, text=True, timeout=10)
        return r.stdout.strip() if r.returncode == 0 else r.stderr.strip()
    except OSError as error:
        return str(error)


host = ("arm64" if platform.machine() in ("arm64", "aarch64") else platform.machine()) + ("-macos" if platform.system() == "Darwin" else "-linux")
expected = os.environ.get("LUCE_CI_HOST", host)
if host != expected:
    raise SystemExit(f"wrong runner architecture: expected {expected}, got {host}")
paths = [Path("."), Path("../luce-seed"), Path("../luce-base"), Path("build/luce-base")]
repos = []
for path in paths:
    if (path / ".git").exists():
        repos.append(dict(path=str(path.resolve()), revision=command(["git", "rev-parse", "HEAD"], path),
                          changes=command(["git", "status", "--porcelain"], path),
                          version=(path / "VERSION").read_text().strip()))
pins = {str(p): p.read_text().strip() for p in (Path("bootstrap/SEED"), Path("bootstrap/BASE")) if p.exists()}
print(json.dumps(dict(host=host, os=platform.platform(), cc=command(["cc", "--version"]),
                     python=platform.python_version(), cmake=command(["cmake", "--version"]),
                     pins=pins, repositories=repos), indent=2))
