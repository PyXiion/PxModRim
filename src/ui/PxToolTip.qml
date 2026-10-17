import QtQuick
import QtQuick.Controls

ToolTip {
    id: control
    popupType: Popup.Window

    delay: Theme.tooltipDelay
    padding: 6
    leftPadding: 8
    rightPadding: 8
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSizeSm

    contentItem: Text {
        text: control.text
        font: control.font
        color: Theme.textMain
        wrapMode: Text.Wrap
    }

    background: Rectangle {
        radius: Theme.radiusSm
        color: Theme.elevate3
        border.width: 1
        border.color: Theme.border
    }
}
