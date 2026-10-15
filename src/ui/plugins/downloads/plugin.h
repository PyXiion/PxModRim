#pragma once

#include "ui/context.h"

namespace px::ui {

class DownloadsUiPlugin final : public UiPlugin {
public:
    DownloadsUiPlugin();
    void setup(AppContext& ctx) override;
};

}  // namespace px::ui
