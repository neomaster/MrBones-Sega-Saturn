# mr-bones-port

Native Windows port of *Mr. Bones* (Sega Saturn, USA, MK-81016 V1.000).
No game data is included: provide your own Disc 1 and Disc 2 images.

**Status:** playable native Windows port covering the full 24-entry campaign (22 native playable stages across Disc 1 & Disc 2 plus `TROLL1`/`TROLL2` empty skips), FMV transitions, `27FINALE` ring arena and ending movie, `/28LOSE`, `nvmshadow.bin` progress save/resume, and `mrbones.ini` options — verified via `mrbones --check --autoplay` (`custom=63` HD sprites). Default video output uses `xbr3x` (`3x`, `960x672`) with auto-loaded 3x HD sprite overrides. TrueMotion and DK3 decoding are linked statically. See `LOG-DESENVOLVIMENTO.md` and `docs/coverage.md` for verification details and remaining SH-2 differences.

The commands below assume the project root is the current directory.

## Quick start

Double-click `Jogar_MrBones.bat` in the project root to launch `build-static\mrbones.exe` directly with bundled paths (`disco1\...`, `disco2\...`, `manifest\versions.txt`), default `xbr3x` 3x (`960x672`) upscaling, and auto-detected HD sprites, or run from the CLI:

```
build-static\mrbones.exe --version
build-static\mrbones.exe "disco1\Mr. Bones (USA) (Disc 1).cue" "manifest\versions.txt" --disc2 "disco2\Mr. Bones (USA) (Disc 2).cue"
build\mrbones.exe "...disc1.cue" "manifest\versions.txt" --disc2 "...disc2.cue" --full-campaign --save-dir build\full_campaign_save
build-static\mrbones.exe "disco1\Mr. Bones (USA) (Disc 1).cue" "manifest\versions.txt" --disc2 "disco2\Mr. Bones (USA) (Disc 2).cue" --check --autoplay --mute
build\mbrunway.exe "disco1\....cue" "manifest\versions.txt" --check
```

- **HD Sprite Pack (`assets/sprites_bones/hd_3x/`):** 63 custom 3x RGBA sprites loaded automatically (`28` Mr. Bones poses, `6` thrown bones, `8` `gang_00..07`, `8` `lord_00..07`, `13` `valley_00..12`), depth-sorted with night-grade shading in HD mode.
- **Hotkeys (windowed):** `F5` cycle upscale filter (`nearest`/`scale2x`/`scale3x`/`xbr2x`/`xbr3x`), `F6` toggle HD sprite overlay (`ON`/`OFF`), `F11` fullscreen, `Esc` quit.
- **Useful CLI flags:** `--upscale <mode>`, `--scale <1..4>`, `--hd-dir <dir>`, `--no-hd-bones`, `--autoplay`, `--mute`, `--capture-dir <dir>`.

Controls (gameplay): arrows/WASD move, X=jump, Down/S=tall, C=Blues, Z=Pulse, Enter=pause, Esc=abort (or any connected Xbox/XInput or USB/HID Saturn gamepad automatically).
MENU: arrows/WASD + Enter/Z (or gamepad D-Pad/stick + A/Start) to select Start Game / Options / Level Select (resumes last unlocked stage from `nvmshadow.bin`).

## Build (Windows x64, MSVC + Ninja)

The static FFmpeg 7.1.1 installation and its matching configured source tree are required. Defaults are `build-static/ffmpeg-min` and `build-static/ffmpeg-7.1.1`; set `MB_FFMPEG_ROOT` and `MB_FFMPEG_SOURCE` to override them. MSVC builds use the static runtime (`/MT` in Release, `/MTd` in Debug). `src/third_party/truemotion1_saturn.c` is a modified FFmpeg 7.1.1 decoder under LGPL 2.1 or later. It retains the second Saturn luminance while an unmodified decoder supplies the original unused byte. Distribution of statically linked FFmpeg requires satisfying the applicable LGPL terms.

```
call "<VS BuildTools>\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B build-static -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-static --config Release
ctest --test-dir build-static --output-on-failure
```

Some tests require the original discs at the paths configured in `CMakeLists.txt`.
For Portuguese MSVC output with Ninja, the build uses `tools/msvc-utf8.cmd` so
Ninja can record header dependencies correctly across incremental builds.

To rebuild FFmpeg 7.1.1, place the [official source archive](https://ffmpeg.org/releases/ffmpeg-7.1.1.tar.xz) at `build-static/ffmpeg-7.1.1.tar.xz` and verify its SHA-256 is `733984395e0dbbe5c046abda2dc49a5544e7e0e1e2366bba849222ae9e3a03b1`. Start MSYS2 Bash from the x64 Visual Studio developer command prompt, then run the following from the project root.

```sh
export PATH="$PATH:/usr/bin"
mkdir -p build-ffmpeg-clean
tar -xf build-static/ffmpeg-7.1.1.tar.xz -C build-ffmpeg-clean
cd build-ffmpeg-clean/ffmpeg-7.1.1
./configure --toolchain=msvc --arch=x86_64 --target-os=win64 \
  --disable-everything --disable-programs --disable-doc --disable-autodetect \
  --disable-network --disable-x86asm --disable-shared --enable-static \
  --enable-avcodec --enable-avformat --enable-avutil --enable-swscale --enable-swresample \
  --enable-demuxer=avi --enable-decoder=truemotion1,adpcm_ima_dk3 --extra-cflags=-MT \
  --prefix="$(pwd)/../install"
make -j8
make install
cd ../..
```

Configure the port from the developer prompt using the rebuilt installation and its matching configured source:

```bat
cmake -S . -B build-static -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DMB_FFMPEG_ROOT:PATH=build-ffmpeg-clean/install ^
  -DMB_FFMPEG_SOURCE:PATH=build-ffmpeg-clean/ffmpeg-7.1.1
cmake --build build-static --config Release
```

## Campaign (24 stages)

| Disc | Stages |
|---|---|
| 1 | RUNWAY, MAUSO, VALLEY, GUITAR, BAT1–3, LOGS, TROLL1–2*, DRUM, GLASS, BIGBNS, VORTEX |
| 2 | RIDE, VILLAGE, ICE1–2, SHADOW, MOAT, STAING, GENRM, LAFF, FINALE |

\*TROLL1/2 have no real `03CODE` (EMPTY skip). Transitions: `/28LOSE` (AVI), per-stage `19DUCK`/`20AVI` (DUKMOVIE stand-in). Specials classified from `mr-bones-maps`: rhythm (GUITAR/DRUM), genroom, bigbones, finale.

Progress unlocks are written to `<save_dir>/nvmshadow.bin` (0x28 bytes, loader `_NvmShadow`).

## Config

| Setting | ini | Environment | CLI |
|---|---|---|---|
| Disc 1 | `disc1` | `MB_DISC1` | argv / `--disc1` (mbverify) |
| Disc 2 | `disc2` | `MB_DISC2` | `--disc2` |
| Saves | `save_dir` | `MB_SAVE_DIR` | `--save-dir` |
| Manifest | `manifest` | `MB_MANIFEST` | argv |

Default config: `%LOCALAPPDATA%\MrBonesPort\mrbones.ini`.
`mrbones.exe` without arguments uses these configured paths. When disc paths are
not configured, it looks for the supplied `disco1` and optional `disco2` folders
beside the executable or one directory above it. The default manifest is found
the same way. Command-line disc and manifest paths override the configuration.

```
build\mbverify --disc1 "D1.cue" --disc2 "D2.cue" --quick
```

## Docs

- `LOG-DESENVOLVIMENTO.md` — north-star log (§12–§25 host 1.0.0)
- `docs/coverage.md` — what is validated vs approximate (SH-2 gaps explicit)
- `docs/runway-brains.md` — RUNWAY brain/VA map
- `manifest/versions.txt` — SHA-256 of maintainer dumps (not a redump DB)
