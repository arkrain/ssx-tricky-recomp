# Isolated Linux builder

Use this Debian toolchain when your host development packages differ, including
on Fedora. Install Podman first. Build with only the Containerfile as context so
private game files and local reports are never sent to the container builder:

```sh
mkdir -p /tmp/ssx-tricky-builder
cp packaging/Containerfile.linux /tmp/ssx-tricky-builder/Containerfile
podman build -t localhost/ssx-tricky-builder:bookworm /tmp/ssx-tricky-builder
```

The official Python/Debian base is pinned by digest. Package repositories remain
live; the base digest does not pin later package updates. Run these commands
from the repository directory. Bootstrap needs network access:

```sh
podman run --rm --userns=keep-id --security-opt label=disable \
  -v "$PWD:/workspace:rw" localhost/ssx-tricky-builder:bookworm \
  python3 scripts/pipeline.py bootstrap
```

Put your extracted supported game in `game/`. To extract your own XISO instead,
place it in `iso/game.xiso.iso`, then use the same container command with
`python3 scripts/pipeline.py extract --iso /workspace/iso/game.xiso.iso`.
The extractor refuses to overwrite an existing game directory.

Generate the private game code, then build and test offline:

```sh
podman run --rm --network=none --userns=keep-id --security-opt label=disable \
  -v "$PWD:/workspace:rw" localhost/ssx-tricky-builder:bookworm \
  python3 scripts/pipeline.py analyze
sh scripts/build-linux-container.sh
```

The build script mounts source read-only and only `build/linux-portable` writable.
Stage a local bundle to use the builder's library versions on your Linux host:

```sh
podman run --rm --network=none --userns=keep-id --security-opt label=disable \
  -v "$PWD:/workspace:ro" \
  -v "$PWD/build/linux-portable:/workspace/build/linux-portable:rw" \
  localhost/ssx-tricky-builder:bookworm \
  python3 scripts/package-linux.py \
  --binary build/linux-portable/ssx_tricky \
  --output build/linux-portable/bundle
./build/linux-portable/bundle/play --game-dir "$PWD/game" --save-dir "$PWD/saves"
```

Existing bundle outputs are never overwritten. The bundle requires host Python3,
x86-64 Linux, glibc2.36+, a Debian12-era or newer C++ runtime, and working
EGL/OpenGL drivers. Host graphics drivers, glibc and C++ runtime are not bundled.
No game assets or saves are copied by the staging script. Generated game-derived
executables are local outputs; do not upload them in public releases.

Validate your host's actual rendering, audio, controls and saving. Full gameplay
coverage remains experimental; see [status](../docs/STATUS.md).
