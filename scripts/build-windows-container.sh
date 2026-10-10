#!/bin/sh
# Optional Win64 prototype. Run-time validation is separate from cross-building.
set -eu
task_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ssx_windows_build=${SSX_WINDOWS_BUILD_DIR:-build/windows-cross}
case "$ssx_windows_build" in /*|*..*) echo 'Use a relative build directory inside the workspace.' >&2; exit 2;; esac
mkdir -p "$task_root/$ssx_windows_build"
exec "${SSX_PODMAN:-podman}" run --rm --network=none --userns=keep-id --security-opt label=disable \
    --env PKG_CONFIG_LIBDIR=/opt/windows-prefix/lib/pkgconfig \
    --env SSX_WINDOWS_BUILD_DIR="$ssx_windows_build" \
    -v "$task_root:/workspace:ro" \
    -v "$task_root/$ssx_windows_build:/workspace/$ssx_windows_build:rw" \
    localhost/ssx-tricky-windows-builder:24.04 sh -eu -c '
    cmake -S /workspace -B "/workspace/$SSX_WINDOWS_BUILD_DIR" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=/workspace/packaging/mingw64-toolchain.cmake \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS_RELEASE=-O2 \
        -DSSX_EXPERIMENTAL_DSP=ON -DSSX_WINDOWS_OPENGL=ON -DSSX_WINDOWS_MOVIES=ON -DBUILD_TESTING=ON \
        -DSSX_WINDOWS_DEPENDENCY_ROOT=/opt/windows-prefix \
        -DSDL2_DIR=/opt/windows-prefix/lib/cmake/SDL2 \
        -DSSX_EPOXY_INCLUDE_DIR=/opt/windows-prefix/include \
        -DSSX_EPOXY_LIBRARY=/opt/windows-prefix/lib/libepoxy.dll.a
    cmake --build "/workspace/$SSX_WINDOWS_BUILD_DIR" --parallel 4 \
        --target ssx_tricky windows_input_test windows_files_test windows_window_test gl_raster_test vertex_native_test movie_probe
    '
