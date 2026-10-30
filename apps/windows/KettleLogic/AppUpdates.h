#pragma once

#include "pch.h"

#include "Ui.h"

#include "kettlelogic/app/app_updates.hpp"

#include <functional>

// The Windows app's side of whole-app updates. The portable part (kettlelogic/app/app_updates.hpp) reads the update
// list and decides whether a version is newer. This file fetches the list and the installer, checks the installer, and
// runs it. The installer replaces the app and keeps the user's data, which lives outside the install folder.
namespace kl::update_check {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace ctl = winrt::Microsoft::UI::Xaml::Controls;

// Checks the update list on a worker thread. `done` runs on the window's UI thread with the result.
void CheckInBackground(winrt::Microsoft::UI::Dispatching::DispatcherQueue queue,
                       std::function<void(kettlelogic::app::updates::CheckResult)> done);

// Shows that `release` is newer than this app. Install downloads the installer, checks its checksum and its publisher,
// runs it, and closes the app so the installer can replace it.
void Offer(xaml::XamlRoot const& root, winrt::Microsoft::UI::Dispatching::DispatcherQueue queue,
           const kettlelogic::app::updates::Release& release);

}  // namespace kl::update_check
