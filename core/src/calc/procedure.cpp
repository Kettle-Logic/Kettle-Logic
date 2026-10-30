#include "kettlelogic/calc/procedure.hpp"

#include "kettlelogic/calc/recipe.hpp"
#include "kettlelogic/quantity.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace kettlelogic::calc {

namespace {

using model::Recipe;

// A number with no trailing zeros, as the brewer would write it.
std::string number(double value, int places) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.*f", places, value);
    std::string text = buffer;
    if (text.find('.') != std::string::npos) {
        while (text.back() == '0') {
            text.pop_back();
        }
        if (text.back() == '.') {
            text.pop_back();
        }
    }
    return text;
}

// A quantity as the brewer would read it, such as "30 g" or "1.5 kg".
std::string amountText(const Quantity& q) {
    return number(q.valueIn(q.unit()).toDouble(), 3) + " " + std::string(beerJsonName(q.unit()));
}

// The grain for the mash with its weights, such as "5 kg Pilsner, 0.5 kg Crystal 60".
// Empty when the recipe has no grain to mash.
std::string grainBillText(const Recipe& recipe) {
    std::string bill;
    for (const model::Fermentable& f : recipe.fermentables) {
        if (f.type != "grain" || (f.timing && f.timing->use && *f.timing->use == "add_to_boil")) {
            continue;
        }
        if (!bill.empty()) {
            bill += ", ";
        }
        bill += amountText(f.amount) + " " + f.name;
    }
    return bill;
}

// The whole minutes in a time quantity, or nothing if it is not a time.
std::optional<long long> minutesOf(const Quantity& q) {
    if (q.dimension() != Dimension::Time) {
        return std::nullopt;
    }
    return std::llround(q.valueIn(Unit::Minute).toDouble());
}

class Builder {
public:
    explicit Builder(std::vector<ProcedureStep>& steps) : steps_(steps) {}

    void add(std::string phase, std::string text, bool warning = false) {
        steps_.push_back(ProcedureStep{std::move(phase), std::nullopt, std::move(text), warning, std::nullopt});
    }

    void boil(long long minute, std::string text, bool warning = false) {
        steps_.push_back(ProcedureStep{"Boil", minute, std::move(text), warning, std::nullopt});
    }

    // A step that runs for `minutes`, so the brew-day view can offer a timer for it.
    void timed(std::string phase, std::string text, long long minutes) {
        steps_.push_back(ProcedureStep{std::move(phase), std::nullopt, std::move(text), false, minutes});
    }

    // A boil step that runs for `minutes`, such as the whole boil.
    void boilTimed(long long minute, std::string text, long long minutes) {
        steps_.push_back(ProcedureStep{"Boil", minute, std::move(text), false, minutes});
    }

private:
    std::vector<ProcedureStep>& steps_;
};

// The strike temperature of the first mash step that has one.
std::optional<double> strikeTemperature(const Results& results) {
    if (results.strikeWater.empty()) {
        return std::nullopt;
    }
    return results.strikeWater.front().celsius;
}

std::string hopLine(const model::Hop& hop) {
    std::string line = amountText(hop.amount) + " " + hop.name;
    if (hop.form) {
        line += " (" + *hop.form + ")";
    }
    return line;
}

}  // namespace

MashSystem mashSystemFromName(std::string_view name) {
    if (name == "separate tun") {
        return MashSystem::SeparateTun;
    }
    if (name == "all in one") {
        return MashSystem::AllInOne;
    }
    throw std::invalid_argument("\"" + std::string(name) + "\" is not a mash system");
}

std::vector<ProcedureStep> brewDayProcedure(const Recipe& recipe, long long boilMinutes, MashSystem system) {
    if (boilMinutes < 1 || boilMinutes > 600) {
        throw std::invalid_argument("the boil must be from 1 to 600 minutes");
    }

    std::vector<ProcedureStep> steps;
    Builder out(steps);
    const Results results = calculate(recipe);
    const bool separate = system == MashSystem::SeparateTun;

    out.add("Prepare", "Clean and sanitize the fermenter, the transfer hose, and the chilling equipment.");

    // Strike and mash.
    const std::optional<double> strike = strikeTemperature(results);
    const std::string strikeText = strike ? number(*strike, 1) + " °C" : "your mash temperature";
    const std::string bill = grainBillText(recipe);
    const std::string grain = bill.empty() ? "the grain" : bill;
    if (separate) {
        out.add("Mash", "Heat the strike water in the kettle to " + strikeText + ".");
        out.add("Mash", "Add " + grain + " to the mash tun and stir in the strike water, breaking up any dough balls.");
    } else {
        out.add("Mash", "Fill the kettle with strike water and heat it to " + strikeText + ".");
        out.add("Mash", "Add " + grain + " to the kettle, stirring well to break up any dough balls.");
    }

    std::string mashTime = "Hold the mash until starch conversion is complete (usually about 60 minutes).";
    long long mashMinutes = 60;
    if (recipe.mash && !recipe.mash->steps.empty()) {
        const auto& first = recipe.mash->steps.front();
        if (const auto minutes = minutesOf(first.stepTime)) {
            mashTime = "Hold the mash for " + std::to_string(*minutes) + " minutes.";
            mashMinutes = *minutes;
        }
    }
    out.timed("Mash", mashTime, mashMinutes);

    if (recipe.hops) {
        for (const model::Hop& hop : *recipe.hops) {
            if (hop.timing.use && *hop.timing.use == "add_to_mash") {
                out.add("Mash", "Add " + hopLine(hop) + " to the mash.");
            }
        }
    }

    // Lauter and collect the pre-boil wort.
    if (separate) {
        out.add("Sparge", "Drain the first runnings from the mash tun, then sparge into the kettle.");
    } else {
        out.add("Sparge", "Recirculate the mash until the runnings are clear, then drain them into the kettle.");
    }
    if (results.preBoilLiters) {
        out.add("Sparge", "Collect " + number(*results.preBoilLiters, 2) + " L of pre-boil wort.");
    } else {
        out.add("Sparge", "Collect enough wort for your pre-boil volume.", true);
        out.add("Sparge", "The pre-boil volume could not be calculated, so no target is given: check the recipe's batch size and boil-off.");
    }

    // The boil, with each addition placed by the time the brewer has given it.
    out.boilTimed(0, "Bring the wort to a rolling boil, then start the " + std::to_string(boilMinutes) + "-minute boil.",
                  boilMinutes);

    struct Timed {
        long long elapsed;  // minutes into the boil
        std::string text;
        bool warning;
    };
    std::vector<Timed> timed;
    const auto place = [&](const std::optional<Quantity>& time, const std::string& text) {
        const auto before = time ? minutesOf(*time) : std::nullopt;
        if (!before) {
            timed.push_back(Timed{-1, text + " No time is set for this addition, so it is not placed in the boil.", true});
            return;
        }
        if (*before > boilMinutes) {
            timed.push_back(Timed{-1, text + " It is timed " + std::to_string(*before) + " minutes before the end of a " +
                                      std::to_string(boilMinutes) + "-minute boil, so it cannot be placed. Check the boil length or the time.",
                                  true});
            return;
        }
        timed.push_back(Timed{boilMinutes - *before, text, false});
    };

    if (recipe.hops) {
        for (const model::Hop& hop : *recipe.hops) {
            if (hop.timing.use && *hop.timing.use == "add_to_boil") {
                place(hop.timing.time, "Add " + hopLine(hop) + ".");
            }
        }
    }
    for (const model::Fermentable& f : recipe.fermentables) {
        if (f.timing && f.timing->use && *f.timing->use == "add_to_boil") {
            place(f.timing->time, "Add " + amountText(f.amount) + " " + f.name + ".");
        }
    }
    if (recipe.miscellaneous) {
        for (const model::Miscellaneous& m : *recipe.miscellaneous) {
            if (m.timing && m.timing->use && *m.timing->use == "add_to_boil") {
                place(m.timing->time, "Add " + amountText(m.amount) + " " + m.name + ".");
            }
        }
    }

    std::stable_sort(timed.begin(), timed.end(), [](const Timed& a, const Timed& b) {
        return a.elapsed < b.elapsed;
    });
    for (const Timed& t : timed) {
        if (t.elapsed < 0) {
            out.add("Boil", t.text, true);
        } else {
            out.boil(t.elapsed, t.text, t.warning);
        }
    }

    out.boil(boilMinutes, "Turn off the heat." +
                              (results.postBoilLiters ? " The post-boil volume should be " + number(*results.postBoilLiters, 2) + " L." : ""));

    // Chill, transfer, pitch.
    out.add("Chill", "Chill the wort to your yeast's pitching temperature as quickly as you can.");
    out.add("Transfer", "Transfer the wort to the fermenter, leaving the trub and hop debris behind, then aerate it.");
    if (recipe.cultures && !recipe.cultures->empty()) {
        for (const model::Culture& culture : *recipe.cultures) {
            out.add("Pitch", "Pitch " + amountText(culture.amount) + " of " + culture.name + " (" + culture.type + ", "
                                 + culture.form + ").");
        }
    } else {
        out.add("Pitch", "No culture is set for this recipe, so there is nothing to pitch.", true);
    }

    // Fermentation and packaging.
    if (results.originalGravity) {
        out.add("Ferment", "Take an original gravity reading. The expected value is " +
                               number(*results.originalGravity, 3) + ".");
    }
    if (results.finalGravity) {
        out.add("Ferment", "Check the gravity when fermentation finishes. The expected final gravity is " +
                               number(*results.finalGravity, 3) + ".");
    }
    if (recipe.hops) {
        for (const model::Hop& hop : *recipe.hops) {
            if (hop.timing.use && *hop.timing.use == "add_to_fermentation") {
                const std::string when = hop.timing.time ? ", timed at " + amountText(*hop.timing.time) + " into fermentation" : "";
                out.add("Ferment", "Add " + hopLine(hop) + when + ".", !hop.timing.time);
            }
        }
    }
    if (recipe.hops) {
        for (const model::Hop& hop : *recipe.hops) {
            if (hop.timing.use && *hop.timing.use == "add_to_package") {
                out.add("Package", "Add " + hopLine(hop) + " at packaging.");
            }
        }
    }
    if (results.abv) {
        out.add("Package", "The expected alcohol is " + number(*results.abv, 1) + "% by volume.");
    }
    return steps;
}

}  // namespace kettlelogic::calc
