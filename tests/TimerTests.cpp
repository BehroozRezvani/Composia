#include <composia/Application.hpp>
#include <composia/Timer.hpp>
#include "support/TestSupport.hpp"
#include <chrono>
#include <memory>
#include <stdexcept>

using namespace composia;
using testing::require;
using namespace std::chrono_literals;

namespace {

// Runs the UI thread's queue for a while, or until the condition holds.
template<class Condition>
bool pump(Condition&& condition, std::chrono::milliseconds limit) {
    const auto end = std::chrono::steady_clock::now() + limit;
    while (!condition() && std::chrono::steady_clock::now() < end) {
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT);
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { DispatchMessageW(&message); }
    }
    return condition();
}

void wait(std::chrono::milliseconds time) {
    (void)pump([] { return false; }, time);
}

int basics(const testing::Options& options) {
    Application app{options.warp};
    {
        int fired = 0;
        Timer once{app, [&] { ++fired; }};
        require(!once.running(), "A new timer was running");
        once.start(20ms);
        require(once.running() && pump([&] { return fired == 1; }, 2s), "A one-shot timer did not fire");
        require(!once.running(), "A one-shot timer kept running after it fired");
        wait(80ms);
        require(fired == 1, "A one-shot timer fired again");

        // Starting it again starts the wait over: restarted faster than it waits, it never fires.
        fired = 0;
        for (int i = 0; i < 6; ++i) {
            once.start(60ms);
            wait(20ms);
        }
        require(fired == 0, "Restarting did not start the wait over");
        require(pump([&] { return fired == 1; }, 2s), "The restarted timer did not fire once left alone");

        // Stopped before it fires: it never does.
        fired = 0;
        once.start(30ms);
        once.stop();
        wait(90ms);
        require(fired == 0 && !once.running(), "A stopped timer fired");

        // Repeating until stopped, also from its own callback.
        int ticks = 0;
        Timer repeating{app, [&] { ++ticks; }};
        repeating.start(10ms, true);
        require(pump([&] { return ticks >= 3; }, 2s) && repeating.running(), "A repeating timer stopped by itself");
        repeating.stop();
        const int stoppedAt = ticks;
        wait(60ms);
        require(ticks == stoppedAt, "A repeating timer went on after stop()");

        // A callback may destroy its own timer, and a destroyed timer never fires.
        int calls = 0;
        std::unique_ptr<Timer> owned;
        owned = std::make_unique<Timer>(app, [&] { ++calls; owned.reset(); });
        owned->start(10ms, true);
        require(pump([&] { return !owned; }, 2s), "The callback did not run");
        wait(60ms);
        require(calls == 1, "A timer fired after it destroyed itself");
        auto destroyed = std::make_unique<Timer>(app, [&] { ++calls; });
        destroyed->start(10ms);
        destroyed.reset();
        wait(60ms);
        require(calls == 1, "A destroyed timer fired");
    }
    app.close();
    return 0;
}

int errors(const testing::Options& options) {
    Application app{options.warp};
    bool thrown = false;
    {
        bool ran = false;
        Timer failing{app, [&] {
            ran = true;
            throw std::runtime_error("timer");
        }};
        failing.start(10ms);
        require(pump([&] { return ran; }, 2s), "The failing callback did not run");
        try {
            (void)app.run();
        } catch (const std::runtime_error& error) {
            thrown = std::string_view{error.what()} == "timer";
        }
    }
    require(thrown, "run() did not rethrow the timer's error");
    app.close();
    return 0;
}

}

int main(int argc, char** argv) {
    return testing::run(argc, argv, {{"basics", basics}, {"errors", errors}});
}
