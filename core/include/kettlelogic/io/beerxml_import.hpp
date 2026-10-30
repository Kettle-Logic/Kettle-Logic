#pragma once

#include "kettlelogic/model/model.hpp"

#include <stdexcept>
#include <string>
#include <string_view>

// Reading BeerXML 1.0 and 2.01 documents, as exported by BeerSmith and other tools. The
// result is the same model a BeerJSON file is read into, so writeBeerJson() converts it to
// BeerJSON. See docs/formats/beerxml-import.md for the mapping.
namespace kettlelogic::io {

// A BeerXML document that cannot be read: malformed XML, an unsupported encoding, or a
// missing required field. The message names the element and its line.
class BeerXmlError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Reads a document whose root is <RECIPES> (with RECIPE children) or a single <RECIPE>.
//
// Tags the engine does not map, and optional fields that are missing, are reported in
// document.diagnostics, and the read goes on. Values are parsed exactly: "0.1" kg is 100 g.
// Throws BeerXmlError for anything else.
model::Document importBeerXml(std::string_view text);

}  // namespace kettlelogic::io
