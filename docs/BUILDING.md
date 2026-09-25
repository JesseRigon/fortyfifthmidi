# Building

There is one script per platform. Each builds the CLAP and VST3, verifies the
binaries are for the platform it thinks it targeted, and exports them to
`dist/<platform>/`.

```bash
bash scripts/build-linux.sh
bash scripts/build-windows.sh
bash scripts/build-macos.sh
```

Add `--clean` to wipe artifacts first, `--install` to also copy into your user
plugin folders (Linux and macOS), `--universal` for a fat arm64 + x86_64 bundle
(macOS only).

## Getting the source

DPF is a git submodule, so:

```bash
git clone --recursive https://github.com/JesseRigon/fortyfifthmidi.git
```

Already cloned without it? `git submodule update --init --recursive`. Without DPF
the scripts stop before compiling anything and tell you this.

## Where the output goes

| Directory | What it is | Copy it? |
|---|---|---|
| `build/` | Object files, the compiler's scratch space | No |
| `bin/` | The build system's immediate output | No |
| `dist/<platform>/` | Verified, exported plugins | **Yes** |

Both `build/` and `bin/` are reused between builds and can hold artifacts from a
previous target. `dist/` is wiped and rewritten on every run, so what is in it
always came from the build that just finished.

The VST3 is a **bundle directory**, not a single file — on Windows the DLL lives
at `Contents/x86_64-win/FortyFifthMidi.vst3`. Copy the whole `.vst3` directory.
Copying only the inner file produces a plugin no host can find.

## Where plugins go

**Linux**

```
~/.clap/          ~/.vst3/            (just you)
/usr/lib/clap/    /usr/lib/vst3/      (everyone)
```

**Windows**

```
C:\Program Files\Common Files\CLAP\
C:\Program Files\Common Files\VST3\
```

**macOS**

```
~/Library/Audio/Plug-Ins/CLAP     ~/Library/Audio/Plug-Ins/VST3     (just you)
/Library/Audio/Plug-Ins/CLAP      /Library/Audio/Plug-Ins/VST3      (everyone)
```

## Toolchains

### Linux

`build-essential` and `pkg-config`, plus X11 and OpenGL development headers for
DPF's UI layer:

```bash
# Debian / Ubuntu
sudo apt-get install build-essential pkg-config \
                     libx11-dev libgl1-mesa-dev \
                     libxext-dev libxcursor-dev libxrandr-dev

# Fedora
sudo dnf install gcc-c++ make pkgconf-pkg-config \
                 libX11-devel mesa-libGL-devel \
                 libXext-devel libXcursor-devel libXrandr-devel

# Arch
sudo pacman -S base-devel libx11 mesa libxext libxcursor libxrandr
```

The script checks for these with `pkg-config` before compiling. Missing UI
headers otherwise surface as a link failure deep inside DGL, which does not
mention X11 anywhere in the error.

### Windows

Two supported ways, and the script picks between them automatically.

**Natively, under MSYS2** — install the toolchain and run the script from the
`MSYS2 MINGW64` shell, not the plain `MSYS` one (the latter builds for MSYS's own
POSIX layer, not for Windows):

```bash
pacman -S mingw-w64-x86_64-gcc make pkg-config
bash scripts/build-windows.sh
```

Git Bash also works if MinGW is on `PATH`.

**Cross-compiled from Linux or WSL:**

```bash
sudo apt-get install mingw-w64     # Debian/Ubuntu
bash scripts/build-windows.sh
```

The build links `-static -static-libgcc -static-libstdc++`, so the plugin loads
on a machine with no MinGW runtime installed — which is every user's machine.

A note on switching toolchains: DGL is compiled separately and cached, and Linux
objects will not link against Windows ones. The Windows script therefore cleans
both the plugin and DGL unconditionally, before and after, rather than only under
`--clean`. Getting this wrong costs more than the rebuild does.

MSVC is not supported. DPF's Windows path is better tested against MinGW, and
nothing here needs the MSVC ABI.

### macOS

**This path is untested — nobody has run it on a Mac.** The flags are DPF's
documented macOS ones, but treat a first run as debugging rather than as
building.

Needs the Xcode command line tools:

```bash
xcode-select --install
bash scripts/build-macos.sh
```

There is no cross-compile route. Apple's SDK is not redistributable, so macOS
binaries have to be produced on macOS.

The bundles are **not signed and not notarized**. macOS usually reports this as
the plugin being "damaged and can't be opened", which is misleading — nothing is
damaged, it is only unsigned. For a plugin you built yourself:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/CLAP/FortyFifthMidi.clap
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/FortyFifthMidi.vst3
```

Only do that for plugins you trust.

## Verifying a build

Each script already checks that it produced binaries for the platform it targeted
and fails loudly otherwise — a cross-build that quietly falls back to the host
compiler is the failure this catches, because it produces a plugin that loads
nowhere and gives no other clue.

To check by hand:

```bash
find dist -type f -exec file {} \;

# ELF 64-bit ... => Linux
# PE32+ ...      => Windows
# Mach-O ...     => macOS
```

On macOS, confirm which architectures a universal build actually contains:

```bash
lipo -info dist/macos/FortyFifthMidi.clap
```

A load test on Linux, if `clap-info` is available:

```bash
clap-info dist/linux/FortyFifthMidi.clap | head -40
```

Confirm the plugin reports **note ports out** and **no audio ports** — that is the
bus configuration the spec requires.

## Tests

```bash
bash dev/run-tests.sh
```

Six suites, compiled straight from `dev/*.cpp` against `src/` with no plugin
types involved. They need only `g++`, and they run in about a second. Every one of
them exists because something was actually broken.
