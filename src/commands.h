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
    PAUSE_PRINT,   // Stop printing transcribed segments to stdout
    RESUME_PRINT,  // Resume printing transcribed segments to stdout
    NEW_LINE,      // Print a blank line to stdout/output file
};

/// A single voice command with human-readable name and trigger phrases.
struct Command {
    CommandAction action;
    std::string name;                       // e.g. "Pause Transcription"
    std::vector<std::string> triggers;      // Case-insensitive trigger strings
};

/// Global flag: when true, segment text is not printed to stdout
/// (still written to output file if configured).
extern std::atomic<bool> g_print_paused;

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
    /// Returns a pointer to the matched Command, or nullptr if no match.
    const Command* match(const std::string & text) const;

    /// Access the full command list.
    const std::vector<Command>& commands() const { return commands_; }

private:
    std::vector<Command> commands_;

    CommandRegistry();  // Initializes default commands

    /// Normalize a string: lowercase, trim, collapse whitespace, strip punctuation.
    static std::string normalize(const std::string & s);
};

#endif  // COMMANDS_H
