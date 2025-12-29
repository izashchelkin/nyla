# cmake --preset linux-debug
# cmake --preset linux-release
ninja -C build/linux-debug wm
ninja -C build/linux-release wm_overlay