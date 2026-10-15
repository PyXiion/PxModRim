#pragma once

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace px::core {

// Synchronous, single-threaded callback list. `connect` returns an id that
// `disconnect` takes back: std::function is not comparable, so the legacy
// identity check on connect is replaced by ids. `emit` runs over a snapshot,
// so handlers may connect or disconnect while an emit is in progress.
template <class T>
class Event {
public:
    using Handler = std::function<void(const T&)>;
    using Id = std::uint64_t;

    Id connect(Handler handler) {
        const Id id = next_id_++;
        handlers_.emplace_back(id, std::move(handler));
        return id;
    }

    // Returns false when `id` is not connected.
    bool disconnect(Id id) {
        for (auto it = handlers_.begin(); it != handlers_.end(); ++it) {
            if (it->first == id) {
                handlers_.erase(it);
                return true;
            }
        }
        return false;
    }

    // Named `notify`: Qt defines `emit` as a macro, which would swallow a member named so.
    void notify(const T& data) const {
        const auto snapshot = handlers_;
        for (const auto& [id, handler] : snapshot) {
            handler(data);
        }
    }

private:
    std::vector<std::pair<Id, Handler>> handlers_;
    Id next_id_ = 1;
};

}  // namespace px::core
