#include <composia/Window.hpp>
#include <composia/Application.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include <composia/AnimationHelpers.hpp>

int main(int argc, char**) {
    // Keep a runtime-dependent call so Release also checks transitive static linking.
    if (argc > 1) {
        composia::Application app{true};
        app.close();
    }
    return 0;
}
