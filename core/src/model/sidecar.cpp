#include "kettlelogic/model/sidecar.hpp"

#include "kettlelogic/json.hpp"
#include "kettlelogic/model/entities.hpp"
#include "kettlelogic/crypto/sha256.hpp"
#include "kettlelogic/styles/style.hpp"
#include "kettlelogic/uuid.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <string>
#include <vector>

namespace kettlelogic::model {

namespace {

using json::Array;
using json::Member;
using json::Object;
using json::Value;

// Version 7 adds the folders and the BJCP style of each recipe. Version 6 files, which
// have neither, are still read. Version 8 adds the user styles. A document with none is
// still written as version 7, so older readers keep opening it.
constexpr int kSidecarSchema = 8;
constexpr int kSidecarSchemaWithoutStyles = 7;
constexpr int kSidecarSchemaWithoutFolders = 6;

// A range as {"low", "high"}, each in the shortest text that reads back as the same double.
Value rangeValue(const styles::Range& range) {
    const auto text = [](double value) {
        char buffer[64];
        const auto end = std::to_chars(buffer, buffer + sizeof buffer, value).ptr;
        return Value::number(std::string(buffer, end));
    };
    Object o;
    o.push_back(Member{"low", text(range.low)});
    o.push_back(Member{"high", text(range.high)});
    return Value::object(std::move(o));
}

const char* additiveName(Additive additive) {
    switch (additive) {
        case Additive::Sulfite: return "sulfite";
        case Additive::TartaricAcid: return "tartaric acid";
    }
    return "";
}

std::optional<Additive> parseAdditive(std::string_view name) {
    for (const Additive additive : {Additive::Sulfite, Additive::TartaricAcid}) {
        if (name == additiveName(additive)) {
            return additive;
        }
    }
    return std::nullopt;
}

const char* saltName(SaltKind kind) {
    switch (kind) {
        case SaltKind::Gypsum: return "gypsum";
        case SaltKind::CalciumChloride: return "calcium chloride";
        case SaltKind::EpsomSalt: return "epsom salt";
        case SaltKind::BakingSoda: return "baking soda";
    }
    return "";
}

std::optional<SaltKind> parseSalt(std::string_view name) {
    for (const SaltKind kind : {SaltKind::Gypsum, SaltKind::CalciumChloride, SaltKind::EpsomSalt, SaltKind::BakingSoda}) {
        if (name == saltName(kind)) {
            return kind;
        }
    }
    return std::nullopt;
}

// Follows an RFC 6901 pointer from `root`. Returns nullptr if it does not resolve.
const Value* resolve(const Value& root, std::string_view pointer) {
    if (pointer.empty()) {
        return &root;
    }
    if (pointer.front() != '/') {
        return nullptr;
    }
    const Value* node = &root;
    std::size_t start = 1;
    while (true) {
        const std::size_t end = pointer.find('/', start);
        std::string token(pointer.substr(start, end == std::string_view::npos ? end : end - start));
        for (std::size_t i = 0; (i = token.find("~1", i)) != std::string::npos; ++i) {
            token.replace(i, 2, "/");
        }
        for (std::size_t i = 0; (i = token.find("~0", i)) != std::string::npos; ++i) {
            token.replace(i, 2, "~");
        }

        if (node->isObject()) {
            node = node->find(token);
        } else if (node->isArray()) {
            const Array& items = node->asArray();
            if (token.empty() || token.find_first_not_of("0123456789") != std::string::npos ||
                token.size() > 9) {
                return nullptr;
            }
            const std::size_t index = std::stoul(token);
            node = index < items.size() ? &items[index] : nullptr;
        } else {
            return nullptr;
        }
        if (node == nullptr) {
            return nullptr;
        }
        if (end == std::string_view::npos) {
            return node;
        }
        start = end + 1;
    }
}

std::optional<std::string> nameOf(const Value& object) {
    const Value* name = object.find("name");
    if (name != nullptr && name->isString()) {
        return name->asString();
    }
    return std::nullopt;
}

// An object's entity-free anchor check, used for a property with no owner: its
// parent must still be where it was, with the same name, and the key still absent.
bool ownerlessAnchorMatches(const Value& root, const ForeignProperty& property) {
    const Value* parent = resolve(root, property.parent);
    if (parent == nullptr || !parent->isObject()) {
        return false;
    }
    return parent->find(property.key) == nullptr && nameOf(*parent) == property.parentName;
}

// A property owned by an entity: the entity's identity is the UUID, so only the
// key being absent is checked.
bool ownedAnchorMatches(const Value& root, const std::string& parent, const std::string& key) {
    const Value* object = resolve(root, parent);
    return object != nullptr && object->isObject() && object->find(key) == nullptr;
}

const std::string& requiredString(const Value& object, const char* key, const std::string& at,
                                  std::string& storage) {
    const Value* v = object.find(key);
    if (v == nullptr) {
        throw ReadError(at, std::string("missing required property \"") + key + "\"");
    }
    if (!v->isString()) {
        throw ReadError(at + "/" + key, "expected a string");
    }
    storage = v->asString();
    return storage;
}

std::string writeSidecar(const Document& document, const std::string& beerJsonHash) {
    Array entities;
    forEachEntity(document, [&](std::string_view kind, const std::string& pointer, const std::string& name,
                                const Uuid& uuid) {
        Object entry;
        entry.push_back(Member{"uuid", Value::string(uuid.toString())});
        entry.push_back(Member{"kind", Value::string(std::string(kind))});
        entry.push_back(Member{"pointer", Value::string(pointer)});
        entry.push_back(Member{"name", Value::string(name)});
        entities.push_back(Value::object(std::move(entry)));
    });

    Array foreign;
    for (const ForeignProperty& property : document.foreign) {
        Object entry;
        entry.push_back(Member{"owner", property.owner.isNil() ? Value{} : Value::string(property.owner.toString())});
        entry.push_back(Member{"path", Value::string(property.path)});
        entry.push_back(Member{"key", Value::string(property.key)});
        entry.push_back(Member{"parentName", property.parentName ? Value::string(*property.parentName) : Value{}});
        entry.push_back(Member{"value", property.value});
        foreign.push_back(Value::object(std::move(entry)));
    }

    // Kettle Logic data, written for every recipe so the settings are visible
    // even when they are the defaults.
    Array boilOffs;
    Array salts;
    Array additives;
    Array prices;
    for (const Recipe& recipe : document.recipes) {
        Object boilOff;
        boilOff.push_back(Member{"evaporationPercentPerHour",
                                 Value::fromRational(recipe.boilOff.evaporationRate.valueIn(Unit::Percent))});
        boilOff.push_back(Member{"trubLossLiters", Value::fromRational(recipe.boilOff.trubLoss.valueIn(Unit::Liter))});
        Object boilOffEntry;
        boilOffEntry.push_back(Member{"uuid", Value::string(recipe.uuid.toString())});
        boilOffEntry.push_back(Member{"boilOff", Value::object(std::move(boilOff))});
        boilOffEntry.push_back(Member{"fermentationEfficiencyPercent",
                                      Value::fromRational(recipe.fermentationEfficiency.valueIn(Unit::Percent))});
        Value folderUuid;
        if (recipe.folder) {
            if (const Folder* folder = document.findFolder(*recipe.folder)) {
                folderUuid = Value::string(folder->uuid.toString());
            }
        }
        boilOffEntry.push_back(Member{"folderUuid", folderUuid});
        boilOffEntry.push_back(Member{"bjcpStyle", recipe.bjcpStyle ? Value::string(*recipe.bjcpStyle) : Value{}});
        boilOffEntry.push_back(Member{"planning", Value::string(recipe.planning)});
        boilOffEntry.push_back(Member{"currency", Value::string(recipe.currency)});
        boilOffs.push_back(Value::object(std::move(boilOffEntry)));

        const auto addPrice = [&](const Uuid& uuid, const std::optional<Price>& price) {
            if (!price) {
                return;
            }
            Object entry;
            entry.push_back(Member{"uuid", Value::string(uuid.toString())});
            entry.push_back(Member{"amount", Value::fromRational(price->amount)});
            entry.push_back(Member{"per", Value::string(std::string(beerJsonName(price->per)))});
            prices.push_back(Value::object(std::move(entry)));
        };
        for (const Fermentable& item : recipe.fermentables) {
            addPrice(item.uuid, item.price);
        }
        for (const Hop& item : recipe.hops.value_or(std::vector<Hop>{})) {
            addPrice(item.uuid, item.price);
        }
        for (const Culture& item : recipe.cultures.value_or(std::vector<Culture>{})) {
            addPrice(item.uuid, item.price);
        }
        for (const Miscellaneous& item : recipe.miscellaneous.value_or(std::vector<Miscellaneous>{})) {
            addPrice(item.uuid, item.price);
        }

        if (!recipe.miscellaneous) {
            continue;
        }
        for (const Miscellaneous& misc : *recipe.miscellaneous) {
            if (!misc.salt) {
                continue;
            }
            Object entry;
            entry.push_back(Member{"uuid", Value::string(misc.uuid.toString())});
            entry.push_back(Member{"salt", Value::string(saltName(*misc.salt))});
            salts.push_back(Value::object(std::move(entry)));
        }
        for (const Miscellaneous& misc : *recipe.miscellaneous) {
            if (!misc.additive) {
                continue;
            }
            Object entry;
            entry.push_back(Member{"uuid", Value::string(misc.uuid.toString())});
            entry.push_back(Member{"additive", Value::string(additiveName(*misc.additive))});
            additives.push_back(Value::object(std::move(entry)));
        }
    }

    Array folders;
    for (const Folder& folder : document.folders) {
        Object entry;
        entry.push_back(Member{"uuid", Value::string(folder.uuid.toString())});
        entry.push_back(Member{"name", Value::string(folder.name)});
        // Written only for the Recently Deleted folder, so older readers see the folders they know.
        if (folder.trash) {
            entry.push_back(Member{"trash", Value::boolean(true)});
        }
        folders.push_back(Value::object(std::move(entry)));
    }

    Array userStyles;
    for (const styles::UserStyle& style : document.userStyles) {
        Object entry;
        entry.push_back(Member{"id", Value::string(style.id)});
        entry.push_back(Member{"name", Value::string(style.name)});
        entry.push_back(Member{"og", rangeValue(style.ranges.og)});
        entry.push_back(Member{"fg", rangeValue(style.ranges.fg)});
        entry.push_back(Member{"ibu", rangeValue(style.ranges.ibu)});
        entry.push_back(Member{"srm", rangeValue(style.ranges.srm)});
        entry.push_back(Member{"abv", rangeValue(style.ranges.abv)});
        userStyles.push_back(Value::object(std::move(entry)));
    }

    Object root;
    root.push_back(Member{"schema", Value::fromRational(Rational(document.userStyles.empty()
                                                                     ? kSidecarSchemaWithoutStyles
                                                                     : kSidecarSchema))});
    root.push_back(Member{"beerJsonSha256", Value::string(beerJsonHash)});
    root.push_back(Member{"entities", Value::array(std::move(entities))});
    root.push_back(Member{"foreign", Value::array(std::move(foreign))});
    root.push_back(Member{"recipeSettings", Value::array(std::move(boilOffs))});
    root.push_back(Member{"saltTypes", Value::array(std::move(salts))});
    root.push_back(Member{"additiveRoles", Value::array(std::move(additives))});
    root.push_back(Member{"prices", Value::array(std::move(prices))});
    root.push_back(Member{"folders", Value::array(std::move(folders))});
    // Written only when there are user styles, so older readers see the files they know.
    if (!userStyles.empty()) {
        root.push_back(Member{"userStyles", Value::array(std::move(userStyles))});
    }
    return json::serialize(Value::object(std::move(root)), 2);
}

// A number that must be present, read exactly.
Rational requiredNumber(const Value& object, const char* key, const std::string& at) {
    const Value* v = object.find(key);
    if (v == nullptr) {
        throw ReadError(at, std::string("missing required property \"") + key + "\"");
    }
    if (!v->isNumber()) {
        throw ReadError(at + "/" + key, "expected a number");
    }
    return v->asRational();
}

// A range stored in the sidecar, read as the double it was written from.
styles::Range requiredRange(const Value& object, const char* key, const std::string& at) {
    const Value* v = object.find(key);
    if (v == nullptr || !v->isObject()) {
        throw ReadError(at + "/" + key, "expected an object");
    }
    return styles::Range{requiredNumber(*v, "low", at + "/" + key).toDouble(),
                         requiredNumber(*v, "high", at + "/" + key).toDouble()};
}

Uuid requiredUuid(const Value& object, const std::string& at) {
    std::string text;
    const auto parsed = Uuid::parse(requiredString(object, "uuid", at, text));
    if (!parsed) {
        throw ReadError(at + "/uuid", "not a UUID");
    }
    return *parsed;
}

// The boil-off settings stored for a recipe, parsed.
struct StoredBoilOff {
    Uuid uuid;
    Rational evaporationPercentPerHour;
    Rational trubLossLiters;
    Rational fermentationEfficiencyPercent;
    std::optional<Uuid> folderUuid;      // the folder the recipe is filed in, if any
    std::optional<std::string> bjcpStyle;
    std::string planning;
    std::string currency = "USD";
    bool used = false;
};

// A folder stored in the sidecar, parsed.
struct StoredFolder {
    Uuid uuid;
    std::string name;
    bool trash = false;
};

// The salt type stored for a miscellaneous ingredient, parsed.
struct StoredSalt {
    Uuid uuid;
    SaltKind salt;
    bool used = false;
};

// The additive role stored for a miscellaneous ingredient, parsed.
struct StoredAdditive {
    Uuid uuid;
    Additive additive;
    bool used = false;
};

// The price stored for an ingredient, parsed.
struct StoredPrice {
    Uuid uuid;
    Price price;
    bool used = false;
};

// An entity in the document, with the sidecar match state.
struct EntityRef {
    std::string kind;
    std::string pointer;
    std::string name;
    Uuid* uuid;
    bool matched = false;
};

// A sidecar entity entry, parsed.
struct StoredEntity {
    Uuid uuid;
    std::string kind;
    std::string pointer;
    std::string name;
    bool used = false;
};

}  // namespace

Saved saveDocument(const Document& document) {
    Saved saved;
    saved.beerJson = writeBeerJson(document);
    saved.sidecar = writeSidecar(document, crypto::sha256Hex(saved.beerJson));
    return saved;
}

bool sidecarMatches(std::string_view beerJson, std::string_view sidecar) {
    try {
        const Value side = json::parse(sidecar);
        const Value* hash = side.find("beerJsonSha256");
        return hash != nullptr && hash->isString() && hash->asString() == crypto::sha256Hex(beerJson);
    } catch (const std::exception&) {
        return false;
    }
}

Document openDocument(std::string_view beerJson, std::optional<std::string_view> sidecar) {
    Document document = readBeerJson(beerJson);
    if (!sidecar) {
        return document;
    }

    const Value side = json::parse(*sidecar);
    if (!side.isObject()) {
        throw ReadError("sidecar:", "expected an object");
    }
    const Value* schemaVersion = side.find("schema");
    if (schemaVersion == nullptr || !schemaVersion->isNumber() ||
        (schemaVersion->asRational() != Rational(kSidecarSchema) &&
         schemaVersion->asRational() != Rational(kSidecarSchemaWithoutStyles) &&
         schemaVersion->asRational() != Rational(kSidecarSchemaWithoutFolders))) {
        throw ReadError("sidecar:/schema", "unsupported sidecar schema version");
    }
    const bool hasFolders = schemaVersion->asRational() != Rational(kSidecarSchemaWithoutFolders);
    const bool hasStyles = schemaVersion->asRational() == Rational(kSidecarSchema);
    const Value* entities = side.find("entities");
    if (entities == nullptr || !entities->isArray()) {
        throw ReadError("sidecar:/entities", "expected an array");
    }
    const Value* foreign = side.find("foreign");
    if (foreign == nullptr || !foreign->isArray()) {
        throw ReadError("sidecar:/foreign", "expected an array");
    }

    const Value* recipeSettings = side.find("recipeSettings");
    if (recipeSettings == nullptr || !recipeSettings->isArray()) {
        throw ReadError("sidecar:/recipeSettings", "expected an array");
    }
    const Value* saltTypes = side.find("saltTypes");
    if (saltTypes == nullptr || !saltTypes->isArray()) {
        throw ReadError("sidecar:/saltTypes", "expected an array");
    }
    const Value* additiveRoles = side.find("additiveRoles");
    if (additiveRoles == nullptr || !additiveRoles->isArray()) {
        throw ReadError("sidecar:/additiveRoles", "expected an array");
    }
    std::vector<StoredFolder> storedFolders;
    if (hasFolders) {
        const Value* folders = side.find("folders");
        if (folders == nullptr || !folders->isArray()) {
            throw ReadError("sidecar:/folders", "expected an array");
        }
        const Array& folderItems = folders->asArray();
        for (std::size_t i = 0; i < folderItems.size(); ++i) {
            const std::string at = "sidecar:/folders/" + std::to_string(i);
            if (!folderItems[i].isObject()) {
                throw ReadError(at, "expected an object");
            }
            StoredFolder entry;
            entry.uuid = requiredUuid(folderItems[i], at);
            std::string name;
            entry.name = requiredString(folderItems[i], "name", at, name);
            // Optional, and absent from every folder but Recently Deleted, so a missing key is false.
            if (const Value* trash = folderItems[i].find("trash"); trash != nullptr && trash->isBool()) {
                entry.trash = trash->asBool();
            }
            storedFolders.push_back(std::move(entry));
        }
    }
    std::vector<styles::UserStyle> storedStyles;
    if (hasStyles) {
        const Value* userStyles = side.find("userStyles");
        if (userStyles == nullptr || !userStyles->isArray()) {
            throw ReadError("sidecar:/userStyles", "expected an array");
        }
        const Array& styleItems = userStyles->asArray();
        for (std::size_t i = 0; i < styleItems.size(); ++i) {
            const std::string at = "sidecar:/userStyles/" + std::to_string(i);
            if (!styleItems[i].isObject()) {
                throw ReadError(at, "expected an object");
            }
            styles::UserStyle style;
            std::string id;
            style.id = requiredString(styleItems[i], "id", at, id);
            if (!styles::isUserStyleId(style.id)) {
                throw ReadError(at + "/id", "a user style id must begin with \"custom-\"");
            }
            std::string name;
            style.name = requiredString(styleItems[i], "name", at, name);
            style.ranges.og = requiredRange(styleItems[i], "og", at);
            style.ranges.fg = requiredRange(styleItems[i], "fg", at);
            style.ranges.ibu = requiredRange(styleItems[i], "ibu", at);
            style.ranges.srm = requiredRange(styleItems[i], "srm", at);
            style.ranges.abv = requiredRange(styleItems[i], "abv", at);
            storedStyles.push_back(std::move(style));
        }
    }

    std::vector<StoredBoilOff> storedBoilOffs;
    const Array& boilOffItems = recipeSettings->asArray();
    for (std::size_t i = 0; i < boilOffItems.size(); ++i) {
        const std::string at = "sidecar:/recipeSettings/" + std::to_string(i);
        if (!boilOffItems[i].isObject()) {
            throw ReadError(at, "expected an object");
        }
        const Value* boilOff = boilOffItems[i].find("boilOff");
        if (boilOff == nullptr || !boilOff->isObject()) {
            throw ReadError(at, "expected a boilOff object");
        }
        StoredBoilOff entry;
        entry.uuid = requiredUuid(boilOffItems[i], at);
        entry.evaporationPercentPerHour = requiredNumber(*boilOff, "evaporationPercentPerHour", at + "/boilOff");
        entry.trubLossLiters = requiredNumber(*boilOff, "trubLossLiters", at + "/boilOff");
        if (entry.evaporationPercentPerHour < Rational(0) || !(entry.evaporationPercentPerHour < Rational(100)) ||
            entry.trubLossLiters < Rational(0)) {
            throw ReadError(at + "/boilOff", "out of range");
        }
        entry.fermentationEfficiencyPercent = requiredNumber(boilOffItems[i], "fermentationEfficiencyPercent", at);
        if (!(Rational(0) < entry.fermentationEfficiencyPercent) || Rational(100) < entry.fermentationEfficiencyPercent) {
            throw ReadError(at + "/fermentationEfficiencyPercent", "must be above 0 and up to 100");
        }
        // Both are optional, so a version 6 file, which has neither, reads as none.
        if (const Value* folderValue = boilOffItems[i].find("folderUuid");
            folderValue != nullptr && !folderValue->isNull()) {
            if (!folderValue->isString()) {
                throw ReadError(at + "/folderUuid", "expected a UUID or null");
            }
            const auto parsed = Uuid::parse(folderValue->asString());
            if (!parsed) {
                throw ReadError(at + "/folderUuid", "not a UUID");
            }
            entry.folderUuid = *parsed;
        }
        if (const Value* styleValue = boilOffItems[i].find("bjcpStyle");
            styleValue != nullptr && !styleValue->isNull()) {
            if (!styleValue->isString()) {
                throw ReadError(at + "/bjcpStyle", "expected a string or null");
            }
            entry.bjcpStyle = styleValue->asString();
        }
        // Optional, so files written before planning data existed read as none.
        if (const Value* planning = boilOffItems[i].find("planning"); planning != nullptr && planning->isString()) {
            entry.planning = planning->asString();
        }
        // Optional, so files written before prices existed read as the default currency.
        if (const Value* currency = boilOffItems[i].find("currency"); currency != nullptr) {
            if (!currency->isString() || currency->asString().size() != 3) {
                throw ReadError(at + "/currency", "expected a three-letter currency code");
            }
            entry.currency = currency->asString();
        }
        storedBoilOffs.push_back(std::move(entry));
    }

    std::vector<StoredSalt> storedSalts;
    const Array& saltItems = saltTypes->asArray();
    for (std::size_t i = 0; i < saltItems.size(); ++i) {
        const std::string at = "sidecar:/saltTypes/" + std::to_string(i);
        if (!saltItems[i].isObject()) {
            throw ReadError(at, "expected an object");
        }
        StoredSalt entry;
        entry.uuid = requiredUuid(saltItems[i], at);
        std::string saltText;
        requiredString(saltItems[i], "salt", at, saltText);
        const auto salt = parseSalt(saltText);
        if (!salt) {
            throw ReadError(at + "/salt", "unknown salt type \"" + saltText + "\"");
        }
        entry.salt = *salt;
        storedSalts.push_back(std::move(entry));
    }

    std::vector<StoredAdditive> storedAdditives;
    const Array& additiveItems = additiveRoles->asArray();
    for (std::size_t i = 0; i < additiveItems.size(); ++i) {
        const std::string at = "sidecar:/additiveRoles/" + std::to_string(i);
        if (!additiveItems[i].isObject()) {
            throw ReadError(at, "expected an object");
        }
        StoredAdditive entry;
        entry.uuid = requiredUuid(additiveItems[i], at);
        std::string additiveText;
        requiredString(additiveItems[i], "additive", at, additiveText);
        const auto additive = parseAdditive(additiveText);
        if (!additive) {
            throw ReadError(at + "/additive", "unknown additive \"" + additiveText + "\"");
        }
        entry.additive = *additive;
        storedAdditives.push_back(std::move(entry));
    }

    // Optional, so files written before prices existed have none.
    std::vector<StoredPrice> storedPrices;
    if (const Value* priceList = side.find("prices"); priceList != nullptr) {
        if (!priceList->isArray()) {
            throw ReadError("sidecar:/prices", "expected an array");
        }
        const Array& priceItems = priceList->asArray();
        for (std::size_t i = 0; i < priceItems.size(); ++i) {
            const std::string at = "sidecar:/prices/" + std::to_string(i);
            if (!priceItems[i].isObject()) {
                throw ReadError(at, "expected an object");
            }
            StoredPrice entry;
            entry.uuid = requiredUuid(priceItems[i], at);
            entry.price.amount = requiredNumber(priceItems[i], "amount", at);
            if (entry.price.amount < Rational(0)) {
                throw ReadError(at + "/amount", "a price cannot be negative");
            }
            std::string perText;
            requiredString(priceItems[i], "per", at, perText);
            const auto per = unitFromBeerJson(perText);
            if (!per) {
                throw ReadError(at + "/per", "unknown unit \"" + perText + "\"");
            }
            entry.price.per = *per;
            storedPrices.push_back(std::move(entry));
        }
    }

    // Parse the stored entities.
    std::vector<StoredEntity> stored;
    const Array& storedItems = entities->asArray();
    for (std::size_t i = 0; i < storedItems.size(); ++i) {
        const std::string at = "sidecar:/entities/" + std::to_string(i);
        if (!storedItems[i].isObject()) {
            throw ReadError(at, "expected an object");
        }
        std::string uuidText;
        StoredEntity entry;
        const auto parsed = Uuid::parse(requiredString(storedItems[i], "uuid", at, uuidText));
        if (!parsed) {
            throw ReadError(at + "/uuid", "not a UUID");
        }
        entry.uuid = *parsed;
        requiredString(storedItems[i], "kind", at, entry.kind);
        requiredString(storedItems[i], "pointer", at, entry.pointer);
        requiredString(storedItems[i], "name", at, entry.name);
        stored.push_back(std::move(entry));
    }

    // Match each entity in the document to a stored one. A name that is unique
    // among the unmatched stored entities of its kind is the strongest evidence
    // (it survives insertions and moves). Failing that, the same kind at the
    // same pointer (it survives renames). Ambiguous cases keep a fresh UUID.
    std::vector<EntityRef> refs;
    forEachEntity(document, [&](std::string_view kind, const std::string& pointer, const std::string& name,
                                Uuid& uuid) {
        refs.push_back(EntityRef{std::string(kind), pointer, name, &uuid});
    });
    for (EntityRef& ref : refs) {
        std::vector<StoredEntity*> candidates;
        for (StoredEntity& s : stored) {
            if (!s.used && s.kind == ref.kind && s.name == ref.name) {
                candidates.push_back(&s);
            }
        }
        if (candidates.size() == 1) {
            *ref.uuid = candidates[0]->uuid;
            candidates[0]->used = true;
            ref.matched = true;
        }
    }
    for (EntityRef& ref : refs) {
        if (ref.matched) {
            continue;
        }
        for (StoredEntity& s : stored) {
            if (!s.used && s.kind == ref.kind && s.pointer == ref.pointer) {
                *ref.uuid = s.uuid;
                s.used = true;
                ref.matched = true;
                break;
            }
        }
    }

    // The folders get new session ids, and keep their UUIDs, so the recipes below can find them.
    document.userStyles = std::move(storedStyles);

    for (const StoredFolder& stored : storedFolders) {
        Folder folder;
        folder.id = document.allocateId();
        folder.uuid = stored.uuid;
        folder.name = stored.name;
        folder.trash = stored.trash;
        document.folders.push_back(std::move(folder));
    }

    // Restore the Kettle Logic settings to the entities that now carry their UUIDs.
    // A setting whose entity is no longer there is ignored, and a diagnostic says so.
    for (Recipe& recipe : document.recipes) {
        for (StoredBoilOff& entry : storedBoilOffs) {
            if (!entry.used && entry.uuid == recipe.uuid) {
                recipe.boilOff.evaporationRate = Quantity::fromUnit(entry.evaporationPercentPerHour, Unit::Percent);
                recipe.boilOff.trubLoss = Quantity::fromUnit(entry.trubLossLiters, Unit::Liter);
                recipe.fermentationEfficiency = Quantity::fromUnit(entry.fermentationEfficiencyPercent, Unit::Percent);
                recipe.bjcpStyle = entry.bjcpStyle;
                recipe.planning = entry.planning;
                recipe.currency = entry.currency;
                if (entry.folderUuid) {
                    const auto folder = std::find_if(document.folders.begin(), document.folders.end(),
                                                     [&](const Folder& f) { return f.uuid == *entry.folderUuid; });
                    if (folder != document.folders.end()) {
                        recipe.folder = folder->id;
                    } else {
                        document.diagnostics.push_back(Diagnostic{
                            "sidecar:/recipeSettings", "a recipe is filed in a folder that is not stored; it is kept at the top level"});
                    }
                }
                entry.used = true;
                break;
            }
        }
        const auto restorePrice = [&](const Uuid& uuid, std::optional<Price>& slot) {
            for (StoredPrice& entry : storedPrices) {
                if (!entry.used && entry.uuid == uuid) {
                    slot = entry.price;
                    entry.used = true;
                    return;
                }
            }
        };
        for (Fermentable& item : recipe.fermentables) {
            restorePrice(item.uuid, item.price);
        }
        if (recipe.hops) {
            for (Hop& item : *recipe.hops) {
                restorePrice(item.uuid, item.price);
            }
        }
        if (recipe.cultures) {
            for (Culture& item : *recipe.cultures) {
                restorePrice(item.uuid, item.price);
            }
        }
        if (recipe.miscellaneous) {
            for (Miscellaneous& item : *recipe.miscellaneous) {
                restorePrice(item.uuid, item.price);
            }
        }
        if (!recipe.miscellaneous) {
            continue;
        }
        for (Miscellaneous& misc : *recipe.miscellaneous) {
            for (StoredSalt& entry : storedSalts) {
                if (!entry.used && entry.uuid == misc.uuid) {
                    misc.salt = entry.salt;
                    entry.used = true;
                    break;
                }
            }
            for (StoredAdditive& entry : storedAdditives) {
                if (!entry.used && entry.uuid == misc.uuid) {
                    misc.additive = entry.additive;
                    entry.used = true;
                    break;
                }
            }
        }
    }
    for (std::size_t i = 0; i < storedBoilOffs.size(); ++i) {
        if (!storedBoilOffs[i].used) {
            document.diagnostics.push_back(Diagnostic{
                "sidecar:/recipeSettings/" + std::to_string(i),
                "a boil-off setting is stored for a recipe that has no matching entity; ignored"});
        }
    }
    for (std::size_t i = 0; i < storedAdditives.size(); ++i) {
        if (!storedAdditives[i].used) {
            document.diagnostics.push_back(Diagnostic{
                "sidecar:/additiveRoles/" + std::to_string(i),
                "an additive role is stored for an ingredient that has no matching entity; ignored"});
        }
    }
    for (std::size_t i = 0; i < storedPrices.size(); ++i) {
        if (!storedPrices[i].used) {
            document.diagnostics.push_back(Diagnostic{
                "sidecar:/prices/" + std::to_string(i),
                "a price is stored for an ingredient that has no matching entity; ignored"});
        }
    }
    for (std::size_t i = 0; i < storedSalts.size(); ++i) {
        if (!storedSalts[i].used) {
            document.diagnostics.push_back(Diagnostic{
                "sidecar:/saltTypes/" + std::to_string(i),
                "a salt type is stored for an ingredient that has no matching entity; ignored"});
        }
    }

    // Restore the foreign properties.
    const Value root = json::parse(beerJson);
    const Array& foreignItems = foreign->asArray();
    for (std::size_t i = 0; i < foreignItems.size(); ++i) {
        const std::string at = "sidecar:/foreign/" + std::to_string(i);
        const Value& entry = foreignItems[i];
        if (!entry.isObject()) {
            throw ReadError(at, "expected an object");
        }

        ForeignProperty property;
        std::string pathText;
        std::string keyText;
        property.path = requiredString(entry, "path", at, pathText);
        property.key = requiredString(entry, "key", at, keyText);
        const Value* owner = entry.find("owner");
        if (owner == nullptr) {
            throw ReadError(at, "missing required property \"owner\"");
        }
        const Value* parentName = entry.find("parentName");
        if (parentName == nullptr) {
            throw ReadError(at, "missing required property \"parentName\"");
        }
        if (!parentName->isNull()) {
            if (!parentName->isString()) {
                throw ReadError(at + "/parentName", "expected a string or null");
            }
            property.parentName = parentName->asString();
        }
        const Value* value = entry.find("value");
        if (value == nullptr) {
            throw ReadError(at, "missing required property \"value\"");
        }
        property.value = *value;

        if (owner->isNull()) {
            // No owner: the path is the parent's absolute pointer, checked by name.
            property.parent = property.path;
            const bool anchored = ownerlessAnchorMatches(root, property);
            document.diagnostics.push_back(Diagnostic{
                property.pointer(),
                anchored ? "restored from the sidecar"
                         : "restored from the sidecar, but its place in the document no longer matches; kept until you check it"});
        } else {
            if (!owner->isString()) {
                throw ReadError(at + "/owner", "expected a UUID or null");
            }
            const auto ownerUuid = Uuid::parse(owner->asString());
            if (!ownerUuid) {
                throw ReadError(at + "/owner", "not a UUID");
            }
            property.owner = *ownerUuid;

            const auto ref = std::find_if(refs.begin(), refs.end(),
                                          [&](const EntityRef& r) { return *r.uuid == *ownerUuid; });
            if (ref == refs.end()) {
                // The owner is gone. Keep the property so it is not lost.
                property.parent.clear();
                document.diagnostics.push_back(Diagnostic{
                    at, "restored from the sidecar, but the entity it belongs to is no longer in the document; kept until you check it"});
            } else {
                property.parent = ref->pointer + property.path;
                const bool anchored = ownedAnchorMatches(root, property.parent, property.key);
                document.diagnostics.push_back(Diagnostic{
                    property.pointer(),
                    anchored ? "restored from the sidecar"
                             : "restored from the sidecar, but its place in the document no longer matches; kept until you check it"});
            }
        }
        document.foreign.push_back(std::move(property));
    }
    return document;
}

}  // namespace kettlelogic::model
