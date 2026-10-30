// The GTK4 and libadwaita front end. It has the same features as the macOS and Windows apps: the app's state, settings
// and files are the portable logic shared with the Windows app, and the screens are built from GTK widgets.
//
// Usage: kettlelogic-gtk [Recipe.json ...]. Recipes in the recipe folder open at start, and so do the files named.

#include "main_window.hpp"

#include <memory>

namespace {

std::unique_ptr<kl::MainWindow> gWindow;

void Show(AdwApplication* application) {
    if (!gWindow) {
        gWindow = std::make_unique<kl::MainWindow>(application);
    }
    gWindow->Present();
}

}  // namespace

int main(int argc, char** argv) {
    g_set_prgname("kettlelogic-gtk");
    g_set_application_name("Kettle Logic");
    AdwApplication* application = adw_application_new("org.kettlelogic.KettleLogic", G_APPLICATION_HANDLES_OPEN);

    g_signal_connect(application, "activate", G_CALLBACK(+[](GApplication* app, gpointer) { Show(ADW_APPLICATION(app)); }),
                     nullptr);
    g_signal_connect(application, "open",
                     G_CALLBACK(+[](GApplication* app, GFile** files, int count, const char*, gpointer) {
                         Show(ADW_APPLICATION(app));
                         std::vector<std::filesystem::path> paths;
                         for (int i = 0; i < count; ++i) {
                             if (char* path = g_file_get_path(files[i])) {
                                 paths.emplace_back(path);
                                 g_free(path);
                             }
                         }
                         gWindow->OpenFiles(paths);
                     }),
                     nullptr);
    g_signal_connect(application, "shutdown", G_CALLBACK(+[](GApplication*, gpointer) { gWindow.reset(); }), nullptr);

    const int status = g_application_run(G_APPLICATION(application), argc, argv);
    g_object_unref(application);
    return status;
}
