# OBS Timelapse

This is an OBS Studio plugin that samples the program output at a fixed interval and turns it into a timelapse — either a folder of lossless PNG frames for stitching later, or a ready-to-play video-only H.264 MKV.

It runs on Linux x86_64, macOS (Apple Silicon and Intel), and Windows x64, and is written in C++17 against the official OBS plugin API. It adds **Tools → OBS Timelapse…**, plus a compact Start/Stop button in OBS's Controls dock when the layout allows it.

## Highlights

- Captures the composed program scene: transforms, filters, transitions, browser sources, and the Program side of Studio Mode.
- Keeps PNG compression, disk I/O, H.264 encoding, and MKV muxing off OBS's video callback.
- Uses a fixed-size, preallocated frame queue. If your storage can't keep up, samples are dropped and counted instead of blocking OBS or eating memory.
- Writes PNG frames atomically, and keeps the MKV named `timelapse.mkv.partial` until the encoder and container are fully finalized — you can always tell a finished file from an interrupted one.
- Records timing, frame counts, completion status, errors, and the stop reason in `session.json`.
- Uses the Qt and FFmpeg libraries that come with OBS. There's no codec pack to install.

## Install

Close OBS before installing or updating, then grab the artifact for your platform.

### Linux (Ubuntu 26.04 x86_64 package)

```sh
sudo apt install ./obs-timelapse-[release]-x86_64-linux-gnu.deb
```

APT pulls in the exact OBS, Qt 6, and FFmpeg runtime libraries recorded in the package metadata. Remove it with `sudo apt remove obs-timelapse`.

### Arch Linux x86_64

```sh
sudo pacman -U ./obs-timelapse-[release]-1-x86_64.pkg.tar.zst
```

The package targets the current Arch `obs-studio`, Qt 6, and FFmpeg packages. Remove it with `sudo pacman -R obs-timelapse`.

### macOS (Apple Silicon and Intel)

Open `obs-timelapse-[release]-macos-universal.pkg` and follow the installer. The development package isn't Developer ID signed or notarized, so Gatekeeper may balk — Control-click the package, choose **Open**, and confirm once.

For a user-only manual install, extract `obs-timelapse-[release]-macos-universal.tar.xz` and copy `obs-timelapse.plugin` to:

```text
~/Library/Application Support/obs-studio/plugins/
```

Delete that bundle to uninstall. The binary is universal (`arm64` and `x86_64`).

### Windows x64

Extract `obs-timelapse-[release]-windows-x64.zip` into the OBS installation folder — normally `C:\Program Files\obs-studio` — and let Windows merge the `obs-plugins` and `data` directories. For portable OBS, extract into the portable root instead.

To uninstall, delete just these files:

```text
obs-plugins\64bit\obs-timelapse.dll
data\obs-plugins\obs-timelapse\
```

OBS 32 needs a current Microsoft Visual C++ v14 x64 Redistributable; the regular OBS installer normally provides it.

Restart OBS and open **Tools → OBS Timelapse…**. The [user guide](docs/user-guide.md) covers settings, safe operation, PNG stitching, and recovery.

## Output at a glance

Every run gets its own UTC-timestamped directory:

```text
2026-07-18_01-12-24-studio-build/
├── session.json
├── frame_00000001.png       # PNG mode
├── frame_00000002.png
└── ...
```

or:

```text
2026-07-18_01-12-24-studio-build/
├── session.json
└── timelapse.mkv            # MKV mode after a clean stop
```

While capture is running, the MKV is named `timelapse.mkv.partial`. A leftover `.partial` means the session never finished cleanly — it's deliberately not dressed up as a complete movie.

## Development

The repository is based on the official OBS plugin template, with the downloaded Windows/macOS SDKs pinned to OBS 31.1.1. Runtime testing also covers OBS 32.1.x.

- [Build and package locally](docs/building.md)
- [Architecture and safety invariants](docs/architecture.md)
- [Test strategy and current acceptance matrix](docs/testing.md)

Quick Linux development loop:

```sh
cmake --preset ubuntu-x86_64
cmake --build --preset ubuntu-x86_64
ctest --test-dir build_x86_64 --output-on-failure
cmake --build build_x86_64 --target package
```

Formatting and source checks:

```sh
FORMATTER_NAME=clang zsh build-aux/.run-format.zsh --check --fail-error
git diff --check
```

Everything builds and releases from a local machine — there's no hosted CI/CD, release workflow, webhook, or package-publishing automation.

## License

GPL-2.0-or-later.
