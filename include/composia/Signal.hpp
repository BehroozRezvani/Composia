#pragma once

#include <algorithm>
#include <functional>
#include <exception>
#include <memory>
#include <utility>
#include <vector>

namespace composia {

class Connection {
public:
    Connection() = default;
    explicit Connection(std::shared_ptr<bool> active) : active_(std::move(active)) {}
    ~Connection() { disconnect(); }
    Connection(Connection&&) noexcept = default;
    Connection& operator=(Connection&& other) noexcept {
        if (this != &other) { disconnect(); active_ = std::move(other.active_); }
        return *this;
    }
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    void disconnect() noexcept { if (active_) { *active_ = false; active_.reset(); } }

private:
    std::shared_ptr<bool> active_;
};

// Connections and notifications belong to the owning UI thread.
template<class... Args>
class Signal {
public:
    Connection connect(std::function<void(Args...)> callback) {
        std::erase_if(slots_, [](const auto& slot) { return !*slot.active; });
        auto active = std::make_shared<bool>(true);
        slots_.push_back({active, std::move(callback)});
        return Connection{std::move(active)};
    }

    void emit(Args... args) {
        const auto snapshot = slots_;
        std::exception_ptr firstError;
        for (const auto& slot : snapshot) {
            if (*slot.active) {
                try { slot.callback(args...); }
                catch (...) { if (!firstError) { firstError = std::current_exception(); } }
            }
        }
        if (firstError) { std::rethrow_exception(firstError); }
    }

private:
    struct Slot { std::shared_ptr<bool> active; std::function<void(Args...)> callback; };
    std::vector<Slot> slots_;
};

}
