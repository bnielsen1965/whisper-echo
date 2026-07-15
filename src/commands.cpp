#include "commands.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>

#include "json.hpp"

using json = nlohmann::json;

std::atomic<bool> g_print_paused{false};
std::atomic<bool> g_uinput_paused{false};

/* ------------------------------------------------------------------ */
/*  CommandRegistry                                                    */
/* ------------------------------------------------------------------ */

CommandRegistry::CommandRegistry() {
    commands_ = {
        { CommandAction::PAUSE_PRINT, "Pause Transcription",
          { "echo pause", "echo paws" } },

        { CommandAction::RESUME_PRINT, "Resume Transcription",
          { "echo resume" } },

        { CommandAction::STOP_UINPUT, "Stop uinput",
          { "echo stop input" } },

        { CommandAction::RESUME_UINPUT, "Resume uinput",
          { "echo start input" } },

        { CommandAction::NEW_LINE, "New Line",
          { "echo new line" } },

        { CommandAction::BACKSPACE, "Backspace N",
          { "echo backspace #", "echo backspace" } },

        { CommandAction::SPACE, "Spaces N",
          { "echo spaces #", "echo spaces" } },

        { CommandAction::ARROW_UP, "Arrow Up",
          { "echo arrow up #", "echo up arrow #", "echo arrow up", "echo up arrow" } },

        { CommandAction::ARROW_DOWN, "Arrow Down",
          { "echo arrow down #", "echo down arrow #", "echo arrow down", "echo down arrow" } },

        { CommandAction::ARROW_LEFT, "Arrow Left",
          { "echo arrow left #", "echo left arrow #", "echo arrow left", "echo left arrow" } },

        { CommandAction::ARROW_RIGHT, "Arrow Right",
          { "echo arrow right #", "echo right arrow #", "echo arrow right", "echo right arrow" } },

        { CommandAction::HOME, "Home",
          { "echo home" } },

        { CommandAction::END, "End",
          { "echo end" } },
    };
}

CommandRegistry& CommandRegistry::instance() {
    static CommandRegistry registry;
    return registry;
}

std::string CommandRegistry::normalize(const std::string & s) {
    std::string result;
    result.reserve(s.size());

    bool last_space = true;  // leading space flag
    for (unsigned char c : s) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!last_space) {
                result += ' ';
                last_space = true;
            }
        } else if (c == '.' || c == ',' || c == '!' || c == '?' ||
                   c == ';' || c == ':' || c == '\'' || c == '"' ||
                   c == '(' || c == ')' || c == '-' || c == '_') {
            // strip common punctuation
        } else {
            result += static_cast<char>(std::tolower(c));
            last_space = false;
        }
    }

    // trim trailing space
    while (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }

    return result;
}

bool CommandRegistry::load_from_file(const std::string & path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        fprintf(stderr, "error: cannot open commands file '%s'\n", path.c_str());
        return false;
    }

    json j;
    try {
        j = json::parse(file);
    } catch (const std::exception & e) {
        fprintf(stderr, "error: failed to parse commands file '%s': %s\n",
                path.c_str(), e.what());
        return false;
    }

    file.close();

    // Parse and validate — expects a flat object: { "action_name": ["trigger1", "trigger2", ...] }
    try {
        if (!j.is_object()) {
            fprintf(stderr, "error: commands file must be a JSON object mapping actions to trigger arrays\n");
            return false;
        }

        // Clear defaults and replace with file contents
        commands_.clear();

        auto action_name = [](const std::string & s) -> std::pair<CommandAction, std::string> {
            if (s == "pause_print") return { CommandAction::PAUSE_PRINT, "Pause Transcription" };
            if (s == "resume_print") return { CommandAction::RESUME_PRINT, "Resume Transcription" };
            if (s == "stop_uinput") return { CommandAction::STOP_UINPUT, "Stop uinput" };
            if (s == "resume_uinput") return { CommandAction::RESUME_UINPUT, "Resume uinput" };
            if (s == "new_line") return { CommandAction::NEW_LINE, "New Line" };
            if (s == "backspace") return { CommandAction::BACKSPACE, "Backspace N" };
            if (s == "space") return { CommandAction::SPACE, "Spaces N" };
            if (s == "arrow_up") return { CommandAction::ARROW_UP, "Arrow Up" };
            if (s == "arrow_down") return { CommandAction::ARROW_DOWN, "Arrow Down" };
            if (s == "arrow_left") return { CommandAction::ARROW_LEFT, "Arrow Left" };
            if (s == "arrow_right") return { CommandAction::ARROW_RIGHT, "Arrow Right" };
            if (s == "home") return { CommandAction::HOME, "Home" };
            if (s == "end") return { CommandAction::END, "End" };
            return { CommandAction::PAUSE_PRINT, "" }; // unknown
        };

        for (auto [action_str, triggers] : j.items()) {
            auto [action, name] = action_name(action_str);
            if (name.empty()) {
                fprintf(stderr, "warning: unknown action '%s', skipping\n", action_str.c_str());
                continue;
            }

            if (!triggers.is_array() || triggers.empty()) {
                fprintf(stderr, "warning: action '%s' has no triggers, skipping\n", action_str.c_str());
                continue;
            }

            Command cmd;
            cmd.action = action;
            cmd.name   = name;

            for (const auto & trigger : triggers) {
                std::string t = trigger.get<std::string>();
                cmd.triggers.push_back(normalize(t));
            }

            commands_.push_back(cmd);
        }

        fprintf(stderr, "loaded %d command(s) from '%s'\n",
                static_cast<int>(commands_.size()), path.c_str());

        // Fill in missing actions from the defaults
        auto defaults = CommandRegistry();
        std::vector<CommandAction> all_actions = {
            CommandAction::PAUSE_PRINT,
            CommandAction::RESUME_PRINT,
            CommandAction::STOP_UINPUT,
            CommandAction::RESUME_UINPUT,
            CommandAction::NEW_LINE,
            CommandAction::BACKSPACE,
            CommandAction::SPACE,
            CommandAction::ARROW_UP,
            CommandAction::ARROW_DOWN,
            CommandAction::ARROW_LEFT,
            CommandAction::ARROW_RIGHT,
            CommandAction::HOME,
            CommandAction::END,
        };
        for (const auto& default_cmd : defaults.commands_) {
            bool found = false;
            for (const auto& loaded_cmd : commands_) {
                if (loaded_cmd.action == default_cmd.action) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                fprintf(stderr, "  adding default triggers for missing action '%s'\n", default_cmd.name.c_str());
                commands_.push_back(default_cmd);
            }
        }

        return true;

    } catch (const std::exception & e) {
        fprintf(stderr, "error: error processing commands file '%s': %s\n",
                path.c_str(), e.what());
        return false;
    }
}

/* ------------------------------------------------------------------ */
/*  Parameterized trigger matching                                     */
/* ------------------------------------------------------------------ */

/// Return an empty MatchResult (no match).
static MatchResult no_match() {
    return {nullptr, std::vector<int>{}};
}

/// Return a MatchResult for a matched command with no parameters.
static MatchResult cmd_match(const Command& cmd) {
    return {&cmd, std::vector<int>{}};
}

/// Return a MatchResult for a matched command with a numeric parameter.
static MatchResult param_match(const Command& cmd, int value) {
    return {&cmd, std::vector<int>{value}};
}

/// Convert a number word to its integer value, or -1 if not recognized.
/// Handles: zero..nine, ten..nineteen, twenty, thirty, forty, fifty.
static int word_to_digit(const std::string& s) {
    if (s == "zero")    return 0;
    if (s == "one")     return 1;
    if (s == "two")     return 2;
    if (s == "three")   return 3;
    if (s == "four")    return 4;
    if (s == "five")    return 5;
    if (s == "six")     return 6;
    if (s == "seven")   return 7;
    if (s == "eight")   return 8;
    if (s == "nine")    return 9;
    if (s == "ten")     return 10;
    if (s == "eleven")  return 11;
    if (s == "twelve")  return 12;
    if (s == "thirteen") return 13;
    if (s == "fourteen") return 14;
    if (s == "fifteen") return 15;
    if (s == "sixteen") return 16;
    if (s == "seventeen") return 17;
    if (s == "eighteen") return 18;
    if (s == "nineteen") return 19;
    if (s == "twenty")  return 20;
    if (s == "thirty")  return 30;
    if (s == "forty")   return 40;
    if (s == "fifty")   return 50;
    return -1;
}

/// Try to match a parameterized trigger (containing '#') against normalized text.
/// Splits the trigger at '#' into prefix and suffix, checks that the input
/// starts with the prefix and ends with the suffix, and validates that the
/// captured substring is a pure integer.
static MatchResult try_match_parameterized(
    const std::string& normalized,
    const Command& cmd,
    const std::string& trigger)
{
    size_t hash_pos = trigger.find('#');
    std::string prefix = trigger.substr(0, hash_pos);
    std::string suffix = trigger.substr(hash_pos + 1);
        
    // Need at least prefix + one digit + suffix
    if (normalized.size() < prefix.size() + suffix.size() + 1) {
        return no_match();
    }

    // Input must start with prefix
    if (normalized.substr(0, prefix.size()) != prefix) {
        return no_match();
    }

    // If suffix is non-empty, input must end with it
    if (!suffix.empty() &&
        normalized.substr(normalized.size() - suffix.size()) != suffix) {
        return no_match();
    }

    // Extract the captured value between prefix and suffix
    std::string captured = normalized.substr(
        prefix.size(),
        normalized.size() - prefix.size() - suffix.size()
    );

    // Validate: all digits
    bool all_digits = true;
    for (char c : captured) {
        if (c < '0' || c > '9') {
            all_digits = false;
            break;
        }
    }

    int value;
    if (all_digits) {
        value = std::stoi(captured);
    } else {
        // Not all digits — try interpreting as a number word (e.g. "three")
        value = word_to_digit(captured);
        if (value < 0) {
            return no_match();
        }
    }

    return param_match(cmd, value);
}

/* ------------------------------------------------------------------ */
/*  Command matching                                                   */
/* ------------------------------------------------------------------ */

MatchResult CommandRegistry::match(const std::string & text) const {
    std::string normalized = normalize(text);

    if (normalized.empty()) {
        return no_match();
    }

    // Pass 1: exact match (highest confidence) — non-parameterized triggers only
    for (const auto & cmd : commands_) {
        for (const auto & trigger : cmd.triggers) {
            if (trigger.find('#') == std::string::npos && normalized == trigger) {
                return cmd_match(cmd);
            }
        }
    }

    // Pass 2a: parameterized trigger match (triggers containing '#')
    for (const auto & cmd : commands_) {
        for (const auto & trigger : cmd.triggers) {
            if (trigger.find('#') != std::string::npos) {
                MatchResult result = try_match_parameterized(normalized, cmd, trigger);
                if (result.command != nullptr) {
                    return result;
                }
            }
        }
    }

    // Pass 2b: substring match with length guard — non-parameterized triggers only
    // Prevents short triggers like "pause" from matching inside long sentences
    for (const auto & cmd : commands_) {
        for (const auto & trigger : cmd.triggers) {
            if (trigger.find('#') != std::string::npos) {
                continue;  // skip parameterized triggers here
            }
            if (normalized.length() <= static_cast<size_t>(trigger.length() + 4) &&
                normalized.find(trigger) != std::string::npos) {
                return cmd_match(cmd);
            }
        }
    }

    return no_match();
}
