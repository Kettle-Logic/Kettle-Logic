#include "check.hpp"

#include "kettlelogic/command.hpp"
#include "kettlelogic/io/document_file.hpp"
#include "kettlelogic/model/beerjson.hpp"
#include "kettlelogic/model/commands.hpp"
#include "kettlelogic/uuid.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace kettlelogic::test {

namespace {

namespace fs = std::filesystem;

std::string readSample(const char* name) {
    std::ifstream in(std::string(KETTLELOGIC_TEST_DATA_DIR) + "/" + name, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void writeFile(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
}

// A fresh folder for each test, removed afterwards.
struct ScratchFolder {
    fs::path path;

    ScratchFolder() : path(fs::temp_directory_path() / ("kettlelogic-io-test-" + Uuid::generate().toString())) {
        fs::create_directories(path);
    }
    ~ScratchFolder() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

bool hasTemporaryFiles(const fs::path& folder) {
    for (const auto& entry : fs::directory_iterator(folder)) {
        if (entry.path().filename().string().find(".tmp-") != std::string::npos) {
            return true;
        }
    }
    return false;
}

void testSidecarNaming() {
    CHECK(io::sidecarPath("/tmp/My Recipe.json") == fs::path("/tmp/My Recipe.kettlelogic.json"));
    CHECK(io::autosavePath("/tmp/My Recipe.json") == fs::path("/tmp/My Recipe.autosave.json"));
    CHECK(io::previousPath("/tmp/My Recipe.json") == fs::path("/tmp/My Recipe.previous.json"));
}

void testSaveAndLoadRoundTrip() {
    ScratchFolder scratch;
    const fs::path file = scratch.path / "Recipe.json";
    writeFile(file, readSample("unknown_keys.json"));

    const io::LoadedDocument loaded = io::loadDocument(file);
    CHECK(loaded.source == io::Source::Current);
    io::saveDocument(loaded.document, file);

    CHECK(fs::exists(scratch.path / "Recipe.kettlelogic.json"));
    CHECK(!hasTemporaryFiles(scratch.path));

    const io::LoadedDocument reopened = io::loadDocument(file);
    CHECK(reopened.document.recipes[0].uuid == loaded.document.recipes[0].uuid);
    CHECK(reopened.document.foreign.size() == loaded.document.foreign.size());
}

void testOverwriteKeepsNewContent() {
    ScratchFolder scratch;
    const fs::path file = scratch.path / "Recipe.json";
    writeFile(file, readSample("sample_recipe.json"));

    io::LoadedDocument loaded = io::loadDocument(file);
    CommandStack<model::Document> stack;
    stack.execute(loaded.document, model::setRecipeName(loaded.document.recipes[0].id, "Renamed on disk"));
    io::saveDocument(loaded.document, file);
    CHECK(io::loadDocument(file).document.recipes[0].name == "Renamed on disk");
}

// A save makes the previous consistent pair the rollback copy.
void testSaveKeepsPreviousCopy() {
    ScratchFolder scratch;
    const fs::path file = scratch.path / "Recipe.json";
    writeFile(file, readSample("sample_recipe.json"));

    io::LoadedDocument loaded = io::loadDocument(file);
    io::saveDocument(loaded.document, file);  // the first save copies the original pair
    loaded.document.recipes[0].name = "Second";
    io::saveDocument(loaded.document, file);  // this one copies the first save

    CHECK(fs::exists(io::previousPath(file)));
    CHECK(io::loadDocument(io::previousPath(file)).document.recipes[0].name != "Second");
}

// A successful save removes the autosave, which the save supersedes.
void testSaveDiscardsAutosave() {
    ScratchFolder scratch;
    const fs::path file = scratch.path / "Recipe.json";
    writeFile(file, readSample("sample_recipe.json"));

    io::LoadedDocument loaded = io::loadDocument(file);
    io::autosave(loaded.document, file);
    CHECK(fs::exists(io::autosavePath(file)));
    io::saveDocument(loaded.document, file);
    CHECK(!fs::exists(io::autosavePath(file)));
    CHECK(!fs::exists(io::sidecarPath(io::autosavePath(file))));
}

// A temporary file left by an earlier failed save is removed by the next save.
void testStaleTemporaryFilesAreRemoved() {
    ScratchFolder scratch;
    const fs::path file = scratch.path / "Recipe.json";
    writeFile(file, readSample("sample_recipe.json"));
    writeFile(scratch.path / "Recipe.json.tmp-leftover", "partial");
    CHECK(hasTemporaryFiles(scratch.path));

    io::saveDocument(io::loadDocument(file).document, file);
    CHECK(!hasTemporaryFiles(scratch.path));
}

// A torn BeerJSON file (for example, truncated by a disk fault after the rename)
// falls back to the previous copy, and the damage is reported, not hidden.
void testTornFileFallsBackToPrevious() {
    ScratchFolder scratch;
    const fs::path file = scratch.path / "Recipe.json";
    writeFile(file, readSample("sample_recipe.json"));

    io::LoadedDocument loaded = io::loadDocument(file);
    io::saveDocument(loaded.document, file);
    loaded.document.recipes[0].name = "Newest";
    io::saveDocument(loaded.document, file);  // the previous copy now holds the first save

    const std::string whole = readSample("sample_recipe.json");
    writeFile(file, whole.substr(0, whole.size() / 2));  // torn

    const io::LoadedDocument recovered = io::loadDocument(file);
    CHECK(recovered.source == io::Source::Previous);
    CHECK(recovered.document.recipes[0].name != "Newest");
}

// With nothing usable, loading fails and says why, instead of inventing a document.
void testNoUsableCopyIsAnError() {
    ScratchFolder scratch;
    const fs::path file = scratch.path / "Recipe.json";
    writeFile(file, "{\"beerjson\": ");  // truncated, no sidecar, no backups
    bool threw = false;
    std::string message;
    try {
        io::loadDocument(file);
    } catch (const std::runtime_error& e) {
        threw = true;
        message = e.what();
    }
    CHECK(threw);
    CHECK(message.find("no usable copy") != std::string::npos);
}

void testMissingFileIsAnError() {
    ScratchFolder scratch;
    CHECK(throws([&] { io::loadDocument(scratch.path / "absent.json"); }));
}

// A save into a folder that does not exist fails cleanly, and leaves nothing behind.
void testFailedSaveLeavesNothing() {
    ScratchFolder scratch;
    const fs::path missing = scratch.path / "no-such-folder" / "Recipe.json";
    const model::Document doc = model::readBeerJson(readSample("sample_recipe.json"));
    CHECK(throws([&] { io::saveDocument(doc, missing); }));
    int files = 0;
    for (const auto& entry : fs::directory_iterator(scratch.path)) {
        (void)entry;
        ++files;
    }
    CHECK(files == 0);
}

#if !defined(_WIN32)

// Runs a save in a child process that exits with status 42 when the save
// reaches `stage`. _exit skips every destructor and flush, so the child dies
// exactly where a crash would stop it. Returns the child's exit status.
int saveThatCrashesAt(const fs::path& file, const model::Document& doc, const std::string& stage) {
    const pid_t pid = fork();
    if (pid == 0) {
        io::testing::setFaultHook([stage](std::string_view reached) {
            if (reached == stage) {
                _exit(42);
            }
        });
        try {
            io::saveDocument(doc, file);
        } catch (...) {
            _exit(1);
        }
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

struct CrashCase {
    const char* stage;
    io::Source expectedSource;
    const char* expectedName;  // "Sample Pale Ale" is the saved-before name, "Crashed Save" the new one
};

// What each crash point leaves behind, and what loading then returns.
//  - before the BeerJSON is replaced: the old pair is intact;
//  - after it is replaced but before the sidecar: the pair does not match, so the previous copy is used;
//  - after the sidecar is replaced: the new pair is complete.
void testCrashAtEachStage() {
    const CrashCase cases[] = {
        {"previous-written", io::Source::Current, "Sample Pale Ale"},
        {"beerjson-tmp-written", io::Source::Current, "Sample Pale Ale"},
        {"beerjson-renamed", io::Source::Previous, "Sample Pale Ale"},
        {"sidecar-tmp-written", io::Source::Previous, "Sample Pale Ale"},
        {"sidecar-renamed", io::Source::Current, "Crashed Save"},
    };
    for (const CrashCase& c : cases) {
        ScratchFolder scratch;
        const fs::path file = scratch.path / "Recipe.json";
        writeFile(file, readSample("sample_recipe.json"));

        // A complete save of the old state, so the previous copy holds it.
        io::LoadedDocument baseline = io::loadDocument(file);
        io::saveDocument(baseline.document, file);

        model::Document next = io::loadDocument(file).document;
        next.recipes[0].name = "Crashed Save";

        const int status = saveThatCrashesAt(file, next, c.stage);
        CHECK(status == 42);  // the crash point was reached

        const io::LoadedDocument after = io::loadDocument(file);
        CHECK(after.source == c.expectedSource);
        CHECK(after.document.recipes[0].name == c.expectedName);

        // Retrying the save finishes the job and leaves no temporary files.
        io::saveDocument(next, file);
        CHECK(io::loadDocument(file).document.recipes[0].name == "Crashed Save");
        CHECK(!hasTemporaryFiles(scratch.path));
    }
    io::testing::setFaultHook(nullptr);
}

// A crash during a save that followed an autosave: the autosave is newer than
// the files on disk, so it is what comes back.
void testCrashAfterAutosaveRecoversAutosave() {
    ScratchFolder scratch;
    const fs::path file = scratch.path / "Recipe.json";
    writeFile(file, readSample("sample_recipe.json"));
    io::saveDocument(io::loadDocument(file).document, file);

    model::Document work = io::loadDocument(file).document;
    work.recipes[0].name = "Crashed Save";
    io::autosave(work, file);

    const int status = saveThatCrashesAt(file, work, "beerjson-renamed");
    CHECK(status == 42);

    const io::LoadedDocument recovered = io::loadDocument(file);
    CHECK(recovered.source == io::Source::Autosave);
    CHECK(recovered.document.recipes[0].name == "Crashed Save");
}

// A crash on a first save, before any previous copy exists. The BeerJSON that
// reached disk has no sidecar, so it is read as a plain BeerJSON file.
void testCrashWithoutSidecarIsAPlainFile() {
    ScratchFolder scratch;
    const fs::path file = scratch.path / "Recipe.json";
    writeFile(file, readSample("sample_recipe.json"));

    // The first save with a previous copy absent: remove the originals first.
    fs::remove(file);
    const model::Document doc = model::readBeerJson(readSample("sample_recipe.json"));
    const int status = saveThatCrashesAt(file, doc, "beerjson-renamed");
    CHECK(status == 42);

    // The BeerJSON now stands alone with no sidecar, so it is accepted as a plain file.
    // That is the documented behavior for a file Kettle Logic did not write.
    const io::LoadedDocument alone = io::loadDocument(file);
    CHECK(alone.source == io::Source::Current);
    io::testing::setFaultHook(nullptr);
}

#endif  // !_WIN32

}  // namespace

void runIoTests() {
    testSidecarNaming();
    testSaveAndLoadRoundTrip();
    testOverwriteKeepsNewContent();
    testSaveKeepsPreviousCopy();
    testSaveDiscardsAutosave();
    testStaleTemporaryFilesAreRemoved();
    testTornFileFallsBackToPrevious();
    testNoUsableCopyIsAnError();
    testMissingFileIsAnError();
    testFailedSaveLeavesNothing();
#if !defined(_WIN32)
    testCrashAtEachStage();
    testCrashAfterAutosaveRecoversAutosave();
    testCrashWithoutSidecarIsAPlainFile();
#endif
}

}  // namespace kettlelogic::test
