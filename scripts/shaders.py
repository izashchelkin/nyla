from glob import glob
from os import makedirs
from os.path import dirname
from time import time
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
import multiprocessing


def detect_stage(src: str) -> str | None:
    if src.endswith(".vs.hlsl"):
        return "vs_6_0"
    if src.endswith(".ps.hlsl"):
        return "ps_6_0"
    return None


def compile_shader(src: str) -> tuple[str, bool, int]:
    """Compile a single HLSL file with DXC. Returns (src, success, ms)."""
    parts = src.split("/")
    parts.insert(len(parts) - 1, "build")
    outfile = "/".join(parts) + ".spv"

    makedirs(dirname(outfile), exist_ok=True)

    stage = detect_stage(src)
    if stage is None:
        print(f"[HLSL] skipping {src} (unknown stage)", file=sys.stderr)
        return src, False, 0

    cmd = [
        "dxc",
        "-spirv",
        "-fspv-target-env=vulkan1.3",
        "-fvk-use-dx-layout",
        "-T",
        stage,
        "-E",
        "main",
        "-Fo",
        outfile,
        src,
    ]

    print(f"[HLSL] {src} -> {outfile} ({stage})")
    begin = int(time() * 1000)

    proc = subprocess.run(cmd, check=False)
    end = int(time() * 1000)

    ok = proc.returncode == 0
    ms = end - begin
    print(f"  [{src}] {ms}ms (returncode={proc.returncode})")

    return src, ok, ms


def main() -> int:
    srcs = glob("nyla/**/shaders/*.hlsl", recursive=True)
    if not srcs:
        print("No HLSL shaders found under nyla/**/shaders", file=sys.stderr)
        return 0

    max_workers = max(1, multiprocessing.cpu_count())

    print(f"Compiling {len(srcs)} shaders with up to {max_workers} workers...")

    failures = 0

    with ThreadPoolExecutor(max_workers=max_workers) as executor:
        future_to_src = {executor.submit(compile_shader, src): src for src in srcs}

        for future in as_completed(future_to_src):
            src = future_to_src[future]
            try:
                _, ok, _ = future.result()
                if not ok:
                    failures += 1
            except Exception as e:
                print(f"[HLSL] Exception while compiling {src}: {e}", file=sys.stderr)
                failures += 1

    if failures:
        print(f"[HLSL] {failures} shader(s) failed to compile", file=sys.stderr)
    else:
        print("[HLSL] All shaders compiled successfully")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
