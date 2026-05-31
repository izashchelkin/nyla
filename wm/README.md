infinite strip tiling window manager for X11

## Build

```sh
cmake --preset linux-release
cmake --build build/linux-release --target wm
```

## Key bindings

See `grabKey` calls in `UserMain`.

## Test

```sh
./scripts/test_wm.sh
```