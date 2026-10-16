#pragma once

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace px::modrim {

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
        const Id id = m_nextId++;
        m_handlers.emplace_back(id, std::move(handler));
        return id;
    }

    // Returns false when `id` is not connected.
    bool disconnect(Id id) {
        for (auto it = m_handlers.begin(); it != m_handlers.end(); ++it) {
            if (it->first == id) {
                m_handlers.erase(it);
                return true;
            }
        }
        return false;
    }

    // Named `notify`: Qt defines `emit` as a macro, which would swallow a member named so.
    void notify(const T& data) const {
        const auto snapshot = m_handlers;
        for (const auto& [id, handler] : snapshot) {
            handler(data);
        }
    }

private:
    std::vector<std::pair<Id, Handler>> m_handlers;
    Id m_nextId = 1;
};

}  // namespace px::modrim
