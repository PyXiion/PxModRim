#pragma once

#include <expected>
#include <functional>
#include <string>
#include <utility>

#include <glaze/glaze.hpp>
#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariant>
#include <QVariantMap>
#include <spdlog/spdlog.h>

#include "core/config.hpp"
#include "core/error.hpp"
#include "core/plugin_config.hpp"

namespace px::modrim {

// A plugin's group on the Settings dialog. `source()` is loaded into a group titled
// `title()` with this object as its required `section` property. Edits stay on the
// object until the dialog is saved, when `apply()` commits them; a cancelled dialog
// just drops it.
class SettingsSection : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    virtual QString title() const = 0;
    virtual QUrl source() const = 0;

    virtual Result<void> apply() = 0;
};

// Builds a plugin's section for one dialog, parented to that dialog.
using SettingsSectionFactory = std::function<SettingsSection*(QObject* parent)>;

// The QML-facing half of PluginConfigSection, independent of the settings type:
// QML reads `initial` and reports edits via `set`, and apply() hands the merged
// values to the typed commit().
class PluginConfigSectionBase : public SettingsSection {
    Q_OBJECT

    Q_PROPERTY(QVariantMap initial READ initial CONSTANT)

public:
    QVariantMap initial() const;

    // Returns false when `key` is not a field of the settings, so QML can tell the edit was dropped.
    Q_INVOKABLE bool set(const QString& key, const QVariant& value);

    Result<void> apply() final;

protected:
    PluginConfigSectionBase(QVariantMap initial, QObject* parent);

    // Commits the merged settings; an error leaves the stored config unchanged.
    virtual Result<void> commit(const QJsonObject& merged) = 0;

private:
    QVariantMap m_initial;
    QVariantMap m_draft;
};

// Edits a PluginConfig<T>. Keys and values follow T's JSON layout.
template <class T>
class PluginConfigSection : public PluginConfigSectionBase {
public:
    PluginConfigSection(PluginConfig<T>& config, QObject* parent)
        : PluginConfigSectionBase(toVariantMap(config.value()), parent), m_config(config) {}

protected:
    Result<void> commit(const QJsonObject& merged) override {
        const std::string text = QJsonDocument(merged).toJson(QJsonDocument::Compact).toStdString();
        T value{};
        if (auto error = glz::read<kJsonReadOpts>(value, text)) {
            return std::unexpected(Error{QString::fromStdString(glz::format_error(error, text))});
        }
        m_config.update(std::move(value));
        return {};
    }

private:
    static QVariantMap toVariantMap(const T& value) {
        auto text = glz::write_json(value);
        if (!text) {
            spdlog::warn("cannot encode plugin settings for the settings dialog");
            return {};
        }
        return QJsonDocument::fromJson(QByteArray::fromStdString(*text)).object().toVariantMap();
    }

    PluginConfig<T>& m_config;
};

}  // namespace px::modrim
