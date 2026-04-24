bash scripts/build_wm.sh

mv ~/wm ~/wmold
cp ./build/linux-debug/bin/wm ~

mv ~/wm_overlay ~/wm_overlay_old
cp ./build/linux-release/bin/wm_overlay ~