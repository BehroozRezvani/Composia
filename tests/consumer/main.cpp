#include <composia/Window.hpp>
#include <composia/Application.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include <composia/AnimationHelpers.hpp>
#include <composia/Button.hpp>

int main(int argc, char**) {
    // Keep a runtime-dependent call so Release also checks transitive static linking.
    if (argc > 1) {
        composia::Application app{true};
        {
            composia::Window window{app, L"Consumer", 320, 240};
            composia::Button button{window, L"Action"};
            button.set_bounds({10, 10, 160, 44});
        }
        app.close();
    }
    return 0;
}
