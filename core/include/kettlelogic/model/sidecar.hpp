#pragma once

#include "kettlelogic/model/beerjson.hpp"

#include <optional>
#include <string>
#include <string_view>

// The sidecar is a second file, next to the BeerJSON file, named
// <name>.kettlelogic.json. It stores what the BeerJSON file cannot hold:
//  - the UUID of each entity, so identity survives saves and reloads;
//  - the properties the schema does not define (Document::foreign), each
//    linked to the entity that contains it;
//  - Kettle Logic's own settings: each recipe's boil-off and each salt's type.
// See docs/formats/beerjson-profile.md, section 8.
namespace kettlelogic::model {

struct Saved {
    std::string beerJson;
    std::string sidecar;  // always written; it records the SHA-256 of beerJson
};

Saved saveDocument(const Document& document);

// True if `sidecar` was written together with exactly this BeerJSON text. A
// mismatch means the pair was caught mid-save, or one file was changed alone.
bool sidecarMatches(std::string_view beerJson, std::string_view sidecar);

// Reads a BeerJSON document and, when given, its sidecar.
//
// Entities are matched to the sidecar first by kind and a unique name, which
// survives insertions and moves. Failing that, by kind and pointer, which
// survives renames. Matched entities keep their stored UUID; the rest keep
// the fresh UUID the reader gave them.
//
// Each foreign property is restored to its owning entity. If the owner is no
// longer in the document, or the property is now defined by the document, the
// property is still kept, with a diagnostic, so that saving does not drop it.
//
// Throws json::ParseError or ReadError for a malformed sidecar, including an
// unsupported sidecar schema version.
Document openDocument(std::string_view beerJson, std::optional<std::string_view> sidecar);

}  // namespace kettlelogic::model
