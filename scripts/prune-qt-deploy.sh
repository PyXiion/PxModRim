#!/usr/bin/env bash
# Removes Qt pieces PxModRim never loads from a deployed tree (linuxdeploy,
# windeployqt or macdeployqt output). The UI runs on the Basic Quick Controls
# style, imports no particles or effects and installs no Qt translators.
#
# usage: prune-qt-deploy.sh <deployed-root> [keep-dialogs]
#
# QtWebEngine's QML delegates import QtQuick.Dialogs, so the web browser
# variant passes keep-dialogs.
set -euo pipefail

root="${1:?usage: prune-qt-deploy.sh <deployed-root> [keep-dialogs]}"
keep_dialogs="${2:-}"

remove_dirs() {
  find "$root" -type d \( "$@" \) -prune -exec rm -rf {} +
}
remove_files() {
  find "$root" \( "$@" \) -prune -exec rm -rf {} +
}

remove_dirs -path '*/QtQuick/Controls/FluentWinUI3' \
  -o -path '*/QtQuick/Controls/Fusion' \
  -o -path '*/QtQuick/Controls/Imagine' \
  -o -path '*/QtQuick/Controls/Material' \
  -o -path '*/QtQuick/Controls/Universal' \
  -o -path '*/QtQuick/Controls/designer' \
  -o -path '*/QtQuick/Particles' \
  -o -path '*/QtQuick/Effects'

# Libraries, DLLs and frameworks, named per platform
# (libQt6X.so.6, Qt6X.dll, QtX.framework).
remove_files -name '*QuickControls2FluentWinUI3*' \
  -o -name '*QuickControls2Fusion*' \
  -o -name '*QuickControls2Imagine*' \
  -o -name '*QuickControls2Material*' \
  -o -name '*QuickControls2Universal*' \
  -o -name '*QuickParticles*' \
  -o -name '*QuickEffects*'

if [ "$keep_dialogs" != keep-dialogs ]; then
  remove_dirs -path '*/QtQuick/Dialogs'
  remove_files -name '*QuickDialogs2*'
fi

# Only the .qm catalogs; QtWebEngine keeps its locale .pak files.
find "$root" -path '*/translations/*.qm' -delete
