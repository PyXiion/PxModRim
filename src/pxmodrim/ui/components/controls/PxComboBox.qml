import QtQuick
import QtQuick.Controls

ComboBox {
    id: control

    implicitHeight: 32
    leftPadding: 10
    rightPadding: 28
    hoverEnabled: true
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSizeMd

    contentItem: Text {
        text: control.displayText
        font: control.font
        color: control.enabled ? Theme.textMain : Theme.textDim
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Image {
        x: control.width - width - 10
        y: (control.height - height) / 2
        width: 12
        height: 12
        source: "image://icons/chevron-down?color=" + encodeURIComponent(control.hovered ? Theme.textMain : Theme.textDim)
        sourceSize.width: 12
        sourceSize.height: 12
        rotation: control.popup.visible ? 180 : 0
    }

    background: Rectangle {
        implicitWidth: 120
        implicitHeight: 32
        radius: Theme.radiusMd
        opacity: control.enabled ? 1 : Theme.disabledOpacity
        color: Theme.elevate0
        border.width: 1
        border.color: control.activeFocus || control.popup.visible ? Theme.primary
                    : control.hovered ? Theme.elevate4
                    : Theme.border
    }

    delegate: ItemDelegate {
        id: option
        required property int index
        required property var model
        width: ListView.view ? ListView.view.width : control.width
        height: 28
        hoverEnabled: true
        leftPadding: 8
        rightPadding: 8
        topPadding: 0
        bottomPadding: 0
        highlighted: control.highlightedIndex === index
        contentItem: Text {
            text: option.model[control.textRole] !== undefined ? option.model[control.textRole] : option.model.modelData
            font: control.font
            color: control.currentIndex === option.index ? Theme.primary : Theme.textMain
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: Theme.radiusSm
            color: option.highlighted ? Theme.elevate3 : "transparent"
        }
    }

    popup: Popup {
        y: control.height + 4
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 280)
        padding: 4

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollBar.vertical: PxScrollBar {}
        }

        background: Rectangle {
            radius: Theme.radiusMd
            color: Theme.elevate2
            border.width: 1
            border.color: Theme.border
        }
    }
}
