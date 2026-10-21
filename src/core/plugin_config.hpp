#pragma once

#include <QHash>
#include <QString>
#include <QtGlobal>
#include <concepts>
#include <optional>
#include <utility>

#include <glaze/glaze.hpp>
#include <spdlog/spdlog.h>

#include "core/config.hpp"
#include "core/events.hpp"

namespace px::modrim {

template <class T>
concept PluginSettings =
    std::equality_comparable<T> && std::default_initializable<T>;

// A plugin's own settings, kept in plugins/<name>.json. `legacy` maps keys of
// the old shared config.json to fields of T; they seed the file the first time
// it is created.
template <PluginSettings T>
class PluginConfig {
 public:
  PluginConfig(ConfigService& service, const QString& name,
               const QHash<QString, QString>& legacy = {});

  const T& value() const noexcept { return m_value; }

  void update(T value);

  Event<T> changed;

 private:
  T seed(const QHash<QString, QString>& legacy) const;
  void store(const T& value) const;

  ConfigService& m_service;
  QString m_filename;
  T m_value{};
};

template <PluginSettings T>
PluginConfig<T>::PluginConfig(ConfigService& service, const QString& name,
                              const QHash<QString, QString>& legacy)
    : m_service(service),
      m_filename(QStringLiteral("plugins/%1.json").arg(name)) {
  // plugins/*.json is unversioned, so loading cannot fail with a migration
  // error.
  auto loaded = m_service.loadExisting<T>(m_filename);
  Q_ASSERT(loaded.has_value());
  std::optional<T> stored = loaded.value_or(std::nullopt);
  m_value = stored ? std::move(*stored) : seed(legacy);
}

template <PluginSettings T>
T PluginConfig<T>::seed(const QHash<QString, QString>& legacy) const {
  if (legacy.isEmpty()) {
    return T{};
  }
  const auto raw = m_service.loadRaw(kConfigFile);
  glz::json_t::object_t fields;
  for (auto it = legacy.cbegin(); it != legacy.cend(); ++it) {
    if (auto found = raw.find(it.key().toStdString()); found != raw.end()) {
      fields[it.value().toStdString()] = found->second;
    }
  }

  const auto text = glz::write_json(fields);
  if (!text) {
    spdlog::warn("Ignoring old settings for {}: cannot encode legacy values",
                 m_filename.toStdString());
    return T{};
  }
  T value{};
  if (auto error = glz::read<kJsonReadOpts>(value, *text)) {
    spdlog::warn("Ignoring old settings for {}: {}", m_filename.toStdString(),
                 glz::format_error(error, *text));
    return T{};
  }
  if (!fields.empty()) {
    store(value);
  }
  return value;
}

template <PluginSettings T>
void PluginConfig<T>::update(T value) {
  if (value == m_value) {
    return;
  }
  m_value = std::move(value);
  store(m_value);
  changed.notify(m_value);
}

template <PluginSettings T>
void PluginConfig<T>::store(const T& value) const {
  if (auto saved = m_service.save(m_filename, value); !saved) {
    spdlog::warn("Cannot save {}: {}", m_filename.toStdString(),
                 saved.error().message.toStdString());
  }
}

}  // namespace px::modrim
