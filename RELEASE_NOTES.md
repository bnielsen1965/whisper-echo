# Release Notes

## 0.2.0

### Changed

- Removed the default Silero VAD model. When `--vad-model` is not specified,
  no model-based VAD is used; the tool now falls back to energy-based VAD
  (`vad_simple`) instead of loading `~/.models/ggml-silero-v6.2.0.bin` by
  default.

## 0.1.0

### Added

- Initial release.
- Real-time streaming speech-to-text with Whisper and VAD.
- Package built with CMake + CPack.
- Includes udev rule for uinput access.
