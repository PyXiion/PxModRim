import QtQuick
import QtQuick.Controls

SpinBox {
    id: control

    property string toolTipText: ""

    implicitHeight: 32
    implicitWidth: 110
    editable: true
    hoverEnabled: true
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSizeMd

    contentItem: TextInput {
        text: control.textFromValue(control.value, control.locale)
        font: control.font
        color: control.enabled ? Theme.textMain : Theme.textDim
        selectionColor: Theme.primary
        horizontalAlignment: Qt.AlignHCenter
        verticalAlignment: Qt.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: Qt.ImhFormattedNumbersOnly
    }

    up.indicator: Text {
        x: control.width - width - 10
        height: control.height
        text: "+"
        font: control.font
        verticalAlignment: Text.AlignVCenter
        color: !control.up.pressed && !control.up.hovered ? Theme.textDim : Theme.textMain
        opacity: control.value < control.to ? 1 : Theme.disabledOpacity
    }

    down.indicator: Text {
        x: 10
        height: control.height
        text: "\u2212"
        font: control.font
        verticalAlignment: Text.AlignVCenter
        color: !control.down.pressed && !control.down.hovered ? Theme.textDim : Theme.textMain
        opacity: control.value > control.from ? 1 : Theme.disabledOpacity
    }

    background: Rectangle {
        radius: Theme.radiusMd
        opacity: control.enabled ? 1 : Theme.disabledOpacity
        color: Theme.elevate0
        border.width: 1
        border.color: control.activeFocus ? Theme.primary
                    : control.hovered ? Theme.elevate4
                    : Theme.border
    }

    PxToolTip {
        visible: control.toolTipText !== "" && control.hovered
        text: control.toolTipText
    }
}
