#include "platform.hpp"

#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace kl::platform {

namespace fs = std::filesystem;

fs::path HomeFolder() {
    if (const char* home = g_get_home_dir()) {
        return fs::path(home);
    }
    return fs::path(".");
}

fs::path DataFolder() {
    return fs::path(g_get_user_data_dir()) / "KettleLogic";
}

fs::path HelpFolder() {
    std::vector<fs::path> candidates;
    if (const char* env = std::getenv("KETTLELOGIC_HELP")) {
        candidates.emplace_back(env);
    }
    std::error_code ignored;
    const fs::path exe = fs::read_symlink("/proc/self/exe", ignored);
    if (!ignored && !exe.empty()) {
        candidates.push_back(exe.parent_path() / "Help");
        candidates.push_back(exe.parent_path().parent_path() / "share" / "kettlelogic" / "Help");
    }
    if (char* program = g_find_program_in_path(g_get_prgname() ? g_get_prgname() : "kettlelogic-gtk")) {
        const fs::path found = fs::weakly_canonical(fs::path(program), ignored);
        g_free(program);
        candidates.push_back(found.parent_path() / "Help");
        candidates.push_back(found.parent_path().parent_path() / "share" / "kettlelogic" / "Help");
    }
    candidates.push_back(fs::current_path(ignored) / "Help");
    for (const fs::path& folder : candidates) {
        if (fs::is_directory(folder, ignored)) {
            return folder;
        }
    }
    return {};
}

// MARK: File dialogs

namespace {

struct Pending {
    PathCallback done;
};

GFile* FolderFile(const fs::path& folder) {
    std::error_code ignored;
    if (folder.empty() || !fs::is_directory(folder, ignored)) {
        return nullptr;
    }
    return g_file_new_for_path(folder.c_str());
}

GListStore* Filters(const std::vector<FileType>& types) {
    GListStore* store = g_list_store_new(GTK_TYPE_FILE_FILTER);
    for (const FileType& type : types) {
        GtkFileFilter* filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, type.name.c_str());
        for (const std::string& pattern : type.patterns) {
            gtk_file_filter_add_pattern(filter, pattern.c_str());
        }
        g_list_store_append(store, filter);
        g_object_unref(filter);
    }
    return store;
}

std::optional<fs::path> PathOf(GFile* file) {
    if (file == nullptr) {
        return std::nullopt;
    }
    char* path = g_file_get_path(file);
    if (path == nullptr) {
        g_object_unref(file);
        return std::nullopt;
    }
    fs::path result(path);
    g_free(path);
    g_object_unref(file);
    return result;
}

}  // namespace

void OpenFile(GtkWindow* parent, const fs::path& startFolder, const std::vector<FileType>& types,
              const std::string& title, PathCallback done) {
    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, title.c_str());
    if (GFile* start = FolderFile(startFolder)) {
        gtk_file_dialog_set_initial_folder(dialog, start);
        g_object_unref(start);
    }
    GListStore* filters = Filters(types);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    g_object_unref(filters);
    gtk_file_dialog_open(dialog, parent, nullptr,
                         [](GObject* source, GAsyncResult* result, gpointer data) {
                             std::unique_ptr<Pending> pending(static_cast<Pending*>(data));
                             GError* error = nullptr;
                             GFile* file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), result, &error);
                             if (error != nullptr) g_error_free(error);
                             if (pending->done) pending->done(PathOf(file));
                         },
                         new Pending{std::move(done)});
    g_object_unref(dialog);
}

void SaveFile(GtkWindow* parent, const fs::path& startFolder, const std::string& suggestedName,
              const std::vector<FileType>& types, PathCallback done) {
    GtkFileDialog* dialog = gtk_file_dialog_new();
    if (GFile* start = FolderFile(startFolder)) {
        gtk_file_dialog_set_initial_folder(dialog, start);
        g_object_unref(start);
    }
    gtk_file_dialog_set_initial_name(dialog, suggestedName.c_str());
    GListStore* filters = Filters(types);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    g_object_unref(filters);
    gtk_file_dialog_save(dialog, parent, nullptr,
                         [](GObject* source, GAsyncResult* result, gpointer data) {
                             std::unique_ptr<Pending> pending(static_cast<Pending*>(data));
                             GError* error = nullptr;
                             GFile* file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), result, &error);
                             if (error != nullptr) g_error_free(error);
                             if (pending->done) pending->done(PathOf(file));
                         },
                         new Pending{std::move(done)});
    g_object_unref(dialog);
}

void ChooseFolder(GtkWindow* parent, const fs::path& startFolder, const std::string& title, PathCallback done) {
    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, title.c_str());
    if (GFile* start = FolderFile(startFolder)) {
        gtk_file_dialog_set_initial_folder(dialog, start);
        g_object_unref(start);
    }
    gtk_file_dialog_select_folder(dialog, parent, nullptr,
                                  [](GObject* source, GAsyncResult* result, gpointer data) {
                                      std::unique_ptr<Pending> pending(static_cast<Pending*>(data));
                                      GError* error = nullptr;
                                      GFile* file = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(source), result, &error);
                                      if (error != nullptr) g_error_free(error);
                                      if (pending->done) pending->done(PathOf(file));
                                  },
                                  new Pending{std::move(done)});
    g_object_unref(dialog);
}

// MARK: Files

std::string ReadTextFile(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        throw std::runtime_error("The file \"" + file.filename().string() + "\" could not be opened.");
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string text = buffer.str();
    if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        text.erase(0, 3);  // a byte order mark
    }
    return text;
}

void WriteBinaryFile(const fs::path& file, const std::string& bytes) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    if (!out) {
        throw std::runtime_error("The file \"" + file.filename().string() + "\" could not be written.");
    }
}

void WriteTextFile(const fs::path& file, const std::string& utf8) {
    WriteBinaryFile(file, utf8);
}

// MARK: Desktop

void CopyText(std::string_view utf8) {
    GdkDisplay* display = gdk_display_get_default();
    if (display == nullptr) {
        return;
    }
    const std::string text(utf8);
    gdk_clipboard_set_text(gdk_display_get_clipboard(display), text.c_str());
}

void OpenUrl(GtkWindow* parent, std::string_view url) {
    const std::string text(url);
    gtk_show_uri(parent, text.c_str(), GDK_CURRENT_TIME);
}

void DisplayLock::Set(GtkWindow* window, bool on) {
    if (on) {
        if (cookie_ != 0 || window == nullptr) {
            return;
        }
        GtkApplication* application = gtk_window_get_application(window);
        if (application == nullptr) {
            return;
        }
        application_ = GTK_APPLICATION(g_object_ref(application));
        cookie_ = gtk_application_inhibit(application_, window, GTK_APPLICATION_INHIBIT_IDLE, "Brewing in progress");
    } else if (application_ != nullptr) {
        // An application that has finished running has nothing left to release.
        if (cookie_ != 0 && g_application_get_is_registered(G_APPLICATION(application_))) {
            gtk_application_uninhibit(application_, cookie_);
        }
        cookie_ = 0;
        g_object_unref(application_);
        application_ = nullptr;
    }
}

}  // namespace kl::platform
