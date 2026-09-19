# Building

## Inside the devcontainer (Linux)

```bash
make                          # -> bin/FortyFifthMidi.clap, bin/FortyFifthMidi.vst3
bash dev/build.sh --install   # also copies to ~/.clap and ~/.vst3
```

These are **Linux** binaries. They load in Reaper, Bitwig or Carla on Linux.
They will not load in a Windows DAW.

## For a Windows DAW

A Linux container cannot produce a Windows plugin without a cross-toolchain.
Two options:

### Option A — cross-compile with MinGW (from the container)

Install the cross-compiler once:

```bash
sudo apt-get update
sudo apt-get install -y mingw-w64
```

Then build against it:

```bash
make CC=x86_64-w64-mingw32-gcc \
     CXX=x86_64-w64-mingw32-g++ \
     CROSS_COMPILING=true
```

Copy the result to your VST folder from WSL (the container cannot see `J:`):

```bash
cp -r bin/FortyFifthMidi.vst3 "/mnt/j/My Drive/Life/5 Media/Music/VSTs/"
```

DPF's Windows VST3 output is a bundle directory containing
`Contents/x86_64-win/FortyFifthMidi.vst3` (a DLL). Copy the whole `.vst3`
directory, not just the DLL.

### Option B — build natively on Windows

Needs MSVC (Visual Studio Build Tools) or MSYS2/MinGW on the Windows side.
The Strawberry Perl MinGW already on this machine can work, but its GCC 13
is not a full Windows SDK toolchain and DPF's Windows path is better tested
against MSYS2.

## Verifying a build

```bash
# what did we actually produce?
file bin/FortyFifthMidi.vst3/Contents/*/*

# ELF 64-bit ... => Linux
# PE32+ ...      => Windows
```

A quick load test on Linux, if `clap-info` or Carla is available:

```bash
clap-info bin/FortyFifthMidi.clap | head -40
```

Confirm in the output that the plugin reports **note ports out** and
**no audio ports** — that is the bus configuration the spec requires.
