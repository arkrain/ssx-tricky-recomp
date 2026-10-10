# Validation status — 2026-10-11

This source snapshot incorporates the Linux audio372 fixes and 136 patches
against the pinned recompilation toolkit. Prior Linux runtime checks cover
menus and both startup movies, completed courses at every venue, normal
saving/loading, race replay, and two-controller split screen with virtual pads.
Event/progression coverage is incomplete. These are scoped checks, not a claim
that every mode and platform is finished.

A Bluetooth Xbox controller was physically tested on Fedora: buttons, both
sticks, triggers, rumble, race controls and Bluetooth reconnect worked.
Internal rendering supports 1× through 4× and anisotropic filtering.

Audio: source tracing identified late writes into already-consumed PCM. Voice
locks, smooth production and completed-source publication protect consumption.
At 4×, 480,000 traced mono samples had zero stale-window changes and exact source
reads. Larger native Linux streaming buffers eliminated sustained race starvation.
59 five-second queue reports after startup had zero empty queues or submission
failures during menus, loading, riding and pause/resume. A startup resource/shader
gap caused one queue recovery. The tester confirms menu and race audio now sound
clean. All 58 regression tests passed; protected save hashes were unchanged.

Ubuntu 24.04/Mesa was exercised in an isolated container. Fedora/NVIDIA was used
for physical controller/audio checks. Windows cross-builds and Wine checks
covered movies, racing, saving/loading, resize and Unicode paths; latest Linux
buffer tuning retains original Windows values. Native Windows hardware and
PowerShell launching remain unverified.

For bug reports include OS/GPU/driver, render scale, event/course, input device,
and a description of what happened. Include only relevant sanitized log excerpts;
logs can contain local paths. Do not attach game assets or saves.

## Public checkout verification

The publication checkout fetched dependencies from their pinned upstream sources,
reconstructed all 136 patches, and regenerated every game-code file identically
to the validated working port. A fresh optimized build using the checked-in
linux-release preset passed all 58 CTest checks (39.06 seconds). Launcher dry-run,
source-only hygiene checks, offline DSP-input rejection tests, telemetry clock
tests and the synthetic section-tail test passed. No private build outputs are
needed to reproduce generation. Full runtime checks require local game files.
