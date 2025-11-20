from glob import glob
from os import makedirs
from os.path import dirname
from time import time
import subprocess

def main():
    srcs = (
        glob("nyla/**/shaders/*.frag", recursive=True) +
        glob("nyla/**/shaders/*.vert", recursive=True)
    )

    for src in srcs:
        parts = src.split("/")
        parts.insert(len(parts) - 1, "build")
        outfile = "/".join(parts) + ".spv"

        makedirs(dirname(outfile), exist_ok=True)

        begin = int(time() * 1000)

        cmd = ["glslangValidator", "-V", "-gVS", "-o", outfile, src]
        subprocess.run(cmd, check=False)

        end = int(time() * 1000)
        print(str(end - begin) + "ms")

if __name__ == "__main__":
    main()