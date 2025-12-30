from __future__ import annotations

from glob import glob
from pathlib import Path
from time import time
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
import multiprocessing


def detect_stage(src: Path) -> str | None:
    name = src.name
    if name.endswith(".vs.hlsl"):
        return "vs_6_0"
    if name.endswith(".ps.hlsl"):
        return "ps_6_0"
    return None


def find_dxc() -> str:
    # Prefer explicit override
    dxc = os.environ.get("DXC")
    if dxc:
        return dxc

    # On Windows, prefer Vulkan SDK DXC if available
    vulkan_sdk = os.environ.get("VULKAN_SDK")
    if vulkan_sdk:
        cand = Path(vulkan_sdk) / "Bin" / "dxc.exe"
        if cand.exists():
            return str(cand)

    # Fallback to PATH
    return "dxc"


def compile_shader(src_str: str) -> tuple[str, bool, int]:
    src = Path(src_str)

    stage = detect_stage(src)
    if stage is None:
        print(f"[HLSL] skipping {src} (unknown stage)", file=sys.stderr)
        return src_str, False, 0

    # Put output under the same folder as the shader: <dir>/build/<file>.spv
    out_dir = src.parent / "build"
    out_dir.mkdir(parents=True, exist_ok=True)
    outfile = out_dir / (src.name + ".spv")

    dxc = find_dxc()

    cmd = [
        dxc,
        "-spirv",
        "-fspv-target-env=vulkan1.3",
        "-fvk-use-dx-layout",
        "-Zi",
        "-Qembed_debug",
        "-fspv-debug=vulkan-with-source",
        "-Od",
        "-T", stage,
        "-E", "main",
        "-Fo", str(outfile),
        str(src),
    ]

    print(f"[HLSL] {src} -> {outfile} ({stage})")
    begin = int(time() * 1000)

    proc = subprocess.run(cmd, check=False, capture_output=True, text=True)

    end = int(time() * 1000)
    ms = end - begin

    ok = proc.returncode == 0
    print(f"  [{src}] {ms}ms (returncode={proc.returncode})")

    if not ok:
        if proc.stdout:
            print(proc.stdout, file=sys.stderr)
        if proc.stderr:
            print(proc.stderr, file=sys.stderr)

    # Extra sanity: even if returncode==0, verify the file exists
    if ok and not outfile.exists():
        print(f"[HLSL] dxc succeeded but output missing: {outfile}", file=sys.stderr)
        ok = False

    return src_str, ok, ms


def main() -> int:
    # Use forward slashes in the glob pattern; glob() handles it on Windows too.
    srcs = glob("nyla/**/shaders/*.hlsl", recursive=True)
    if not srcs:
        print("No HLSL shaders found under nyla/**/shaders", file=sys.stderr)
        return 0

    max_workers = max(1, multiprocessing.cpu_count())
    print(f"Compiling {len(srcs)} shaders with up to {max_workers} workers...")

    failures = 0
    with ThreadPoolExecutor(max_workers=max_workers) as executor:
        futures = [executor.submit(compile_shader, s) for s in srcs]
        for fut in as_completed(futures):
            _, ok, _ = fut.result()
            if not ok:
                failures += 1

    if failures:
        print(f"[HLSL] {failures} shader(s) failed to compile", file=sys.stderr)
        return 1

    print("[HLSL] All shaders compiled successfully")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())