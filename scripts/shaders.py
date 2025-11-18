from glob import glob
from os import system
from time import time

def main():
    srcs = (glob("nyla/*/shaders/*.frag") + glob("nyla/*/shaders/*.vert"))
    for src in srcs:
        parts = src.split("/")
        parts.insert(len(parts) - 1, "build")
        outfile = "/".join(parts) + ".spv"

        begin = int(time() * 1000)
        print(src, end=" ")
        system("glslc " + src + " -o " + outfile)
        end = int(time() * 1000)
        print(str(end - begin) + "ms")

if __name__ == "__main__":
    main()