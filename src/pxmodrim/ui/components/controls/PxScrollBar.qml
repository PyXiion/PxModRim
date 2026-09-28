import QtQuick
import QtQuick.Controls

ScrollBar {
    id: control

    implicitWidth: Theme.scrollbarWidth
    implicitHeight: Theme.scrollbarWidth
    padding: 0
    hoverEnabled: true
    visible: policy !== ScrollBar.AlwaysOff && size < 1.0

    contentItem: Rectangle {
        implicitWidth: Theme.scrollbarWidth
        implicitHeight: Theme.scrollbarWidth
        radius: Theme.radiusPill
        color: control.pressed || control.hovered ? Theme.textMuted : Theme.textDim
    }

    background: Item {}
}
