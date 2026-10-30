#include "kettlelogic/app/brew_day.hpp"

#include "kettlelogic/app/text.hpp"

#include <cmath>
#include <cstdio>
#include <cstdint>

namespace kettlelogic::app {

std::string BrewDayPlan::progressKey() const {
    // FNV-1a over the recipe name, the boil length, and the step texts, each separated by U+001F.
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    const auto feed = [&](const std::string& text) {
        for (const unsigned char byte : text) {
            hash = (hash ^ byte) * 0x100000001b3ULL;
        }
    };
    feed(recipeName);
    feed("\x1F");
    feed(std::to_string(boilMinutes));
    for (const calc::ProcedureStep& step : steps) {
        feed("\x1F");
        feed(step.text);
    }
    char buffer[40];
    std::snprintf(buffer, sizeof buffer, "%llx", static_cast<unsigned long long>(hash));
    return std::string("brewDayProgress.") + buffer;
}

std::set<int> BrewDayPlan::loadProgress(const Preferences& preferences) const {
    std::set<int> done;
    const std::string text = preferences.string(progressKey());
    std::string current;
    for (const char c : text + ",") {
        if (c == ',') {
            if (!current.empty() && current.find_first_not_of("0123456789") == std::string::npos && current.size() < 9) {
                const int index = std::stoi(current);
                if (index < static_cast<int>(steps.size())) {
                    done.insert(index);
                }
            }
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    return done;
}

void BrewDayPlan::saveProgress(Preferences& preferences, const std::set<int>& done) const {
    std::string text;
    for (const int index : done) {
        text += (text.empty() ? "" : ",") + std::to_string(index);
    }
    preferences.setString(progressKey(), text);
}

std::string clockText(double seconds) {
    const int total = static_cast<int>(std::ceil(std::max(seconds, 0.0)));
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%d:%02d", total / 60, total % 60);
    return buffer;
}

}  // namespace kettlelogic::app
