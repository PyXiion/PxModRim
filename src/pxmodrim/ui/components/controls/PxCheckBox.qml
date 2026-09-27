import QtQuick
import QtQuick.Controls

CheckBox {
    id: control

    hoverEnabled: true
    spacing: 8
    padding: 0
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSizeMd

    HoverHandler { cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }

    indicator: Rectangle {
        x: control.leftPadding
        y: (control.height - height) / 2
        implicitWidth: 16
        implicitHeight: 16
        radius: 3
        opacity: control.enabled ? 1 : 0.45
        color: control.checkState === Qt.Checked ? Theme.primary
             : control.checkState === Qt.PartiallyChecked ? Theme.primaryBg
             : "transparent"
        border.width: 1.5
        border.color: control.checkState !== Qt.Unchecked || control.visualFocus ? Theme.primary
                    : control.hovered ? Theme.textDim
                    : Theme.border

        Text {
            anchors.centerIn: parent
            visible: control.checkState !== Qt.Unchecked
            text: control.checkState === Qt.Checked ? "\u2713" : "\u2212"
            color: control.checkState === Qt.Checked ? Theme.elevate0 : Theme.primary
            font.pixelSize: 12
            font.bold: true
        }
    }

    contentItem: Text {
        leftPadding: control.text.length > 0 ? control.indicator.width + control.spacing : control.indicator.width
        text: control.text
        font: control.font
        color: control.enabled ? Theme.textMain : Theme.textDim
        verticalAlignment: Text.AlignVCenter
    }
}
