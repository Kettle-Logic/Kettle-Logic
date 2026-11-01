#pragma once

#include "kettlelogic/app/analytics.hpp"
#include "kettlelogic/app/preferences.hpp"

#include <string>
#include <vector>

// The Windows app's side of anonymous usage and error reports. The portable part (kettlelogic/app/analytics.hpp) decides
// what may be sent and builds the request. This file posts it with WinHTTP, and leaves a marker when the app crashes.
// What is sent is listed in docs/privacy.md.
namespace kl::telemetry {

// Starts reporting: sends the crash the last run left, if any, and records that the app was opened. Call it once the
// user's telemetry choice is known. A second call does nothing. `preferences` must outlive `Stop`.
void Start(kettlelogic::app::Preferences& preferences);

// Sends what is waiting and ends reporting. Safe to call when `Start` was not.
void Stop();

// Each does nothing until `Start`, and nothing at a level that does not send it.
void Record(kettlelogic::app::analytics::Event event, std::vector<kettlelogic::app::analytics::Param> params = {});
void RecordError(const std::string& domain, const std::string& code, const std::string& where);

// Settings changed the level. Turning reports off also removes any crash marker, so nothing is sent later.
void LevelChanged();
// Settings asked for a new anonymous id.
void ResetId();

}  // namespace kl::telemetry
