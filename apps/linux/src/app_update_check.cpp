#include "app_update_check.hpp"

#include "ui.hpp"

#include <libsoup/soup.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace kl::update_check {

namespace {

namespace updates = kettlelogic::app::updates;

// The application ID, which the Flatpak manifest and the desktop file both use.
constexpr const char* kFlatpakId = "org.kettlelogic.KettleLogic";

// Fetches with libsoup, which the GNOME runtime provides, so the Flatpak needs no extra library. Blocking, so it is
// only called from the worker thread.
class SoupTransport : public updates::Transport {
public:
    std::string get(const std::string& url) override {
        SoupSession* session = soup_session_new();
        soup_session_set_user_agent(session, ("KettleLogic/" + std::string(updates::kAppVersion)).c_str());
        SoupMessage* message = soup_message_new("GET", url.c_str());
        if (message == nullptr) {
            g_object_unref(session);
            throw std::runtime_error("the update address is not valid");
        }

        GError* error = nullptr;
        GBytes* body = soup_session_send_and_read(session, message, nullptr, &error);
        const guint status = soup_message_get_status(message);
        std::string text;
        if (error == nullptr && body != nullptr && status == SOUP_STATUS_OK) {
            gsize size = 0;
            const char* data = static_cast<const char*>(g_bytes_get_data(body, &size));
            text.assign(data, size);
        }

        const std::string failure = error != nullptr ? error->message : "";
        if (error != nullptr) g_error_free(error);
        if (body != nullptr) g_bytes_unref(body);
        g_object_unref(message);
        g_object_unref(session);

        if (!failure.empty()) {
            throw std::runtime_error("the update list could not be reached: " + failure);
        }
        if (status != SOUP_STATUS_OK) {
            throw std::runtime_error("the update list could not be read (HTTP " + std::to_string(status) + ")");
        }
        return text;
    }
};

// The result travels from the worker thread to the main loop in this.
struct Pending {
    std::function<void(updates::CheckResult)> done;
    updates::CheckResult result;
};

bool RunningAsFlatpak() {
    return g_file_test("/.flatpak-info", G_FILE_TEST_EXISTS) != FALSE;
}

}  // namespace

void CheckInBackground(std::function<void(updates::CheckResult)> done) {
    // The check waits on the network, so it runs on its own thread, and the result is handed to the main loop. The
    // check never throws, so the thread cannot end early.
    auto* pending = new Pending{std::move(done), {}};
    std::thread([pending] {
        SoupTransport transport;
        pending->result = updates::check(transport, updates::Platform::linux);
        ui::Later([pending] {
            std::unique_ptr<Pending> owned(pending);
            owned->done(owned->result);
        });
    }).detach();
}

void Offer(GtkWidget* parent, const updates::Release& release) {
    const std::string version = release.version.text();
    GtkWidget* content = ui::Box(GTK_ORIENTATION_VERTICAL, 12);
    ui::Margins(content, 20, 8, 20, 20);

    ui::DialogOptions options;
    options.title = "Update Available";
    options.closeText = "Later";
    options.width = 460;

    if (RunningAsFlatpak()) {
        const std::string command = std::string("flatpak update ") + kFlatpakId;
        ui::Append(content, ui::Text("Kettle Logic " + version + " is available. Update it with this command, or install "
                                     "the new Flatpak from its release page if you installed from a file:",
                                     ui::Kind::body));
        ui::Append(content, ui::Text(command, ui::Kind::mono));
        ui::Append(content, ui::Button("Copy Command", [command] {
            gdk_clipboard_set_text(gdk_display_get_clipboard(gdk_display_get_default()), command.c_str());
        }));
    } else {
        ui::Append(content, ui::Text("Kettle Logic " + version + " is available. Download it from its release page.",
                                     ui::Kind::body));
    }
    const std::string url = release.url;
    ui::Append(content, ui::LinkButton("Release notes", [parent, url] {
        GtkUriLauncher* launcher = gtk_uri_launcher_new(url.c_str());
        gtk_uri_launcher_launch(launcher, GTK_WINDOW(gtk_widget_get_root(parent)), nullptr, nullptr, nullptr);
        g_object_unref(launcher);
    }));
    options.content = content;
    ui::ShowDialog(parent, options);
}

}  // namespace kl::update_check
