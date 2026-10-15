import QtQuick
import QtQuick.Controls

Menu {
    id: control
    popupType: Popup.Window

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentItem.implicitWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentItem.implicitHeight + topPadding + bottomPadding)
    padding: 4
    margins: 0
    overlap: 1
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSizeMd

    delegate: PxMenuItem {}

    contentItem: ListView {
        implicitHeight: contentHeight
        model: control.contentModel
        interactive: Window.window ? contentHeight + control.topPadding + control.bottomPadding > Window.window.height : false
        clip: true
        currentIndex: control.currentIndex
        ScrollBar.vertical: PxScrollBar {}
    }

    background: Rectangle {
        implicitWidth: 160
        implicitHeight: 36
        radius: Theme.radiusMd
        color: Theme.elevate2
        border.width: 1
        border.color: Theme.border
    }
}
