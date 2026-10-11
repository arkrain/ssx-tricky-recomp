# SSX Tricky Recomp

A Linux-first native recompilation of the original Xbox US retail release of
**SSX Tricky**. Build it from your own extracted game files, then play with a
keyboard or SDL-compatible controller. No game files, saves, generated game
code, or game-derived executables are distributed here.

This is an experimental community project, unaffiliated with EA. Linux menus,
movies, races, saving/loading, higher-resolution rendering and a Bluetooth Xbox
controller have been tested. The latest Linux audio changes pass 58 regression
checks, and menu/race crackling is confirmed resolved by the tester. Remaining
event coverage and long-session behavior are still being checked. Windows
cross-builds have been exercised under Wine; native Windows remains unverified.

## Requirements

- An x86-64 Linux machine and OpenGL/EGL graphics drivers.
- Your own original Xbox **US retail** game dump. The supported `default.xbe`
  SHA-256 is `0128bbf6f86cc27307156f9206245115a9950541317d05b7037cab6ef063c84a`.
- Python 3, Git, CMake 3.20+, Ninja, a C11 compiler, and the development libraries
  listed below. Other game releases are rejected by the launcher.

## Build on Ubuntu / Debian

```sh
sudo apt update
sudo apt install build-essential cmake ninja-build git pkg-config python3-venv \
  libsdl2-dev libepoxy-dev libegl1-mesa-dev libssl-dev \
  libavformat-dev libavcodec-dev libswscale-dev libswresample-dev libavutil-dev

git clone https://github.com/reindance/ssx-tricky-recomp.git
cd ssx-tricky-recomp
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install -r requirements.txt
python3 scripts/pipeline.py bootstrap
```

Bootstrap downloads pinned open-source dependencies and applies the checked-in
patch series. Put your extracted game's files directly in `game/`, with
`game/default.xbe` present. Alternatively, extract your own XISO:

```sh
python3 scripts/pipeline.py extract --iso /path/to/your-game.xiso.iso
```

Extraction refuses to overwrite an existing `game/`. Then generate and build:

```sh
python3 scripts/pipeline.py analyze
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release
./play
```

Generation and compilation take time and several gigabytes of disk space.
Analysis logs are written locally to `reports/`. Keep the virtual environment
active when generating or building. Game code and embedded shader programs are
created locally from your XBE, not downloaded from this repository.

## Play

From the repository directory:

```sh
./play
./play --render-scale 4 --anisotropy 16
./play --skip-intro
```

The default renderer uses the original 640×480 resolution. Multipliers 2, 3 and 4
render internally at 1280×960, 1920×1440 and 2560×1920. Start at 1× if your GPU
struggles. Audio uses the real GP/EP DSP interpreter by default. The Linux
software mixer now prepares 60 ms ahead in a 100 ms ring to tolerate refill
jitter; this adds latency compared with the original buffer settings.

Saves go in `saves/`. You can choose directories with `--game-dir` and
`--save-dir`; run `./play --help` for options. Back up saves before changing
builds. The launcher verifies the supported XBE before starting.

**Keyboard:** Enter pauses / maps to Start; Space selects / crouches (release
to jump); Escape goes back / maps to B; W/S accelerate/brake; A/D steer;
Backspace resets the rider; R/Q/E grab. Arrows map to the D-pad and I/J/K/L to
the right stick. Controls in races depend on the game's selected configuration.

**Controller:** plug in or pair an SDL-compatible gamepad before launching.
A Bluetooth Xbox controller's buttons, sticks, triggers, rumble and reconnect
were physically checked on Fedora. Other models may need an SDL mapping in
`scripts/controller-mappings.txt`.

## Fedora and Windows

For Fedora, use the equivalent development packages or the isolated Debian
builder in [packaging/README.md](packaging/README.md). FFmpeg development packages
may require a repository that supplies them. Ubuntu 24.04/Mesa and Fedora/NVIDIA
have been exercised; other host configurations may need further testing.

Experimental Windows build instructions are in
[packaging/WINDOWS.md](packaging/WINDOWS.md). Native Windows hardware is not yet a
supported, validated release target.

## Status, contributions and licensing

See [validation status](docs/STATUS.md) and
[contributing](CONTRIBUTING.md). Public CI checks source preparation and hygiene;
full game builds and gameplay checks require a locally supplied game dump.

Project-authored code and patches are GPL-2.0-or-later. Upstream components
retain their own licenses; see [third-party notices](THIRD_PARTY.md) and
[LICENSE](LICENSE). Game assets and original game code remain their owners'
property and are outside this project's license. Do not upload game dumps,
generated game code, saves, or recordings to issues or pull requests.
