// uinput virtual keyboard for whisper-echo
//
// Types transcribed text as keystrokes to a Linux uinput device.
// Extracted from the whinput project.
//
#pragma once

#include <string>

namespace uinput {

/// Open and configure a uinput virtual keyboard device.
/// Returns a valid file descriptor on success, or -1 on failure
/// (prints error to stderr).
int setup();

/// Destroy the uinput virtual keyboard device and close the fd.
void teardown(int fd);

/// Type a string of text, interpreting \n as Enter, \t as Tab, \b as Backspace.
/// Characters not in the keymap are skipped with a warning to stderr.
void type_string(int fd, const std::string& text);

/// Type a newline (Enter key press).
void type_newline(int fd);

/// Type the backspace key N times.
void type_backspaces(int fd, int count);

/// Type the space key N times.
void type_spaces(int fd, int count);

} // namespace uinput
