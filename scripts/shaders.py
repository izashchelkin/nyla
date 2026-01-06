from __future__ import annotations

from glob import glob
from pathlib import Path
from time import time
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
import multiprocessing


def detect_stage_profile(src: Path) -> str | None:
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


def run_cmd(cmd: list[str]) -> tuple[bool, str, str, int]:
    proc = subprocess.run(cmd, check=False, capture_output=True, text=True)
    return proc.returncode == 0, proc.stdout, proc.stderr, proc.returncode


def compile_spirv(dxc: str, src: Path, profile: str, out_dir: Path) -> tuple[bool, int]:
    outfile = out_dir / (src.name + ".spv")

    cmd = [
        dxc,
        "-spirv",
        "-DSPIRV=1",
        "-fspv-target-env=vulkan1.3",
        "-fvk-use-dx-layout",
        "-Zi",
        "-Qembed_debug",
        "-fspv-debug=vulkan-with-source",
        "-Od",
        "-T", profile,
        "-E", "main",
        "-Fo", str(outfile),
        str(src),
    ]

    begin = int(time() * 1000)
    ok, stdout, stderr, rc = run_cmd(cmd)
    ms = int(time() * 1000) - begin

    if not ok:
        print(f"[SPIRV] FAIL {src} (rc={rc})", file=sys.stderr)
        if stdout:
            print(stdout, file=sys.stderr)
        if stderr:
            print(stderr, file=sys.stderr)
        return False, ms

    if not outfile.exists():
        print(f"[SPIRV] dxc succeeded but output missing: {outfile}", file=sys.stderr)
        return False, ms

    print(f"[SPIRV] {src} -> {outfile} ({profile}) {ms}ms")
    return True, ms


def compile_dxil(dxc: str, src: Path, profile: str, out_dir: Path) -> tuple[bool, int]:
    outfile = out_dir / (src.name + ".dxil")

    cmd = [
        dxc,
        "-Zi",
        "-Qembed_debug",
        "-Od",
        "-T", profile,
        "-E", "main",
        "-Fo", str(outfile),
        str(src),
    ]

    begin = int(time() * 1000)
    ok, stdout, stderr, rc = run_cmd(cmd)
    ms = int(time() * 1000) - begin

    if not ok:
        print(f"[DXIL] FAIL {src} (rc={rc})", file=sys.stderr)
        if stdout:
            print(stdout, file=sys.stderr)
        if stderr:
            print(stderr, file=sys.stderr)
        return False, ms

    if not outfile.exists():
        print(f"[DXIL] dxc succeeded but output missing: {outfile}", file=sys.stderr)
        return False, ms

    print(f"[DXIL]  {src} -> {outfile} ({profile}) {ms}ms")
    return True, ms


def compile_shader(src_str: str) -> tuple[str, bool, int]:
    src = Path(src_str)

    profile = detect_stage_profile(src)
    if profile is None:
        print(f"[HLSL] skipping {src} (unknown stage)")
        return src_str, True, 0

    dxc = find_dxc()

    out_dir = src.parent / "build"
    out_dir.mkdir(parents=True, exist_ok=True)

    ok_spv, ms_spv = compile_spirv(dxc, src, profile, out_dir)
    ok_dxil, ms_dxil = compile_dxil(dxc, src, profile, out_dir)

    ok = ok_spv and ok_dxil
    return src_str, ok, ms_spv + ms_dxil


def main() -> int:
    srcs = glob("nyla/**/shaders/*.hlsl", recursive=True)
    if not srcs:
        print("No HLSL shaders found under nyla/**/shaders", file=sys.stderr)
        return 0

    max_workers = max(1, multiprocessing.cpu_count())
    print(f"Compiling {len(srcs)} shaders (SPIR-V + DXIL) with up to {max_workers} workers...")

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