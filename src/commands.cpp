#include "commands.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>

#include "json.hpp"

using json = nlohmann::json;

std::atomic<bool> g_print_paused{false};

/* ------------------------------------------------------------------ */
/*  CommandRegistry                                                    */
/* ------------------------------------------------------------------ */

CommandRegistry::CommandRegistry() {
    commands_ = {
        { CommandAction::PAUSE_PRINT, "Pause Transcription",
          { "echo pause" } },

        { CommandAction::RESUME_PRINT, "Resume Transcription",
          { "echo resume" } },

        { CommandAction::NEW_LINE, "New Line",
          { "echo new line" } },
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
            if (s == "new_line") return { CommandAction::NEW_LINE, "New Line" };
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

        return true;

    } catch (const std::exception & e) {
        fprintf(stderr, "error: error processing commands file '%s': %s\n",
                path.c_str(), e.what());
        return false;
    }
}

const Command* CommandRegistry::match(const std::string & text) const {
    std::string normalized = normalize(text);

    if (normalized.empty()) {
        return nullptr;
    }

    // Pass 1: exact match (highest confidence)
    for (const auto & cmd : commands_) {
        for (const auto & trigger : cmd.triggers) {
            if (normalized == trigger) {
                return &cmd;
            }
        }
    }

    // Pass 2: substring match with length guard
    // Prevents short triggers like "pause" from matching inside long sentences
    for (const auto & cmd : commands_) {
        for (const auto & trigger : cmd.triggers) {
            if (normalized.length() <= static_cast<size_t>(trigger.length() + 4) &&
                normalized.find(trigger) != std::string::npos) {
                return &cmd;
            }
        }
    }

    return nullptr;
}
