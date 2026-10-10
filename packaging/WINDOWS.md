# Experimental Windows cross-build

Windows is not a validated native release target. Earlier MinGW/Wine checks
covered movies, a complete Garibaldi race, pause, save/load, resize and Unicode
paths. Native Windows hardware, controller/audio behavior and PowerShell
launching remain unverified. See [validation status](../docs/STATUS.md).

Supply the supported XBE and run the same bootstrap and analysis stages as
Linux first. The original game, generated C and prepared DSP sources must be
available locally. With Podman installed:

```sh
podman build -t localhost/ssx-tricky-windows-builder:24.04 \
  -f packaging/Containerfile.windows packaging
sh scripts/build-windows-container.sh
```

The base image and libepoxy, SDL2 and FFmpeg archives are pinned; apt package
versions are not. The build runs offline with source mounted read-only.
`SSX_WINDOWS_BUILD_DIR` selects another relative build directory, and
`SSX_PODMAN` selects an alternate Podman executable. A host GCC generates vertex
programs; MinGW compiles them. The application includes a UTF-8 manifest.

To stage a local runtime with its DLLs and notices:

```sh
podman run --rm --network=none --userns=keep-id --security-opt label=disable \
  -v "$PWD:/workspace:ro" \
  -v "$PWD/build/windows-cross:/workspace/build/windows-cross:rw" \
  localhost/ssx-tricky-windows-builder:24.04 \
  python3 /workspace/scripts/package-windows.py \
  --binary /workspace/build/windows-cross/ssx_tricky.exe \
  --output /workspace/build/windows-cross/bundle
```

An existing output directory is refused. Inspect the staged README and launcher
for local use. Do not publish generated game-derived executables or include
original game assets in public uploads. No Windows binaries are distributed
with this source repository. These instructions describe the prototype workflow,
not a promise of native Windows support.
