import QtQuick
import QtQuick.Controls

SpinBox {
    id: control

    property string toolTipText: ""

    implicitHeight: 32
    implicitWidth: 110
    editable: true
    leftPadding: 28
    rightPadding: 28
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

    up.indicator: Item {
        x: control.width - width
        width: 28
        height: control.height
        opacity: control.value < control.to ? 1 : Theme.disabledOpacity

        Text {
            anchors.centerIn: parent
            text: "+"
            font: control.font
            color: control.up.pressed || control.up.hovered ? Theme.textMain : Theme.textDim
        }
    }

    down.indicator: Item {
        width: 28
        height: control.height
        opacity: control.value > control.from ? 1 : Theme.disabledOpacity

        Text {
            anchors.centerIn: parent
            text: "\u2212"
            font: control.font
            color: control.down.pressed || control.down.hovered ? Theme.textMain : Theme.textDim
        }
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
