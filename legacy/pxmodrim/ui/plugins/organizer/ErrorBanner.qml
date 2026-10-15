import QtQuick
import QtQuick.Layouts

// Inline error strip; hidden while `message` is empty.
Rectangle {
    id: root

    property string text: ""

    visible: text !== ""
    color: Theme.dangerBg
    border.color: Theme.danger
    border.width: 1
    radius: Theme.radiusSm
    implicitHeight: errorText.implicitHeight + 14

    Accessible.role: Accessible.AlertMessage
    Accessible.name: text

    RowLayout {
        anchors.fill: parent
        anchors.margins: 6

        Text {
            id: errorText

            Layout.fillWidth: true
            text: root.text
            color: Theme.danger
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            wrapMode: Text.Wrap
        }
    }
}
