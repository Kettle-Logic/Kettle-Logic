#pragma once

#include "kettlelogic/model/model.hpp"

#include <stdexcept>
#include <string>
#include <string_view>

namespace kettlelogic::model {

// A document that is well-formed JSON but does not match the BeerJSON model.
// pointer() is the RFC 6901 JSON Pointer of the offending value, for example
// "/beerjson/recipes/0/batch_size". The whole document is "".
class ReadError : public std::runtime_error {
public:
    ReadError(std::string pointer, const std::string& message);

    const std::string& pointer() const { return pointer_; }

private:
    std::string pointer_;
};

// Reads a BeerJSON document. Throws json::ParseError for malformed JSON and
// ReadError for documents that break the schema rules the model enforces.
Document readBeerJson(std::string_view text);

// Writes a document as BeerJSON, pretty-printed with two-space indentation.
std::string writeBeerJson(const Document& document);

}  // namespace kettlelogic::model
