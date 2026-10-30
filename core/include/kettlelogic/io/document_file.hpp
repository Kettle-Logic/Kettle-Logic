#pragma once

#include "kettlelogic/model/model.hpp"

#include <filesystem>
#include <functional>
#include <string_view>

// Reading and writing a recipe on disk. A recipe is a BeerJSON file, a sidecar
// beside it, and two backup slots:
//
//   My Recipe.json                 the document
//   My Recipe.kettlelogic.json     its sidecar, bound to the document by a SHA-256 hash
//   My Recipe.previous.json        a copy of the last consistent document, kept before each save
//   My Recipe.previous.kettlelogic.json
//   My Recipe.autosave.json        the latest unsaved work, written by autosave()
//   My Recipe.autosave.kettlelogic.json
//
// A save that is interrupted leaves a pair that does not match its hash. Loading
// then falls back to the autosave, and then to the previous copy. See
// docs/model.md, "Files".
namespace kettlelogic::io {

// Where a loaded document came from.
enum class Source {
    Current,   // the recipe's own files, consistent with each other
    Autosave,  // the latest autosave; the recipe's own files were not usable
    Previous,  // the copy kept before the last save; nothing newer was usable
};

struct LoadedDocument {
    model::Document document;
    Source source = Source::Current;
};

// The sidecar for a BeerJSON file: the same folder, the stem plus ".kettlelogic.json".
std::filesystem::path sidecarPath(const std::filesystem::path& beerJsonFile);
std::filesystem::path autosavePath(const std::filesystem::path& beerJsonFile);
std::filesystem::path previousPath(const std::filesystem::path& beerJsonFile);

// Loads the recipe, falling back in this order: the current pair, the
// autosave, the previous copy. A missing sidecar is not an error, and a
// BeerJSON file without one is usable.
//
// Throws std::system_error when the recipe does not exist at all, and
// std::runtime_error, naming each reason, when no copy is usable.
LoadedDocument loadDocument(const std::filesystem::path& beerJsonFile);

// Saves the recipe, and its sidecar, and removes the autosave.
//
// Steps, in order:
//  1. Remove temporary files left by an earlier failed save.
//  2. If the current pair is consistent, copy it to the previous slot.
//  3. Write the BeerJSON file atomically (temporary file, flush, rename).
//  4. Write the sidecar atomically. It records the hash of the new BeerJSON.
//  5. Remove the autosave.
//
// A crash after step 3 leaves a BeerJSON file whose hash the old sidecar does
// not match. Loading then uses the autosave or the previous copy. Throws
// std::system_error on failure; the target files are then unchanged, except
// as described above.
void saveDocument(const model::Document& document, const std::filesystem::path& beerJsonFile);

// Writes the latest state to the autosave slot. Safe to call often.
void autosave(const model::Document& document, const std::filesystem::path& beerJsonFile);

// Removes the autosave slot.
void discardAutosave(const std::filesystem::path& beerJsonFile);

namespace testing {

// Test hook, called after each durable step of saveDocument with a stage name:
// "previous-written", "beerjson-tmp-written", "beerjson-renamed",
// "sidecar-tmp-written", "sidecar-renamed". A test can stop the process at a
// stage to simulate a crash. Production code never sets it.
using FaultHook = std::function<void(std::string_view stage)>;
void setFaultHook(FaultHook hook);

}  // namespace testing

}  // namespace kettlelogic::io
