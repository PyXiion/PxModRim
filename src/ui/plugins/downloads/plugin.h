#pragma once

#include "ui/context.h"

namespace px::modrim {

class DownloadsUiPlugin final : public UiPlugin {
public:
    DownloadsUiPlugin();
    void setup(AppContext& ctx) override;
};

}  // namespace px::modrim
