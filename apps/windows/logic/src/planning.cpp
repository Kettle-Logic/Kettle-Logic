#include "kettlelogic/app/planning.hpp"

#include "kettlelogic/catalog/fermentation_presets.hpp"
#include "kettlelogic/catalog/options.hpp"
#include "kettlelogic/json.hpp"
#include "kettlelogic/uuid.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace kettlelogic::app {

using json::Member;
using json::Object;
using json::Value;

namespace {

// Seconds from 1 January 1970 to 1 January 2001, the reference date the macOS app counts dates from.
constexpr double kReferenceOffset = 978307200.0;

// MARK: Reading

const Value* field(const Value& object, const char* key) {
    return object.isObject() ? object.find(key) : nullptr;
}

double readDouble(const Value& object, const char* key, double fallback) {
    const Value* value = field(object, key);
    return value != nullptr && value->isNumber() ? value->asRational().toDouble() : fallback;
}

std::optional<double> readOptional(const Value& object, const char* key, std::optional<double> fallback = {}) {
    const Value* value = field(object, key);
    if (value == nullptr) {
        return fallback;
    }
    return value->isNumber() ? std::optional<double>(value->asRational().toDouble()) : std::nullopt;
}

bool readBool(const Value& object, const char* key, bool fallback) {
    const Value* value = field(object, key);
    return value != nullptr && value->isBool() ? value->asBool() : fallback;
}

std::string readString(const Value& object, const char* key, const std::string& fallback) {
    const Value* value = field(object, key);
    return value != nullptr && value->isString() ? value->asString() : fallback;
}

IonsInput readIons(const Value& object, const IonsInput& fallback) {
    if (!object.isObject()) {
        return fallback;
    }
    IonsInput ions = fallback;
    ions.calcium = readDouble(object, "calcium", ions.calcium);
    ions.magnesium = readDouble(object, "magnesium", ions.magnesium);
    ions.sodium = readDouble(object, "sodium", ions.sodium);
    ions.sulfate = readDouble(object, "sulfate", ions.sulfate);
    ions.chloride = readDouble(object, "chloride", ions.chloride);
    ions.bicarbonate = readDouble(object, "bicarbonate", ions.bicarbonate);
    return ions;
}

template <typename T, typename Fn>
std::vector<T> readArray(const Value& object, const char* key, Fn&& fn, std::vector<T> fallback) {
    const Value* value = field(object, key);
    if (value == nullptr || !value->isArray()) {
        return fallback;
    }
    std::vector<T> out;
    for (const Value& item : value->asArray()) {
        out.push_back(fn(item));
    }
    return out;
}

ProfileStep readProfileStep(const Value& v) {
    ProfileStep step;
    step.id = readString(v, "id", step.id);
    step.days = readDouble(v, "days", step.days);
    step.tempC = readDouble(v, "tempC", step.tempC);
    return step;
}

FermentationReading readReading(const Value& v) {
    FermentationReading reading;
    reading.id = readString(v, "id", reading.id);
    reading.date = readDouble(v, "date", reading.date);
    reading.temperatureC = readOptional(v, "temperatureC");
    reading.gravity = readOptional(v, "gravity");
    return reading;
}

BrewSession readSession(const Value& v) {
    BrewSession s;
    s.id = readString(v, "id", s.id);
    s.name = readString(v, "name", s.name);
    s.brewDate = readDouble(v, "brewDate", s.brewDate);
    s.mashPh = readOptional(v, "mashPh");
    s.spargeRunoffPh = readOptional(v, "spargeRunoffPh");
    s.endOfRunningsGravity = readOptional(v, "endOfRunningsGravity");
    s.postMashGravity = readOptional(v, "postMashGravity");
    s.preBoilLiters = readOptional(v, "preBoilLiters");
    s.preBoilGravity = readOptional(v, "preBoilGravity");
    s.batchLiters = readOptional(v, "batchLiters");
    s.originalGravity = readOptional(v, "originalGravity");
    s.finalGravity = readOptional(v, "finalGravity");
    s.bottlingLiters = readOptional(v, "bottlingLiters");
    s.carbonationMethod = readString(v, "carbonationMethod", s.carbonationMethod);
    s.carbonationVolumes = readOptional(v, "carbonationVolumes");
    // The key is left out when the value is empty, so an absent key is an empty value here, as it is in Swift.
    s.servingTempC = readOptional(v, "servingTempC");
    if (const Value* name = field(v, "profileName"); name != nullptr && name->isString()) {
        s.profileName = name->asString();
    }
    s.profileSteps = readArray<ProfileStep>(v, "profileSteps", readProfileStep, {});
    s.readings = readArray<FermentationReading>(v, "readings", readReading, {});
    return s;
}

// MARK: Writing

Value num(double value) {
    if (!std::isfinite(value)) {
        return Value::number("0");
    }
    // The shortest text that reads back as the same double.
    char buffer[40];
    for (int digits = 6; digits <= 17; ++digits) {
        std::snprintf(buffer, sizeof buffer, "%.*g", digits, value);
        if (std::strtod(buffer, nullptr) == value) {
            break;
        }
    }
    return Value::number(buffer);
}

void put(Object& o, const char* key, Value value) {
    o.push_back(Member{key, std::move(value)});
}

void putOptional(Object& o, const char* key, const std::optional<double>& value) {
    if (value) {
        put(o, key, num(*value));
    }
}

Value ionsValue(const IonsInput& ions) {
    Object o;
    put(o, "calcium", num(ions.calcium));
    put(o, "magnesium", num(ions.magnesium));
    put(o, "sodium", num(ions.sodium));
    put(o, "sulfate", num(ions.sulfate));
    put(o, "chloride", num(ions.chloride));
    put(o, "bicarbonate", num(ions.bicarbonate));
    return Value::object(std::move(o));
}

Value profileStepValue(const ProfileStep& s) {
    Object o;
    put(o, "id", Value::string(s.id));
    put(o, "days", num(s.days));
    put(o, "tempC", num(s.tempC));
    return Value::object(std::move(o));
}

Value readingValue(const FermentationReading& r) {
    Object o;
    put(o, "id", Value::string(r.id));
    put(o, "date", num(r.date));
    putOptional(o, "temperatureC", r.temperatureC);
    putOptional(o, "gravity", r.gravity);
    return Value::object(std::move(o));
}

Value sessionValue(const BrewSession& s) {
    Object o;
    put(o, "id", Value::string(s.id));
    put(o, "name", Value::string(s.name));
    put(o, "brewDate", num(s.brewDate));
    putOptional(o, "mashPh", s.mashPh);
    putOptional(o, "spargeRunoffPh", s.spargeRunoffPh);
    putOptional(o, "endOfRunningsGravity", s.endOfRunningsGravity);
    putOptional(o, "postMashGravity", s.postMashGravity);
    putOptional(o, "preBoilLiters", s.preBoilLiters);
    putOptional(o, "preBoilGravity", s.preBoilGravity);
    putOptional(o, "batchLiters", s.batchLiters);
    putOptional(o, "originalGravity", s.originalGravity);
    putOptional(o, "finalGravity", s.finalGravity);
    putOptional(o, "bottlingLiters", s.bottlingLiters);
    put(o, "carbonationMethod", Value::string(s.carbonationMethod));
    putOptional(o, "carbonationVolumes", s.carbonationVolumes);
    putOptional(o, "servingTempC", s.servingTempC);
    if (s.profileName) {
        put(o, "profileName", Value::string(*s.profileName));
    }
    json::Array steps;
    for (const ProfileStep& step : s.profileSteps) {
        steps.push_back(profileStepValue(step));
    }
    put(o, "profileSteps", Value::array(std::move(steps)));
    json::Array readings;
    for (const FermentationReading& reading : s.readings) {
        readings.push_back(readingValue(reading));
    }
    put(o, "readings", Value::array(std::move(readings)));
    return Value::object(std::move(o));
}

}  // namespace

// MARK: Dates and identifiers

std::string newUuidText() {
    std::string text = Uuid::generate().toString();
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return text;
}

double nowReferenceSeconds() {
    const auto since = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration<double>(since).count() - kReferenceOffset;
}

namespace {

std::tm localTime(double referenceSeconds) {
    const std::time_t unix = static_cast<std::time_t>(std::floor(referenceSeconds + kReferenceOffset));
    std::tm out{};
#ifdef _WIN32
    localtime_s(&out, &unix);
#else
    localtime_r(&unix, &out);
#endif
    return out;
}

}  // namespace

CivilDate civilDateOf(double referenceSeconds) {
    const std::tm t = localTime(referenceSeconds);
    return {t.tm_year + 1900, t.tm_mon + 1, t.tm_mday};
}

double referenceSecondsAtMidday(const CivilDate& date) {
    std::tm t{};
    t.tm_year = date.year - 1900;
    t.tm_mon = date.month - 1;
    t.tm_mday = date.day;
    t.tm_hour = 12;
    t.tm_isdst = -1;
    return static_cast<double>(std::mktime(&t)) - kReferenceOffset;
}

int daysBetween(double from, double to) {
    // Whole calendar days between the two local dates, whatever the time of day.
    using namespace std::chrono;
    const CivilDate a = civilDateOf(from);
    const CivilDate b = civilDateOf(to);
    const sys_days da = year_month_day{year{a.year}, month{static_cast<unsigned>(a.month)},
                                       day{static_cast<unsigned>(a.day)}};
    const sys_days db = year_month_day{year{b.year}, month{static_cast<unsigned>(b.month)},
                                       day{static_cast<unsigned>(b.day)}};
    return static_cast<int>((db - da).count());
}

// MARK: Settings

const std::vector<std::string>& MashStepSetting::kinds() { return catalog::mashStepKinds(); }

MashSettings::MashSettings() {
    MashStepSetting step;
    step.name = "Saccharification";
    step.kind = MashStepSetting::kinds()[0];
    step.tempC = 67;
    step.minutes = 60;
    steps.push_back(step);
}

const std::vector<std::string>& BrewSession::carbonationMethods() { return catalog::carbonationMethods(); }

const std::vector<FermentationPreset>& FermentationPreset::all() {
    static const std::vector<FermentationPreset> presets = [] {
        std::vector<FermentationPreset> out;
        for (const catalog::FermentationPreset& preset : catalog::fermentationPresets()) {
            FermentationPreset copy;
            copy.name = preset.name;
            for (const catalog::FermentationStep& step : preset.steps) {
                ProfileStep s;
                s.days = step.days;
                s.tempC = step.tempC;
                copy.steps.push_back(s);
            }
            out.push_back(std::move(copy));
        }
        return out;
    }();
    return presets;
}

// MARK: RecipePlanning

RecipePlanning RecipePlanning::read(const std::string& text) {
    RecipePlanning planning;
    if (text.empty()) {
        return planning;
    }
    try {
        const Value root = json::parse(text);
        if (!root.isObject()) {
            return planning;
        }

        if (const Value* v = root.find("starter"); v != nullptr && v->isObject()) {
            StarterSettings& s = planning.starter;
            s.lager = readBool(*v, "lager", s.lager);
            s.packCells = readString(*v, "packCells", s.packCells);
            s.growth = readString(*v, "growth", s.growth);
            s.starterLiters = readString(*v, "starterLiters", s.starterLiters);
            s.starterGravity = readString(*v, "starterGravity", s.starterGravity);
            s.twoStage = readBool(*v, "twoStage", s.twoStage);
            s.firstStageLiters = readString(*v, "firstStageLiters", s.firstStageLiters);
            s.addToBottling = readBool(*v, "addToBottling", s.addToBottling);
        }

        if (const Value* v = root.find("water"); v != nullptr && v->isObject()) {
            WaterSettings& w = planning.water;
            w.baseName = readString(*v, "baseName", w.baseName);
            if (const Value* base = v->find("base")) w.base = readIons(*base, w.base);
            w.salts = readArray<SaltInput>(
                *v, "salts",
                [](const Value& item) {
                    SaltInput salt;
                    salt.id = readString(item, "id", salt.id);
                    salt.kind = readString(item, "kind", salt.kind);
                    salt.grams = readDouble(item, "grams", salt.grams);
                    salt.sparge = readBool(item, "sparge", salt.sparge);
                    return salt;
                },
                w.salts);
            w.leaveOutSalts = readBool(*v, "leaveOutSalts", w.leaveOutSalts);
            w.holdSpargeSaltsUntilBoil = readBool(*v, "holdSpargeSaltsUntilBoil", w.holdSpargeSaltsUntilBoil);
            w.roPercent = readDouble(*v, "roPercent", w.roPercent);
            w.useTarget = readBool(*v, "useTarget", w.useTarget);
            if (const Value* target = v->find("target")) w.target = readIons(*target, w.target);
        }

        if (const Value* v = root.find("mash"); v != nullptr && v->isObject()) {
            MashSettings& m = planning.mash;
            m.steps = readArray<MashStepSetting>(
                *v, "steps",
                [](const Value& item) {
                    MashStepSetting step;
                    step.id = readString(item, "id", step.id);
                    step.name = readString(item, "name", step.name);
                    step.kind = readString(item, "kind", step.kind);
                    step.tempC = readDouble(item, "tempC", step.tempC);
                    step.minutes = readDouble(item, "minutes", step.minutes);
                    step.addition = readString(item, "addition", step.addition);
                    return step;
                },
                m.steps);
            m.spargeType = readString(*v, "spargeType", m.spargeType);
            m.spargeTempC = readDouble(*v, "spargeTempC", m.spargeTempC);
            m.grainTempC = readDouble(*v, "grainTempC", m.grainTempC);
            m.tunTempC = readDouble(*v, "tunTempC", m.tunTempC);
            m.decoctionTempC = readDouble(*v, "decoctionTempC", m.decoctionTempC);
            m.mashRatioLPerKg = readDouble(*v, "mashRatioLPerKg", m.mashRatioLPerKg);
            m.absorptionLPerKg = readDouble(*v, "absorptionLPerKg", m.absorptionLPerKg);
            m.deadspaceLiters = readDouble(*v, "deadspaceLiters", m.deadspaceLiters);
            m.adjustForDeadspace = readBool(*v, "adjustForDeadspace", m.adjustForDeadspace);
            m.acids = readArray<AcidInput>(
                *v, "acids",
                [](const Value& item) {
                    AcidInput acid;
                    acid.id = readString(item, "id", acid.id);
                    acid.kind = readString(item, "kind", acid.kind);
                    acid.grams = readDouble(item, "grams", acid.grams);
                    return acid;
                },
                m.acids);
            m.acidForTarget = readString(*v, "acidForTarget", m.acidForTarget);
            m.targetMashPh = readDouble(*v, "targetMashPh", m.targetMashPh);
            m.spargeAcid = readString(*v, "spargeAcid", m.spargeAcid);
            m.spargeAcidStrength = readDouble(*v, "spargeAcidStrength", m.spargeAcidStrength);
            m.spargeStartPh = readDouble(*v, "spargeStartPh", m.spargeStartPh);
            m.spargeTargetPh = readDouble(*v, "spargeTargetPh", m.spargeTargetPh);
        }

        planning.sessions = readArray<BrewSession>(root, "sessions", readSession, {});
        planning.notes = readString(root, "notes", "");
    } catch (const std::exception&) {
        return RecipePlanning{};  // text that does not read back gives the defaults
    }
    return planning;
}

std::string RecipePlanning::text() const {
    Object root;

    Object s;
    put(s, "lager", Value::boolean(starter.lager));
    put(s, "packCells", Value::string(starter.packCells));
    put(s, "growth", Value::string(starter.growth));
    put(s, "starterLiters", Value::string(starter.starterLiters));
    put(s, "starterGravity", Value::string(starter.starterGravity));
    put(s, "twoStage", Value::boolean(starter.twoStage));
    put(s, "firstStageLiters", Value::string(starter.firstStageLiters));
    put(s, "addToBottling", Value::boolean(starter.addToBottling));
    put(root, "starter", Value::object(std::move(s)));

    Object w;
    put(w, "baseName", Value::string(water.baseName));
    put(w, "base", ionsValue(water.base));
    json::Array salts;
    for (const SaltInput& salt : water.salts) {
        Object o;
        put(o, "id", Value::string(salt.id));
        put(o, "kind", Value::string(salt.kind));
        put(o, "grams", num(salt.grams));
        put(o, "sparge", Value::boolean(salt.sparge));
        salts.push_back(Value::object(std::move(o)));
    }
    put(w, "salts", Value::array(std::move(salts)));
    put(w, "leaveOutSalts", Value::boolean(water.leaveOutSalts));
    put(w, "holdSpargeSaltsUntilBoil", Value::boolean(water.holdSpargeSaltsUntilBoil));
    put(w, "roPercent", num(water.roPercent));
    put(w, "useTarget", Value::boolean(water.useTarget));
    put(w, "target", ionsValue(water.target));
    put(root, "water", Value::object(std::move(w)));

    Object m;
    json::Array steps;
    for (const MashStepSetting& step : mash.steps) {
        Object o;
        put(o, "id", Value::string(step.id));
        put(o, "name", Value::string(step.name));
        put(o, "kind", Value::string(step.kind));
        put(o, "tempC", num(step.tempC));
        put(o, "minutes", num(step.minutes));
        put(o, "addition", Value::string(step.addition));
        steps.push_back(Value::object(std::move(o)));
    }
    put(m, "steps", Value::array(std::move(steps)));
    put(m, "spargeType", Value::string(mash.spargeType));
    put(m, "spargeTempC", num(mash.spargeTempC));
    put(m, "grainTempC", num(mash.grainTempC));
    put(m, "tunTempC", num(mash.tunTempC));
    put(m, "decoctionTempC", num(mash.decoctionTempC));
    put(m, "mashRatioLPerKg", num(mash.mashRatioLPerKg));
    put(m, "absorptionLPerKg", num(mash.absorptionLPerKg));
    put(m, "deadspaceLiters", num(mash.deadspaceLiters));
    put(m, "adjustForDeadspace", Value::boolean(mash.adjustForDeadspace));
    json::Array acids;
    for (const AcidInput& acid : mash.acids) {
        Object o;
        put(o, "id", Value::string(acid.id));
        put(o, "kind", Value::string(acid.kind));
        put(o, "grams", num(acid.grams));
        acids.push_back(Value::object(std::move(o)));
    }
    put(m, "acids", Value::array(std::move(acids)));
    put(m, "acidForTarget", Value::string(mash.acidForTarget));
    put(m, "targetMashPh", num(mash.targetMashPh));
    put(m, "spargeAcid", Value::string(mash.spargeAcid));
    put(m, "spargeAcidStrength", num(mash.spargeAcidStrength));
    put(m, "spargeStartPh", num(mash.spargeStartPh));
    put(m, "spargeTargetPh", num(mash.spargeTargetPh));
    put(root, "mash", Value::object(std::move(m)));

    json::Array sessionValues;
    for (const BrewSession& session : sessions) {
        sessionValues.push_back(sessionValue(session));
    }
    put(root, "sessions", Value::array(std::move(sessionValues)));
    put(root, "notes", Value::string(notes));

    return json::serialize(Value::object(std::move(root)), -1);
}

// MARK: Engine requests

calc::BrewPlanInputs brewingInputs(const WaterSettings& water, const MashSettings& mash) {
    const auto ions = [](const IonsInput& in) {
        calc::Ions out;
        out.calcium = in.calcium;
        out.magnesium = in.magnesium;
        out.sodium = in.sodium;
        out.sulfate = in.sulfate;
        out.chloride = in.chloride;
        out.bicarbonate = in.bicarbonate;
        return out;
    };

    calc::BrewPlanInputs in;
    in.baseWater = ions(water.base);
    in.mashRatioLPerKg = mash.mashRatioLPerKg;
    in.absorptionLPerKg = mash.absorptionLPerKg;
    in.grainTempC = mash.grainTempC;
    in.tunTempC = mash.tunTempC;
    in.mashTempC = mash.steps.empty() ? 67 : mash.steps.front().tempC;
    in.spargeTempC = mash.spargeTempC;
    in.deadspaceLiters = mash.deadspaceLiters;
    in.adjustForDeadspace = mash.adjustForDeadspace;
    in.leaveOutSalts = water.leaveOutSalts;
    in.holdSpargeSaltsUntilBoil = water.holdSpargeSaltsUntilBoil;
    for (const SaltInput& salt : water.salts) {
        in.salts.push_back(calc::WaterSalt{salt.kind, salt.grams, salt.sparge});
    }
    for (const AcidInput& acid : mash.acids) {
        in.acids.push_back(calc::AcidAddition{acid.kind, acid.grams});
    }
    in.acidForTarget = mash.acidForTarget;
    in.targetMashPh = mash.targetMashPh;
    in.spargeAcid = mash.spargeAcid;
    in.spargeAcidStrengthPercent = mash.spargeAcidStrength;
    in.spargeStartPh = mash.spargeStartPh;
    in.spargeTargetPh = mash.spargeTargetPh;
    in.roPercent = water.roPercent;
    // roWater stays at zero, as it does in the macOS app: the RO water has no ions.
    if (water.useTarget) {
        in.targetWater = ions(water.target);
    }
    return in;
}

calc::JournalInputs journalInputs(const BrewSession& session) {
    calc::JournalInputs in;
    in.carbonationTempC = session.servingTempC;
    in.postMashGravity = session.postMashGravity;
    in.preBoilLiters = session.preBoilLiters;
    in.preBoilGravity = session.preBoilGravity;
    in.batchLiters = session.batchLiters;
    in.originalGravity = session.originalGravity;
    in.finalGravity = session.finalGravity;
    in.bottlingLiters = session.bottlingLiters;
    in.carbonationVolumes = session.carbonationVolumes;
    return in;
}

}  // namespace kettlelogic::app
