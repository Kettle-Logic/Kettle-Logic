#pragma once

namespace kettlelogic {

// The engine's version, in semantic versioning (https://semver.org). It is versioned apart from each app, which has
// its own. Every front end shows both in its About box. Keep it equal to the version in core/CMakeLists.txt; the
// engine's tests check that.
inline constexpr const char* kEngineVersion = "0.1.0";

}  // namespace kettlelogic
