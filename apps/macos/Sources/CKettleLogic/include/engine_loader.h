#ifndef KETTLELOGIC_ENGINE_LOADER_H
#define KETTLELOGIC_ENGINE_LOADER_H

/* Loads the engine library at `path`, and points the facade's functions (kettlelogic.h) at it. Returns 0 on
 * success. On failure it returns -1 and, when `error` is not NULL, sets *error to a message. Free that message with
 * free(), not kl_string_free, which needs the engine. Only one engine is loaded per process. Call this before any
 * other facade function: they stop the app if the engine is not loaded. The loader checks the engine's interface
 * version against KL_ENGINE_ABI_VERSION before it uses the engine. */
int kl_engine_load(const char *path, char **error);

/* The engine library in the build folder, for a run without an app bundle (swift run, or Xcode running the package).
 * Package.swift sets KETTLELOGIC_BUILD_ENGINE to its path, and this returns NULL when that is not set. */
const char *kl_build_engine_path(void);

#endif /* KETTLELOGIC_ENGINE_LOADER_H */
