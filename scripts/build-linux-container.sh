#!/bin/sh
# Build generated game sources with the isolated Debian toolchain.
set -eu
task_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ssx_build_dsp=${SSX_BUILD_DSP:-ON}
case "$ssx_build_dsp" in ON|OFF) ;; *) echo 'SSX_BUILD_DSP must be ON or OFF' >&2; exit 2;; esac
mkdir -p "$task_root/build/linux-portable"
exec podman run --rm --network=none --userns=keep-id \
  --env SSX_BUILD_DSP="$ssx_build_dsp" \
  --security-opt label=disable \
  -v "$task_root:/workspace:ro" \
  -v "$task_root/build/linux-portable:/workspace/build/linux-portable:rw" \
  localhost/ssx-tricky-builder:bookworm sh -eu -c '
    cmake -S /workspace -B /workspace/build/linux-portable -G Ninja \
      -DCMAKE_C_COMPILER=/usr/bin/gcc -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_FLAGS_RELEASE=-O2 -DBUILD_TESTING=ON -DSSX_ABI_CHECK=OFF \
      -DSSX_EXPERIMENTAL_DSP="$SSX_BUILD_DSP"
    cmake --build /workspace/build/linux-portable --parallel 4
    ctest --test-dir /workspace/build/linux-portable --output-on-failure
  '
