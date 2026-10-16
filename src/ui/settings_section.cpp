#include "ui/settings_section.hpp"

#include <QJsonDocument>
#include <QJsonObject>

namespace px::modrim {

PluginConfigSectionBase::PluginConfigSectionBase(QVariantMap initial, QObject* parent)
    : SettingsSection(parent), m_initial(std::move(initial)) {}

QVariantMap PluginConfigSectionBase::initial() const { return m_initial; }

bool PluginConfigSectionBase::set(const QString& key, const QVariant& value) {
    if (!m_initial.contains(key)) {
        return false;
    }
    m_draft.insert(key, value);
    return true;
}

Result<void> PluginConfigSectionBase::apply() {
    if (m_draft.isEmpty()) {
        return {};
    }
    QVariantMap merged = m_initial;
    for (auto it = m_draft.cbegin(); it != m_draft.cend(); ++it) {
        merged.insert(it.key(), it.value());
    }
    return commit(QJsonObject::fromVariantMap(merged));
}

}  // namespace px::modrim
