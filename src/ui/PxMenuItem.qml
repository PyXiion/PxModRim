import QtQuick
import QtQuick.Controls

MenuItem {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: 28
    leftPadding: 8
    rightPadding: 8
    topPadding: 0
    bottomPadding: 0
    spacing: 8
    hoverEnabled: true
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSizeMd

    HoverHandler { cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }

    indicator: PxCheckIndicator {
        x: control.leftPadding
        y: (control.height - height) / 2
        visible: control.checkable
        checkState: control.checked ? Qt.Checked : Qt.Unchecked
        hovered: control.highlighted
        opacity: control.enabled ? 1 : Theme.disabledOpacity
    }

    arrow: Image {
        x: control.width - width - control.rightPadding
        y: (control.height - height) / 2
        visible: control.subMenu !== null
        width: 12
        height: 12
        sourceSize.width: 12
        sourceSize.height: 12
        rotation: -90
        opacity: control.enabled ? 1 : Theme.disabledOpacity
        source: visible ? "image://icons/chevron-down?color=" + encodeURIComponent(Theme.textDim) : ""
    }

    contentItem: Text {
        leftPadding: control.checkable ? control.indicator.width + control.spacing : 0
        rightPadding: control.subMenu !== null ? control.arrow.width + control.spacing : 0
        text: control.text
        font: control.font
        color: Theme.textMain
        opacity: control.enabled ? 1 : Theme.disabledOpacity
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        implicitWidth: 160
        implicitHeight: 28
        radius: Theme.radiusSm
        color: control.highlighted && control.enabled
               ? (control.down ? Theme.primaryBg : Theme.elevate3)
               : "transparent"
    }
}
