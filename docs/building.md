# Building and packaging

## Toolchain

CMake 3.28+, C++17, Qt 6, libobs/obs-frontend-api, and FFmpeg (`avcodec`, `avformat`, `avutil`, `swscale`). `buildspec.json` pins the official OBS dependency archives used by the Windows and macOS presets.

Windows and macOS link against the same FFmpeg runtime family that ships with OBS.

## Linux x86_64

Install Ninja, CMake 3.28+, a C++17 compiler, OBS development files, Qt 6, FFmpeg development files, zsh, and clang-format. On Debian/Ubuntu:

```text
libobs-dev
qt6-base-dev
libavcodec-dev
libavformat-dev
libavutil-dev
libswscale-dev
```

Build:

```sh
cmake --preset ubuntu-x86_64
cmake --build --preset ubuntu-x86_64
ctest --test-dir build_x86_64 --output-on-failure
cmake --build build_x86_64 --target package
```

The package will be in `release/`:

```sh
dpkg-deb -I release/obs-timelapse-*-linux-gnu.deb
dpkg-deb -c release/obs-timelapse-*-linux-gnu.deb
```

## macOS universal

The first configure downloads and verifies the OBS, Qt, and dependency archives named in `buildspec.json`.

```sh
cmake --preset macos
cmake --build --preset macos --config RelWithDebInfo
ctest --test-dir build_macos -C RelWithDebInfo --output-on-failure
cmake --install build_macos --config RelWithDebInfo \
  --prefix "$PWD/release/RelWithDebInfo"
```

Recent Xcode releases may omit `get-task-allow` from the RelWithDebInfo test executable, and the framework then refuses to load at process launch for library-validation reasons. Re-sign only the local test executable with the generated Debug test `.xcent` before rerunning CTest — don't apply that development entitlement to the distributed plugin.

The install step creates both `obs-timelapse.plugin` and `obs-timelapse.pkg`. Local builds use ad-hoc signing unless `CODESIGN_IDENT`, `CODESIGN_TEAM`, and the corresponding installer identity are configured.

Verify the artifact:

```sh
lipo -archs release/RelWithDebInfo/obs-timelapse.plugin/Contents/MacOS/obs-timelapse
codesign --verify --deep --strict --verbose=2 release/RelWithDebInfo/obs-timelapse.plugin
pkgutil --check-signature release/RelWithDebInfo/obs-timelapse.pkg
```

I didn't bother setting up notarization for macOS.

## Windows x64

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
ctest --test-dir build_x64 -C RelWithDebInfo --output-on-failure
cmake --install build_x64 --config RelWithDebInfo --prefix release\RelWithDebInfo
```

The bundle looks like this:

```text
obs-timelapse\
├── bin\64bit\obs-timelapse.dll
└── data\locale\en-US.ini
```

For the installation-root ZIP, place the DLL and data into this archive layout:

```text
obs-plugins\64bit\obs-timelapse.dll
data\obs-plugins\obs-timelapse\locale\en-US.ini
```

Run the core test executable on native Windows.

## Source checks

```sh
FORMATTER_NAME=clang zsh build-aux/.run-format.zsh --check --fail-error
git diff --check
```

