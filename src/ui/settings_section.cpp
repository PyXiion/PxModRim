#include "ui/settings_section.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace px::ui {

PluginConfigSectionBase::PluginConfigSectionBase(QVariantMap initial, QObject* parent)
    : SettingsSection(parent), initial_(std::move(initial)) {}

QVariantMap PluginConfigSectionBase::initial() const { return initial_; }

bool PluginConfigSectionBase::set(const QString& key, const QVariant& value) {
    if (!initial_.contains(key)) {
        return false;
    }
    draft_.insert(key, value);
    return true;
}

core::Result<void> PluginConfigSectionBase::apply() {
    if (draft_.isEmpty()) {
        return {};
    }
    QVariantMap merged = initial_;
    for (auto it = draft_.cbegin(); it != draft_.cend(); ++it) {
        merged.insert(it.key(), it.value());
    }
    return commit(QJsonObject::fromVariantMap(merged));
}

}  // namespace px::ui
