import QtQuick
import QtQuick.Controls

TextField {
    id: control

    property bool invalid: false
    property bool monospace: false

    implicitHeight: 32
    leftPadding: 10
    rightPadding: 10
    topPadding: 0
    bottomPadding: 0
    verticalAlignment: TextInput.AlignVCenter
    hoverEnabled: true
    selectByMouse: true
    color: readOnly ? Theme.textMuted : Theme.textMain
    placeholderTextColor: Theme.textDim
    selectionColor: Theme.primary
    selectedTextColor: Theme.elevate0
    font.family: monospace ? "monospace" : Theme.fontFamily
    font.pixelSize: Theme.fontSizeMd

    background: Rectangle {
        implicitWidth: 120
        implicitHeight: 32
        radius: Theme.radiusMd
        opacity: control.enabled ? 1 : 0.45
        color: control.readOnly ? "transparent" : Theme.elevate0
        border.width: 1
        border.color: control.invalid ? Theme.danger
                    : control.activeFocus ? Theme.primary
                    : control.readOnly ? "transparent"
                    : control.hovered ? Theme.elevate4
                    : Theme.border

        Behavior on border.color { ColorAnimation { duration: 100 } }
    }
}
