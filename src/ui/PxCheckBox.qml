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

    indicator: PxCheckIndicator {
        x: control.leftPadding
        y: (control.height - height) / 2
        checkState: control.checkState
        hovered: control.hovered
        focused: control.visualFocus
        opacity: control.enabled ? 1 : Theme.disabledOpacity
    }

    contentItem: Text {
        leftPadding: control.text.length > 0 ? control.indicator.width + control.spacing : control.indicator.width
        text: control.text
        font: control.font
        color: control.enabled ? Theme.textMain : Theme.textDim
        verticalAlignment: Text.AlignVCenter
    }
}
