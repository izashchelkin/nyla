bash scripts/build_wm.sh

mv ~/wm ~/wmold
cp ./build/linux-release/bin/wm ~

mv ~/terminal ~/terminal_old
cp ./build/linux-release/bin/terminal ~

mv ~/assets.bin ~/assets.bin.old
cp ./assets.bin ~