#include "kettlelogic/io/document_file.hpp"

#include "kettlelogic/model/sidecar.hpp"
#include "kettlelogic/uuid.hpp"

#include <cerrno>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace kettlelogic::io {

namespace fs = std::filesystem;

namespace {

testing::FaultHook& faultHook() {
    static testing::FaultHook hook;
    return hook;
}

void fault(std::string_view stage) {
    if (faultHook()) {
        faultHook()(stage);
    }
}

[[noreturn]] void throwSystem(const std::string& what, const fs::path& path) {
    throw std::system_error(errno ? errno : EIO, std::generic_category(), what + ": " + path.string());
}

std::optional<std::string> readWholeFile(const fs::path& path) {
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        return std::nullopt;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throwSystem("cannot open", path);
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) {
        throwSystem("cannot read", path);
    }
    return text;
}

// Writes `text` to `path` and makes it durable before returning. On macOS
// fsync does not force the drive's cache, so F_FULLFSYNC is used.
void writeDurably(const fs::path& path, const std::string& text) {
    std::FILE* file = std::fopen(path.string().c_str(), "wb");
    if (file == nullptr) {
        throwSystem("cannot create", path);
    }
    const bool written = std::fwrite(text.data(), 1, text.size(), file) == text.size();
    const bool flushed = written && std::fflush(file) == 0;
    bool synced = flushed;
    if (flushed) {
#if defined(_WIN32)
        synced = _commit(_fileno(file)) == 0;
#elif defined(__APPLE__)
        synced = fcntl(fileno(file), F_FULLFSYNC) != -1;
#else
        synced = fsync(fileno(file)) == 0;
#endif
    }
    const int savedErrno = errno;
    std::fclose(file);
    if (!synced) {
        errno = savedErrno ? savedErrno : EIO;
        throwSystem("cannot write", path);
    }
}

// Renames a temporary file over `target`. std::filesystem::rename replaces an
// existing file on all three platforms.
void replaceFile(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::rename(from, to, ec);
    if (ec) {
        throw std::system_error(ec, "cannot replace " + to.string());
    }
}

// Writes `text` to a temporary file next to `target`, then renames it over
// `target`. With a non-empty `stage`, the fault hook runs after the temporary
// file is durable ("<stage>-tmp-written") and after the rename ("<stage>-renamed").
// The temporary file is removed if anything fails.
void writeAtomically(const fs::path& target, const std::string& text, std::string_view stage) {
    const fs::path temporary = target.parent_path() / (target.filename().string() + ".tmp-" + Uuid::generate().toString());
    try {
        writeDurably(temporary, text);
        if (!stage.empty()) {
            fault(std::string(stage) + "-tmp-written");
        }
        replaceFile(temporary, target);
    } catch (...) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw;
    }
    if (!stage.empty()) {
        fault(std::string(stage) + "-renamed");
    }
}

// Removes temporary files left in `beerJsonFile`'s folder by a save that did not finish.
void removeStaleTemporaries(const fs::path& beerJsonFile) {
    const std::string prefix = beerJsonFile.stem().string() + ".";
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(beerJsonFile.parent_path(), ec)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind(prefix, 0) == 0 && name.find(".tmp-") != std::string::npos) {
            std::error_code ignored;
            fs::remove(entry.path(), ignored);
        }
    }
}

// Copies the pair at `from` to the pair at `to`. A missing sidecar at `from` removes the one at `to`.
void copyPair(const fs::path& from, const fs::path& to) {
    const std::optional<std::string> beerJson = readWholeFile(from);
    if (!beerJson) {
        throw std::system_error(ENOENT, std::generic_category(), "no such file: " + from.string());
    }
    writeAtomically(to, *beerJson, "");

    const std::optional<std::string> sidecar = readWholeFile(sidecarPath(from));
    if (sidecar) {
        writeAtomically(sidecarPath(to), *sidecar, "");
    } else {
        std::error_code ignored;
        fs::remove(sidecarPath(to), ignored);
    }
}

// Opens one pair. Returns the document, or the reason it is not usable.
struct Attempt {
    std::optional<model::Document> document;
    std::string reason;
};

Attempt tryOpen(const fs::path& beerJsonFile) {
    const std::optional<std::string> beerJson = readWholeFile(beerJsonFile);
    if (!beerJson) {
        return {std::nullopt, "missing"};
    }
    try {
        const std::optional<std::string> sidecar = readWholeFile(sidecarPath(beerJsonFile));
        if (sidecar && !model::sidecarMatches(*beerJson, *sidecar)) {
            return {std::nullopt, "its sidecar does not match it (the pair was caught mid-save)"};
        }
        return {model::openDocument(*beerJson, sidecar ? std::optional<std::string_view>(*sidecar) : std::nullopt),
                ""};
    } catch (const std::exception& e) {
        return {std::nullopt, e.what()};
    }
}

}  // namespace

fs::path sidecarPath(const fs::path& beerJsonFile) {
    return beerJsonFile.parent_path() / (beerJsonFile.stem().string() + ".kettlelogic.json");
}

fs::path autosavePath(const fs::path& beerJsonFile) {
    return beerJsonFile.parent_path() / (beerJsonFile.stem().string() + ".autosave.json");
}

fs::path previousPath(const fs::path& beerJsonFile) {
    return beerJsonFile.parent_path() / (beerJsonFile.stem().string() + ".previous.json");
}

LoadedDocument loadDocument(const fs::path& beerJsonFile) {
    std::vector<std::string> reasons;

    Attempt current = tryOpen(beerJsonFile);
    if (current.document) {
        return {std::move(*current.document), Source::Current};
    }
    reasons.push_back("current: " + current.reason);

    Attempt autosave = tryOpen(autosavePath(beerJsonFile));
    if (autosave.document) {
        return {std::move(*autosave.document), Source::Autosave};
    }
    if (autosave.reason != "missing") {
        reasons.push_back("autosave: " + autosave.reason);
    }

    Attempt previous = tryOpen(previousPath(beerJsonFile));
    if (previous.document) {
        return {std::move(*previous.document), Source::Previous};
    }
    if (previous.reason != "missing") {
        reasons.push_back("previous: " + previous.reason);
    }

    if (current.reason == "missing" && autosave.reason == "missing" && previous.reason == "missing") {
        throw std::system_error(ENOENT, std::generic_category(), "no such file: " + beerJsonFile.string());
    }
    std::string message = "no usable copy of " + beerJsonFile.string() + ":";
    for (const std::string& reason : reasons) {
        message += "\n  " + reason;
    }
    throw std::runtime_error(message);
}

void saveDocument(const model::Document& document, const fs::path& beerJsonFile) {
    removeStaleTemporaries(beerJsonFile);

    const model::Saved saved = model::saveDocument(document);

    // Keep the current pair as the rollback copy, but only if it is consistent,
    // so a damaged pair never replaces the last good copy.
    if (tryOpen(beerJsonFile).document) {
        copyPair(beerJsonFile, previousPath(beerJsonFile));
    }
    fault("previous-written");

    writeAtomically(beerJsonFile, saved.beerJson, "beerjson");
    writeAtomically(sidecarPath(beerJsonFile), saved.sidecar, "sidecar");

    discardAutosave(beerJsonFile);
}

void autosave(const model::Document& document, const fs::path& beerJsonFile) {
    removeStaleTemporaries(autosavePath(beerJsonFile));
    const model::Saved saved = model::saveDocument(document);
    writeAtomically(autosavePath(beerJsonFile), saved.beerJson, "");
    writeAtomically(sidecarPath(autosavePath(beerJsonFile)), saved.sidecar, "");
}

void discardAutosave(const fs::path& beerJsonFile) {
    std::error_code ignored;
    fs::remove(autosavePath(beerJsonFile), ignored);
    fs::remove(sidecarPath(autosavePath(beerJsonFile)), ignored);
}

namespace testing {

void setFaultHook(FaultHook hook) {
    faultHook() = std::move(hook);
}

}  // namespace testing

}  // namespace kettlelogic::io
