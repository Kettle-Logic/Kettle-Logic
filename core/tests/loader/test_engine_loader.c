/* Tests the macOS app's loader against the built engine library: it loads the engine the way the app does, then calls
 * the facade through the loader's stubs. See apps/macos/Sources/CKettleLogic/engine_loader.c. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kettlelogic/c/kettlelogic.h>
#include "engine_loader.h"

static int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

static void testLoadsTheEngine(void) {
    char *error = NULL;
    CHECK(kl_engine_load(KETTLELOGIC_ENGINE_PATH, &error) == 0);
    CHECK(error == NULL);
    CHECK(kl_engine_abi_version() == KL_ENGINE_ABI_VERSION);
    CHECK(strcmp(kl_engine_version(), KETTLELOGIC_PROJECT_VERSION) == 0);

    /* A facade call that returns a string, freed through the engine. */
    char *styles = kl_styles_json();
    CHECK(styles != NULL);
    CHECK(styles != NULL && strstr(styles, "\"id\"") != NULL);
    kl_string_free(styles);
}

static void testSecondLoadIsRefused(void) {
    char *error = NULL;
    CHECK(kl_engine_load(KETTLELOGIC_ENGINE_PATH, &error) == -1);
    CHECK(error != NULL);
    free(error);
}

static void testMissingLibraryIsRefused(void) {
    /* Runs before the real load: a failed load must leave the process free to load an engine afterwards. */
    char *error = NULL;
    CHECK(kl_engine_load("/nonexistent/libkettlelogic_engine.dylib", &error) == -1);
    CHECK(error != NULL);
    free(error);
}

int main(void) {
    testMissingLibraryIsRefused();
    testLoadsTheEngine();
    testSecondLoadIsRefused();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("engine loader: all checks passed\n");
    return 0;
}
