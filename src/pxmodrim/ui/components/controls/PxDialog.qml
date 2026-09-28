import QtQuick
import QtQuick.Controls

// Themed modal shell. Footer buttons: `standardButtons` render as PxButton
// (accept role = primary); custom footers can assign their own DialogButtonBox.
Dialog {
    id: dialog

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    padding: 16
    topPadding: 12
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSizeMd

    Overlay.modal: Rectangle { color: Theme.overlay }

    background: Rectangle {
        color: Theme.elevate2
        border.color: Theme.border
        radius: Theme.radiusLg
    }

    header: Item {
        implicitHeight: 48
        visible: dialog.title.length > 0

        Text {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            text: dialog.title
            color: Theme.textMain
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeLg
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.border
        }
    }

    footer: DialogButtonBox {
        visible: count > 0
        alignment: Qt.AlignRight
        spacing: 8
        padding: 12
        leftPadding: 16
        rightPadding: 16

        background: Rectangle {
            color: "transparent"
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: Theme.border
            }
        }

        delegate: PxButton {
            variant: DialogButtonBox.buttonRole === DialogButtonBox.AcceptRole
                     || DialogButtonBox.buttonRole === DialogButtonBox.YesRole ? "primary" : "ghost"
        }
    }
}
