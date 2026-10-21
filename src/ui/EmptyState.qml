import QtQuick

Item {
    id: root

    property string iconName: ""
    property string title: ""
    property string detail: ""

    implicitWidth: column.implicitWidth
    implicitHeight: column.implicitHeight

    Accessible.role: Accessible.StaticText
    Accessible.name: title

    Column {
        id: column

        anchors.centerIn: parent
        width: Math.min(root.width, 320)
        spacing: 8

        Image {
            visible: root.iconName.length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            width: 32
            height: 32
            sourceSize.width: 32
            sourceSize.height: 32
            source: visible ? "image://icons/" + root.iconName + "?color=" + encodeURIComponent(Theme.textDim) : ""
        }

        Text {
            width: parent.width
            text: root.title
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeMd
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Text {
            visible: root.detail.length > 0
            width: parent.width
            text: root.detail
            color: Theme.textDim
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
    }
}
