#pragma once

#include <QStringList>

namespace px::modrim {

class CoreContext;

// Enables and disables mods without reordering the rest of the active list.
class ActivationService {
public:
    // `ctx` is not owned and must outlive the service.
    explicit ActivationService(CoreContext& ctx);

    // Active mods that depend on packages lost once `uuids` are disabled and `enable` is
    // enabled. The inputs themselves are excluded; order follows the active list.
    QStringList dependents_of(const QStringList& uuids, const QStringList& enable = {}) const;

    // Applies a batch of enable/disable changes and returns whether the active state changed.
    // A uuid in both lists is disabled.
    bool apply(const QStringList& enable = {}, const QStringList& disable = {});

private:
    CoreContext& ctx_;
};

}  // namespace px::modrim
