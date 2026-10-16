#pragma once

#include "ui/context.hpp"
#include "ui/plugins/organizer/view.hpp"

namespace px::modrim {

class OrganizerUiPlugin final : public UiPlugin {
public:
    OrganizerUiPlugin() : UiPlugin(QStringLiteral("organizer_ui"), {QStringLiteral("organizer")}) {}
    void setup(AppContext& ctx) override { ctx.addRailView<OrganizerViewPanel>(0); }
};

} // namespace px::modrim
