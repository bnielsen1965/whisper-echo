// Voice command system — matches transcribed text against known commands
// and triggers actions (e.g. pause/resume printing).
//
#ifndef COMMANDS_H
#define COMMANDS_H

#include <atomic>
#include <string>
#include <vector>

/// Action performed when a command is matched.
enum class CommandAction {
    PAUSE_PRINT,      // Stop printing transcribed segments to stdout
    RESUME_PRINT,     // Resume printing transcribed segments to stdout
    STOP_UINPUT,      // Stop sending transcribed segments to uinput
    RESUME_UINPUT,    // Resume sending transcribed segments to uinput
    NEW_LINE,         // Print a blank line to stdout/output file
    BACKSPACE,        // Type backspace N times (N from captured # parameter)
    SPACE,            // Type space N times (N from captured # parameter)
    ARROW_UP,         // Type arrow up key N times (N from captured # parameter, default 1)
    ARROW_DOWN,       // Type arrow down key N times (N from captured # parameter, default 1)
    ARROW_LEFT,       // Type arrow left key N times (N from captured # parameter, default 1)
    ARROW_RIGHT,      // Type arrow right key N times (N from captured # parameter, default 1)
    HOME,             // Type Home key N times (N from captured # parameter, default 1)
    END,              // Type End key N times (N from captured # parameter, default 1)
};

/// A single voice command with human-readable name and trigger phrases.
struct Command {
    CommandAction action;
    std::string name;                       // e.g. "Pause Transcription"
    std::vector<std::string> triggers;      // Case-insensitive trigger strings
};

/// Result of matching transcribed text against registered commands.
struct MatchResult {
    const Command* command;           // nullptr if no match
    std::vector<int> params;          // captured numeric values from # placeholders
};

/// Global flag: when true, segment text is not printed to stdout
/// (still written to output file if configured).
extern std::atomic<bool> g_print_paused;

/// Global flag: when true, transcribed segments are not sent to uinput
extern std::atomic<bool> g_uinput_paused;

/// Singleton registry of voice commands.
/// Initialized with default pause/resume commands at first use.
/// Can be extended or replaced by loading a command.json file.
class CommandRegistry {
public:
    static CommandRegistry& instance();

    /// Load commands from a JSON file.
    /// Returns true on success; prints diagnostic to stderr and returns false on failure.
    bool load_from_file(const std::string & path);

    /// Match transcribed text against registered commands.
    /// Returns a MatchResult containing the matched Command and any captured numeric parameters.
    MatchResult match(const std::string & text) const;

    /// Access the full command list.
    const std::vector<Command>& commands() const { return commands_; }

private:
    std::vector<Command> commands_;

    CommandRegistry();  // Initializes default commands

    /// Normalize a string: lowercase, trim, collapse whitespace, strip punctuation.
    static std::string normalize(const std::string & s);
};

#endif  // COMMANDS_H
