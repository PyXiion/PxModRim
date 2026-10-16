#pragma once

#include "ui/context.hpp"

namespace px::modrim {

class DownloadsUiPlugin final : public UiPlugin {
public:
    DownloadsUiPlugin();
    void setup(AppContext& ctx) override;
};

}  // namespace px::modrim
