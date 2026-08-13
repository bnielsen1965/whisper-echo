# whisper-echo

Real-time streaming speech-to-text application. Captures live audio from a microphone, detects speech with Voice Activity Detection (VAD), transcribes it using Meta's Whisper model (via [whisper.cpp](https://github.com/ggerganov/whisper.cpp)), and prints results to stdout, optionally writes to a file, and can type text directly into applications via a Linux uinput virtual keyboard.

Supports GPU-accelerated inference, voice commands for controlling transcription output, uinput typing with independent pause controls, and two VAD modes — neural (Silero) and energy-based fallback.

## Table of Contents
- [Quick Start](#quick-start)
- [Prerequisites](#prerequisites)
- [Models](#models)
- [Building](#building)
- [Usage](#usage)
- [Voice Activity Detection](#voice-activity-detection)
- [Voice Commands](#voice-commands)
- [Uinput Virtual Keyboard](#uinput-virtual-keyboard)
- [Status Indicator](#status-indicator)
- [Audio Output](#audio-output)
- [Architecture](#architecture)
- [Project Structure](#project-structure)
- [Troubleshooting](#troubleshooting)
- [Dependencies](#dependencies)
- [Packaging and Development](#packaging-and-development)

## AI Transparency

This application was developed using AI.

Claude code and opencode were used with a local AI setup using an AMD Radeon AI Pro R9700 32GB GPU card.
The GPU card is mounted in a DEG1 dock with an oculink connection.
A custom build of llama.cpp was used to host the LLM for claude code use.
A quantized version of Qwen3.6 27B dense model with MTP was used for the initial development.
In follow up development a quantized Muse Glitter 30B model was used.
Once whisper-echo reached a functional point it was used to generate claude code prompts from speech.


## Quick Start

```bash
# Clone with submodules
git clone --recursive https://github.com/bnielsen1965/whisper-echo.git
cd whisper-echo

# If you already cloned without --recursive:
git submodule update --init --recursive

# Build
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

**Models are required.** whisper-echo will not run without a Whisper model file. Download at least one model to `~/.models/` before running:

```bash
mkdir -p ~/.models
# Example: download ggml-base.en.bin to ~/.models/
# See Models section for download links
```

```bash
# Run with default model
./whisper-echo
```

# Run with a specific model
./whisper-echo -m ~/.models/ggml-medium.en.bin

# Use Silero VAD (neural speech detection)
./whisper-echo -vm ~/.models/ggml-silero-v6.2.0.bin

# Write transcription to a file
./whisper-echo -f output.txt

# Type transcription directly into applications (Linux uinput)
./whisper-echo --uinput
```

## Prerequisites

- **CMake** 3.16+
- **C++ compiler** with C++17 support (GCC 9+, Clang 10+)
- **SDL2** development package (`libsdl2-dev` on Debian/Ubuntu, `sdl2` on Fedora)
- **GPU runtime** (one of the following, depending on backend choice):
  - **Vulkan SDK** — AMD/Intel/NVIDIA GPUs (default)
  - **CUDA toolkit** — NVIDIA GPUs
  - **Metal** — macOS Apple Silicon (built-in)
  - **ROCm/HIP SDK** — AMD GPUs

### Uinput Requirements

The uinput feature (`--uinput`) is Linux-only and requires:

- Kernel `uinput` support (`CONFIG_UINPUT=y` or `uinput` module loaded)
- Write access to `/dev/uinput` — use the included setup script:
  ```bash
  sudo ./setup_uinput.sh $USER
  ```
  This adds a udev rule that grants your user permanent access to `/dev/uinput`.

## Models

**Models are required for operation.** whisper-echo will exit with an error if the Whisper model specified by `-m/--model` cannot be loaded. Place model files in `~/.models/`. The project ships with the following (git-ignored `.bin` files):

| Model | File | Size | Description |
|-------|------|------|-------------|
| Whisper Base (English) | `ggml-base.en.bin` | ~148 MB | Fast, good for clear speech |
| Whisper Medium (English) | `ggml-medium.en.bin` | ~1.5 GB | More accurate, handles noise better |
| Silero VAD | `ggml-silero-v6.2.0.bin` | ~885 KB | Neural voice activity detection – optional but strongly recommended |

Download Whisper models from [ggerganov/whisper.cpp#models](https://github.com/ggerganov/whisper.cpp#models) or [Hugging Face](https://huggingface.co/ggerganov/whisper.cpp).

Example download for the default model:
```bash
mkdir -p ~/.models
wget -O ~/.models/ggml-base.en.bin https://huggingface.co/ggerganov/whisper.cpp/resolve/main/models/ggml-base.en.bin
# Optional Silero VAD
wget -O ~/.models/ggml-silero-v6.2.0.bin https://huggingface.co/ggerganov/whisper.cpp/resolve/main/models/ggml-silero-v6.2.0.bin
```

> **Note:** At minimum one Whisper model must be present at the path given to `-m/--model` (default `~/.models/ggml-base.en.bin`). Without a valid model, whisper-echo will fail to start.

## Building

```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

### GPU Backend

Exactly one GPU backend should be enabled. The default is Vulkan. Override at configure time:

```bash
# Vulkan (default — AMD/Intel/NVIDIA)
cmake -DGGML_VULKAN=ON -DGGML_CUDA=OFF -DGGML_METAL=OFF -DGGML_HIP=OFF ..

# NVIDIA CUDA
cmake -DGGML_VULKAN=OFF -DGGML_CUDA=ON -DGGML_METAL=OFF -DGGML_HIP=OFF ..

# Apple Metal (macOS)
cmake -DGGML_VULKAN=OFF -DGGML_CUDA=OFF -DGGML_METAL=ON -DGGML_HIP=OFF ..

# AMD ROCm/HIP
cmake -DGGML_VULKAN=OFF -DGGML_CUDA=OFF -DGGML_METAL=OFF -DGGML_HIP=ON ..
```

## Usage

```
./whisper-echo [options]
```

### Options

| Short | Long | Default | Description |
|-------|------|---------|-------------|
| `-h` | `--help` | — | Show help |
| `-L` | `--list-devices` | false | List audio capture devices as JSON and exit |
| `-t` | `--threads` | min(4, CPU cores) | Number of threads for inference |
| `--length` | — | 60000 | Audio buffer depth in milliseconds |
| `-c` | `--capture` | -1 | Capture device ID (use -1 for default; use --list-devices to enumerate) |
| `-bs` | `--beam-size` | -1 | Beam search size (-1 = greedy decoding) |
| `-ac` | `--audio-ctx` | 0 | Maximum audio context length in tokens (0 = send all) |
| `-vth` | `--vad-thold` | 0.6 | Energy-based VAD threshold |
| `-fth` | `--freq-thold` | 100.0 | High-pass filter cutoff frequency in Hz (energy VAD) |
| `-vg` | `--vad-gain` | 1.0 | Audio gain multiplier applied before VAD |
| `-tr` | `--translate` | false | Translate output to English |
| `-nf` | `--no-fallback` | false | Disable temperature fallback on low confidence |
| `-ps` | `--print-special` | false | Print special tokens in output |
| `-l` | `--language` | en | Spoken language (ISO 639-1 code, e.g. "en", "es", "fr") |
| `-m` | `--model` | ~/.models/ggml-base.en.bin | Path to Whisper model file |
| `-f` | `--file` | — | Save transcription to a text file |
| `-tdrz` | `--tinydiarize` | false | Enable speaker diarization |
| `-sa` | `--save-audio` | false | Save recorded audio segments as WAV files |
| `-ng` | `--no-gpu` | false | Disable GPU inference (CPU only) |
| `-gd` | `--gpu-device` | 0 | GPU device ID |
| `-nfa` | `--no-flash-attn` | false | Disable flash attention |
| `-vm` | `--vad-model` | ~/.models/ggml-silero-v6.2.0.bin | Path to Silero VAD model file |
| `-nsv` | `--no-silero-vad` | false | Disable Silero VAD (use energy-based fallback) |
| `-d` | `--detail` | false | Print transcription timestamps and headers |
| `-ns` | `--no-status` | false | Hide the status indicator |
| `-cm` | `--commands` | — | Path to voice command JSON configuration file |
| `-ui` | `--uinput` | false | Type transcribed text via uinput virtual keyboard (Linux) |

### Examples

```bash
# Basic usage — English, base model, energy-based VAD
./whisper-echo

# List available capture devices as JSON
./whisper-echo --list-devices

# Spanish transcription with medium model
./whisper-echo -l es -m ~/.models/ggml-medium.en.bin

# Neural VAD for better speech detection
./whisper-echo -vm ~/.models/ggml-silero-v6.2.0.bin

# CPU only, detailed output, save to file
./whisper-echo -ng -d -f transcript.txt

# Translate spoken French to English
./whisper-echo -l fr -tr -m ~/.models/ggml-medium.en.bin

# Type directly into applications with uinput
./whisper-echo --uinput -vm ~/.models/ggml-silero-v6.2.0.bin

# Custom capture device, custom commands
./whisper-echo -c 2 -cm command.json

# Save audio segments alongside transcription
./whisper-echo -sa -f transcript.txt
```

## Voice Activity Detection

whisper-echo supports two VAD modes:

1. **Silero VAD (neural)** — A deep-learning-based speech detector that accurately distinguishes speech from noise. Load it with `-vm ~/.models/ggml-silero-v6.2.0.bin`. This is the recommended mode for reliable operation.

2. **Energy-based (fallback)** — A simple high-pass filter and energy comparison. Used automatically if Silero VAD is not available or is disabled with `-nsv`. Tunable via `--vad-thold`, `--freq-thold`, and `--vad-gain`.

## Voice Commands

Voice commands let you control transcription behavior without touching the keyboard. Commands are matched against transcribed text and execute silently (they don't appear in output).

### Built-in Commands

| Command | Action |
|---------|--------|
| "echo pause" / "echo paws" | Pause output to stdout **and** uinput |
| "echo resume" | Resume output to stdout and uinput |
| "echo stop input" | Stop uinput typing only (stdout continues) |
| "echo start input" | Resume uinput typing |
| "echo new line" | Insert a line break in the output |
| "echo backspace" / "echo backspace \<N\>" | Type 1 or N backspaces via uinput |
| "echo space" / "echo space \<N\>" | Type 1 or N spaces via uinput |
| "echo arrow up" / "echo up arrow" / "echo arrow up \<N\>" | Type 1 or N up arrows |
| "echo arrow down" / "echo down arrow" / "echo arrow down \<N\>" | Type 1 or N down arrows |
| "echo arrow left" / "echo left arrow" / "echo arrow left \<N\>" | Type 1 or N left arrows |
| "echo arrow right" / "echo right arrow" / "echo arrow right \<N\>" | Type 1 or N right arrows |
| "echo home" | Type Home key (once) |
| "echo end" | Type End key (once) |

> **Note:** "echo paws" is an alternate trigger for "echo pause" since Whisper often transcribes "pause" as "paws". For parameterized commands, omitting the number defaults to 1.

### Command Variants

The default `command.json` provides three trigger prefixes for most commands:

| Prefix | Example |
|--------|---------|
| **echo** | "echo pause", "echo arrow up", "echo stop input" |
| **omega** | "omega pause", "omega up arrow", "omega stop input" |
| **mega** | "mega pause", "mega arrow up", "mega stop input" |

### Custom Commands

Provide a JSON configuration file with `-cm`:

```json
{
    "pause_print": ["echo pause", "omega pause", "mega pause", "echo paws"],
    "resume_print": ["echo resume", "omega resume", "mega resume"],
    "stop_uinput": ["echo stop input", "echo input stop", "omega stop input", "mega stop input"],
    "resume_uinput": ["echo start input", "echo input start", "omega start input", "mega start input"],
    "new_line": ["echo new line", "echo a new line", "echo end of line", "omega new line", "mega new line"],
    "arrow_up": ["echo arrow up #", "echo up arrow #", "echo arrow up", "echo up arrow", "omega arrow up #", "omega up arrow #", "mega arrow up #", "mega up arrow #"],
    "arrow_down": ["echo arrow down #", "echo down arrow #", "echo arrow down", "echo down arrow", "omega arrow down #", "omega down arrow #", "mega arrow down #", "mega down arrow #"],
    "arrow_left": ["echo arrow left #", "echo left arrow #", "echo arrow left", "echo left arrow", "omega arrow left #", "omega left arrow #", "mega arrow left #", "mega left arrow #"],
    "arrow_right": ["echo arrow right #", "echo right arrow #", "echo arrow right", "echo right arrow", "omega arrow right #", "omega right arrow #", "mega arrow right #", "mega right arrow #"],
    "home": ["echo home", "omega home", "mega home"],
    "end": ["echo end", "omega end", "mega end"],
    "backspace": ["echo backspace #", "echo backspace", "omega backspace #", "mega backspace #"],
    "space": ["echo space #", "echo space", "omega space #", "mega space #"]
}
```

Match is case-insensitive. A segment is treated as a command if it exactly matches a trigger phrase or is very close (at most 4 extra characters allowed to reduce false positives).

Parameterized triggers use `#` as a placeholder for a number (e.g. "echo backspace #" matches "echo backspace three"). Number words (one through twenty, thirty, forty, fifty) and digits are both accepted. When no number is given, the default is 1.

### Command Actions

| Action | Description |
|--------|------------|
| `PAUSE_PRINT` | Stops output to stdout and uinput. Transcription continues; text is still written to the output file (`-f`). |
| `RESUME_PRINT` | Resumes output to stdout and uinput. |
| `STOP_UINPUT` | Stops uinput typing only. Stdout printing continues. |
| `RESUME_UINPUT` | Resumes uinput typing. |
| `NEW_LINE` | Inserts a line break in stdout, output file, and uinput. |
| `BACKSPACE` | Types backspace N times via uinput (default 1). |
| `SPACE` | Types space N times via uinput (default 1). |
| `ARROW_UP` | Types up arrow N times via uinput (default 1). |
| `ARROW_DOWN` | Types down arrow N times via uinput (default 1). |
| `ARROW_LEFT` | Types left arrow N times via uinput (default 1). |
| `ARROW_RIGHT` | Types right arrow N times via uinput (default 1). |
| `HOME` | Types Home key via uinput (once, no parameter). |
| `END` | Types End key via uinput (once, no parameter). |

### Pause State Independence

The two pause flags are independent:

| `g_print_paused` | `g_uinput_paused` | Result |
|------------------|-------------------|--------|
| false | false | Output to both stdout and uinput |
| true | false | Output to neither (echo pause) |
| false | true | Stdout only, uinput stopped |
| true | true | Output to neither |

## Uinput Virtual Keyboard

When enabled with `--uinput` (`-ui`), transcribed text is typed directly into the focused application as keystrokes via a Linux uinput virtual keyboard. This allows whisper-echo to function as a hands-free dictation tool.

Text is typed as a running line — segments are joined with spaces. Use "echo new line" to press Enter and move to a new line.

The uinput output can be paused independently of stdout printing:

- **"echo pause"** stops both stdout and uinput
- **"echo stop input"** stops uinput only (stdout continues)
- **"echo resume"** / **"echo start input"** restore output

Characters are typed with a 5ms delay between keystrokes. Escape sequences `\n` (Enter), `\t` (Tab), and `\b` (Backspace) are interpreted.

## Status Indicator

By default, whisper-echo prints a status indicator that updates in-place:

- **IDLE** — Waiting, buffer not yet active
- **LISTENING** — Monitoring for speech
- **CAPTURING** — Speech detected, collecting audio
- **PROCESSING** — Running Whisper inference

### Status Indicators

Parenthetical suffixes indicate active pause states:

| Indicator | Meaning |
|-----------|---------|
| `[listening]` | Normal operation |
| `[listening (p)]` | Print paused (stdout + uinput stopped) |
| `[listening (Si)]` | Input stopped (uinput stopped, stdout continues) |
| `[listening (pSi)]` | Both paused |

Disable with `--no-status`.

## Audio Output

When `--save-audio` is enabled, each transcribed speech segment is saved as a WAV file alongside the transcription.

## Architecture

```
Microphone → SDL2 → Circular Buffer → VAD → Whisper → Segment Text
                                                    │
                                        ┌───────────┼───────────┐
                                        │           │           │
                                   stdout      output file   uinput
                                  (if not p)    (always)    (not p/Si)
```

1. SDL2 captures audio at 16 kHz mono (32-bit float) into a circular buffer.
2. The VAD (Silero or energy-based) monitors incoming audio for speech.
3. When speech ends, the captured segment is fed to Whisper for transcription.
4. Each transcribed segment is checked against registered voice commands.
5. Non-command text is printed to stdout (if not paused), written to the output file, and typed via uinput (if enabled and not paused).

## Project Structure

```
├── CMakeLists.txt              # Build configuration
├── command.json                # Voice command configuration
├── setup_uinput.sh             # Script to configure /dev/uinput permissions
├── models/                     # Pre-trained model files (.bin) – default runtime path ~/.models/
├── src/
│   ├── stream.cpp              # Main entry point, CLI, transcription loop
│   ├── audio_capture.h         # SDL-based audio capture declarations
│   ├── audio_capture.cpp       # Audio device handling, circular buffer
│   ├── audio_utils.h           # Audio utility declarations
│   ├── audio_utils.cpp         # VAD helpers, WAV writer, Silero state
│   ├── commands.h              # Voice command declarations
│   ├── commands.cpp            # Command registry, JSON loading, matching
│   ├── uinput.h                # Uinput virtual keyboard declarations
│   └── uinput.cpp              # Uinput device setup, keystroke generation
└── vendor/
    └── whisper.cpp/            # whisper.cpp inference engine (submodule)
```

## Troubleshooting

- **No audio detected** — Check the capture device ID listed at startup; specify with `-c`.
- **Silero VAD not loading** — Ensure the model file exists at the path given to `-vm`. The program falls back to energy-based VAD if Silero fails to initialize.
- **Slow inference** — Enable a GPU backend, reduce `-t` to match your CPU, or use the smaller base model.
- **Too many false transcriptions** — Increase `--vad-thold` (energy mode) or switch to Silero VAD with `-vm`.
- **Not enough transcriptions** — Decrease `--vad-thold`, increase `--vad-gain`, or try the medium model.
- **Uinput permission denied** — Run `sudo ./setup_uinput.sh $USER` to set up persistent access to `/dev/uinput`.
- **Uinput types wrong characters** — The keymap is US-QWERTY. Other layouts are not currently supported.

## Dependencies

- [whisper.cpp](https://github.com/ggerganov/whisper.cpp) — C/C++ Whisper inference (vendored submodule)
- [SDL2](https://www.libsdl.org/) — Audio capture
- [nlohmann/json](https://github.com/nlohmann/json) — JSON parsing for commands (via whisper.cpp)

## Packaging and Development

### Building from source

```bash
git clone --recursive https://github.com/bnielsen1965/whisper-echo.git
cd whisper-echo
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

CMake targets:
* `cmake --build build --target dist` — create a source tarball with submodules
* `cmake --build build --target update-changelog` — update debian/changelog from git tag

### RPM packaging

Binary RPM via CPack:
```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
cd build && cpack -G RPM
```

The build produces `whisper-echo` and `whisper-echo-devel` packages in the `build/` directory. Run `rpmlint` to check policy:
```bash
rpmlint build/whisper-echo-*.rpm
```

The spec is at `packaging/rpm/whisper-echo.spec`. For SRPM builds, create a source tarball with submodules:
```bash
./tools/make_dist.sh
rpmbuild -ba packaging/rpm/whisper-echo.spec
```
SRPMs and binary RPMs are placed in `~/rpmbuild/SRPMS/` and `~/rpmbuild/RPMS/x86_64/` respectively. Check policy with:
```bash
rpmlint ~/rpmbuild/RPMS/x86_64/whisper-echo-*.rpm
```

### Debian packaging

```bash
sudo apt install debhelper cmake build-essential libsdl2-dev libvulkan-dev
dpkg-buildpackage -us -uc
lintian ../whisper-echo_*.deb
```

The Debian tree is in `debian/`. Man page is installed as `whisper-echo.1.gz`, docs go to `/usr/share/doc/whisper-echo/`. Udev rules are not installed automatically; see `docs/uinput.md` and `share/whisper-echo/setup_uinput.sh` for manual uinput setup.

### Versioning

Version is derived from git tags via CMake:
* `PROJECT_VERSION` = `git describe --tags --abbrev=0`
* `PROJECT_RELEASE` = commits since tag from `git describe --tags --long`

Both RPM and DEB release numbers are auto-filled from git. Update changelog with `cmake --build build --target update-changelog`.
