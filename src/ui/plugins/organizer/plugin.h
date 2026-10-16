#pragma once

#include "ui/context.h"
#include "ui/plugins/organizer/view.h"

namespace px::modrim {

class OrganizerUiPlugin final : public UiPlugin {
public:
    OrganizerUiPlugin() : UiPlugin(QStringLiteral("organizer_ui"), {QStringLiteral("organizer")}) {}
    void setup(AppContext& ctx) override { ctx.addRailView<OrganizerViewPanel>(0); }
};

} // namespace px::modrim
