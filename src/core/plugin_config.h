#pragma once

#include <concepts>
#include <optional>
#include <utility>

#include <glaze/glaze.hpp>
#include <QHash>
#include <QString>
#include <QtGlobal>
#include <spdlog/spdlog.h>

#include "core/config.h"
#include "core/events.h"

namespace px::modrim {

// A plugin's own settings, kept in plugins/<name>.json. `legacy` maps keys of
// the old shared config.json to fields of T; they seed the file the first time
// it is created. Without a service the settings live in memory only.
template <class T>
    requires std::equality_comparable<T> && std::default_initializable<T>
class PluginConfig {
public:
    PluginConfig(ConfigService* service, const QString& name, const QHash<QString, QString>& legacy = {});

    const T& value() const noexcept { return value_; }

    void update(T value);

    Event<T> changed;

private:
    T seed(const QHash<QString, QString>& legacy) const;
    void store(const T& value) const;

    ConfigService* service_;
    QString filename_;
    T value_{};
};

template <class T>
    requires std::equality_comparable<T> && std::default_initializable<T>
PluginConfig<T>::PluginConfig(ConfigService* service, const QString& name,
                              const QHash<QString, QString>& legacy)
    : service_(service), filename_(QStringLiteral("plugins/%1.json").arg(name)) {
    std::optional<T> stored;
    if (service_) {
        // plugins/*.json is unversioned, so loading cannot fail with a migration error.
        auto loaded = service_->load_existing<T>(filename_);
        Q_ASSERT(loaded.has_value());
        stored = loaded.value_or(std::nullopt);
    }
    value_ = stored ? std::move(*stored) : seed(legacy);
}

template <class T>
    requires std::equality_comparable<T> && std::default_initializable<T>
T PluginConfig<T>::seed(const QHash<QString, QString>& legacy) const {
    if (!service_ || legacy.isEmpty()) {
        return T{};
    }
    const auto raw = service_->load_raw("config.json");
    glz::json_t::object_t fields;
    for (auto it = legacy.cbegin(); it != legacy.cend(); ++it) {
        if (auto found = raw.find(it.key().toStdString()); found != raw.end()) {
            fields[it.value().toStdString()] = found->second;
        }
    }

    const auto text = glz::write_json(fields);
    if (!text) {
        spdlog::warn("Ignoring old settings for {}: cannot encode legacy values", filename_.toStdString());
        return T{};
    }
    T value{};
    if (auto error = glz::read<kJsonReadOpts>(value, *text)) {
        spdlog::warn("Ignoring old settings for {}: {}", filename_.toStdString(),
                     glz::format_error(error, *text));
        return T{};
    }
    if (!fields.empty()) {
        store(value);
    }
    return value;
}

template <class T>
    requires std::equality_comparable<T> && std::default_initializable<T>
void PluginConfig<T>::update(T value) {
    if (value == value_) {
        return;
    }
    value_ = std::move(value);
    store(value_);
    changed.notify(value_);
}

template <class T>
    requires std::equality_comparable<T> && std::default_initializable<T>
void PluginConfig<T>::store(const T& value) const {
    if (!service_) {
        return;
    }
    if (auto saved = service_->save(filename_, value); !saved) {
        spdlog::warn("Cannot save {}: {}", filename_.toStdString(), saved.error().message.toStdString());
    }
}

}  // namespace px::modrim
