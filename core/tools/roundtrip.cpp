// Reads a BeerJSON file, or a BeerXML file (.xml), and writes it back as BeerJSON to
// stdout. Used by the schema conformance test to check the writer's output against the
// vendored schema.

#include "kettlelogic/io/beerxml_import.hpp"
#include "kettlelogic/model/beerjson.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: kettlelogic_roundtrip <file.json | file.xml>\n";
        return 2;
    }
    const std::string path = argv[1];
    std::ifstream in(path);
    if (!in) {
        std::cerr << "cannot open " << path << "\n";
        return 2;
    }
    std::ostringstream text;
    text << in.rdbuf();
    const bool beerXml = path.size() >= 4 && path.compare(path.size() - 4, 4, ".xml") == 0;
    try {
        const auto document = beerXml ? kettlelogic::io::importBeerXml(text.str())
                                      : kettlelogic::model::readBeerJson(text.str());
        std::cout << kettlelogic::model::writeBeerJson(document);
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    return 0;
}
