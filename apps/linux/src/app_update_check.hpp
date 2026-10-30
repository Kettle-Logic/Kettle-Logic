#pragma once

#include "kettlelogic/app/app_updates.hpp"

#include <gtk/gtk.h>

#include <functional>

// The Linux app's side of whole-app updates. The portable part (kettlelogic/app/app_updates.hpp) reads the update list
// and decides whether a version is newer. This file fetches the list and shows the offer. A Flatpak app is updated by
// its runtime, so the offer gives the `flatpak update` command, not a download.
namespace kl::update_check {

// Checks the update list on a worker thread, then calls `done` on the main loop with the result.
void CheckInBackground(std::function<void(kettlelogic::app::updates::CheckResult)> done);

// Shows that `release` is newer than this app, with the command or the link that updates it.
void Offer(GtkWidget* parent, const kettlelogic::app::updates::Release& release);

}  // namespace kl::update_check
