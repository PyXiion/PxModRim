#pragma once

#include <optional>

class QDialog;

namespace px::modrim {

// Gives `dialog` the frameless title-bar look of the main window. Call after
// the dialog's layout is built and before it is shown. Native file pickers keep
// the OS look. The title bar is an overlay, so the dialog's own layout only
// needs extra top margin, which is added here.
void installDialogChrome(QDialog* dialog, bool closable = true,
                         std::optional<bool> resizable = std::nullopt);

}  // namespace px::modrim
