# whisper-echo

Real-time streaming speech-to-text application. Captures live audio from a microphone, detects speech with Voice Activity Detection (VAD), transcribes it using Meta's Whisper model (via [whisper.cpp](https://github.com/ggerganov/whisper.cpp)), and prints results to stdout and optionally a file.

Supports GPU-accelerated inference, voice commands for controlling transcription output, and two VAD modes — neural (Silero) and energy-based fallback.

## Quick Start

```bash
# Build
mkdir build && cd build
cmake ..
cmake --build . --config Release

# Run with default model
./whisper-echo

# Run with a specific model
./whisper-echo -m models/ggml-medium.en.bin

# Use Silero VAD (neural speech detection)
./whisper-echo -vm models/ggml-silero-v6.2.0.bin

# Write transcription to a file
./whisper-echo -f output.txt
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

## Models

Place model files in the `models/` directory. The project ships with the following (git-ignored `.bin` files):

| Model | File | Size | Description |
|-------|------|------|-------------|
| Whisper Base (English) | `ggml-base.en.bin` | ~148 MB | Fast, good for clear speech |
| Whisper Medium (English) | `ggml-medium.en.bin` | ~1.5 GB | More accurate, handles noise better |
| Silero VAD | `ggml-silero-v6.2.0.bin` | ~885 KB | Neural voice activity detection |

Download Whisper models from [ggerganov/whisper.cpp#models](https://github.com/ggerganov/whisper.cpp#models) or [Hugging Face](https://huggingface.co/ggerganov/whisper.cpp).

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
| `-t` | `--threads` | min(4, CPU cores) | Number of threads for inference |
| `--length` | — | 60000 | Audio buffer depth in milliseconds |
| `-c` | `--capture` | -1 | Capture device ID (use -1 for default; list devices on startup) |
| `-bs` | `--beam-size` | -1 | Beam search size (-1 = greedy decoding) |
| `-ac` | `--audio-ctx` | 0 | Maximum audio context length in tokens (0 = send all) |
| `-vth` | `--vad-thold` | 0.6 | Energy-based VAD threshold |
| `-fth` | `--freq-thold` | 100.0 | High-pass filter cutoff frequency in Hz (energy VAD) |
| `-vg` | `--vad-gain` | 1.0 | Audio gain multiplier applied before VAD |
| `-tr` | `--translate` | false | Translate output to English |
| `-nf` | `--no-fallback` | false | Disable temperature fallback on low confidence |
| `-ps` | `--print-special` | false | Print special tokens in output |
| `-l` | `--language` | en | Spoken language (ISO 639-1 code, e.g. "en", "es", "fr") |
| `-m` | `--model` | models/ggml-base.en.bin | Path to Whisper model file |
| `-f` | `--file` | — | Save transcription to a text file |
| `-tdrz` | `--tinydiarize` | false | Enable speaker diarization |
| `-sa` | `--save-audio` | false | Save recorded audio segments as WAV files |
| `-ng` | `--no-gpu` | false | Disable GPU inference (CPU only) |
| `-gd` | `--gpu-device` | 0 | GPU device ID |
| `-nfa` | `--no-flash-attn` | false | Disable flash attention |
| `-vm` | `--vad-model` | models/for-tests-silero-v6.2.0-ggml.bin | Path to Silero VAD model file |
| `-nsv` | `--no-silero-vad` | false | Disable Silero VAD (use energy-based fallback) |
| `-d` | `--detail` | false | Print transcription timestamps and headers |
| `-ns` | `--no-status` | false | Hide the status indicator |
| `-cm` | `--commands` | — | Path to voice command JSON configuration file |

### Examples

```bash
# Basic usage — English, base model, energy-based VAD
./whisper-echo

# Spanish transcription with medium model
./whisper-echo -l es -m models/ggml-medium.en.bin

# Neural VAD for better speech detection
./whisper-echo -vm models/ggml-silero-v6.2.0.bin

# CPU only, detailed output, save to file
./whisper-echo -ng -d -f transcript.txt

# Translate spoken French to English
./whisper-echo -l fr -tr -m models/ggml-medium.en.bin

# Custom capture device, custom commands
./whisper-echo -c 2 -cm command.json

# Save audio segments alongside transcription
./whisper-echo -sa -f transcript.txt
```

## Voice Activity Detection

whisper-echo supports two VAD modes:

1. **Silero VAD (neural)** — A deep-learning-based speech detector that accurately distinguishes speech from noise. Load it with `-vm models/ggml-silero-v6.2.0.bin`. This is the recommended mode for reliable operation.

2. **Energy-based (fallback)** — A simple high-pass filter and energy comparison. Used automatically if Silero VAD is not available or is disabled with `-nsv`. Tunable via `--vad-thold`, `--freq-thold`, and `--vad-gain`.

## Voice Commands

Voice commands let you control transcription behavior without touching the keyboard. Commands are matched against transcribed text and execute silently (they don't appear in output).

### Built-in Commands

| Command | Action |
|---------|--------|
| "echo pause" | Pause transcription output to stdout |
| "echo resume" | Resume transcription output to stdout |
| "echo new line" | Insert a line break in the output |

### Custom Commands

Provide a JSON configuration file with `-cm`:

```json
{
    "pause_print": ["echo pause", "omega pause"],
    "resume_print": ["echo resume", "omega resume"],
    "new_line": ["echo new line", "omega new line"]
}
```

Match is case-insensitive. A segment is treated as a command if it exactly matches a trigger phrase or is very close (at most 4 extra characters allowed to reduce false positives).

### Command Actions

- **`PAUSE_PRINT`** — Stops printing to stdout. Transcription continues; text is still written to the output file (`-f`).
- **`RESUME_PRINT`** — Resumes printing to stdout.
- **`NEW_LINE`** — Inserts a line break in the output stream.

## Status Indicator

By default, whisper-echo prints a status indicator that updates in-place:

- **IDLE** — Waiting, buffer not yet active
- **LISTENING** — Monitoring for speech
- **CAPTURING** — Speech detected, collecting audio
- **PROCESSING** — Running Whisper inference

Disable with `--no-status`.

## Audio Output

When `--save-audio` is enabled, each transcribed speech segment is saved as a WAV file alongside the transcription.

## Architecture

```
Microphone → SDL2 → Circular Buffer → VAD → Whisper → Text Output
                                      │
                                   Commands
                                   (pause/resume/
                                    new line)
```

1. SDL2 captures audio at 16 kHz mono (32-bit float) into a circular buffer.
2. The VAD (Silero or energy-based) monitors incoming audio for speech.
3. When speech ends, the captured segment is fed to Whisper for transcription.
4. Each transcribed segment is checked against registered voice commands.
5. Non-command text is printed to stdout and optionally written to a file.

## Project Structure

```
├── CMakeLists.txt          # Build configuration
├── command.json            # Voice command configuration
├── models/                 # Pre-trained model files (.bin)
├── src/
│   ├── stream.cpp          # Main entry point, CLI, transcription loop
│   ├── audio_capture.h     # SDL-based audio capture declarations
│   ├── audio_capture.cpp   # Audio device handling, circular buffer
│   ├── audio_utils.h       # Audio utility declarations
│   ├── audio_utils.cpp     # VAD helpers, WAV writer, Silero state
│   ├── commands.h          # Voice command declarations
│   └── commands.cpp        # Command registry, JSON loading, matching
└── vendor/
    └── whisper.cpp/        # whisper.cpp inference engine (submodule)
```

## Troubleshooting

- **No audio detected** — Check the capture device ID listed at startup; specify with `-c`.
- **Silero VAD not loading** — Ensure the model file exists at the path given to `-vm`. The program falls back to energy-based VAD if Silero fails to initialize.
- **Slow inference** — Enable a GPU backend, reduce `-t` to match your CPU, or use the smaller base model.
- **Too many false transcriptions** — Increase `--vad-thold` (energy mode) or switch to Silero VAD with `-vm`.
- **Not enough transcriptions** — Decrease `--vad-thold`, increase `--vad-gain`, or try the medium model.

## Dependencies

- [whisper.cpp](https://github.com/ggerganov/whisper.cpp) — C/C++ Whisper inference (vendored submodule)
- [SDL2](https://www.libsdl.org/) — Audio capture
- [nlohmann/json](https://github.com/nlohmann/json) — JSON parsing for commands (via whisper.cpp)
