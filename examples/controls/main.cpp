#include "Playback.hpp"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int show) {
    try {
        composia::Application app;
        int result{};
        {
            playback::Playback panel(app);
            composia::ElementWindow window(app, L"Audio output", 344, 184, panel.root);
            window.show(show);
            result = app.run();
        }
        app.close();
        return result;
    } catch (...) { return 1; }
}
