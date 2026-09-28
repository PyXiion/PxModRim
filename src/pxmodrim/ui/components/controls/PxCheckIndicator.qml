import QtQuick

// Visual-only box; the owning control supplies state and handles input.
Item {
    id: root

    property int checkState: Qt.Unchecked
    property bool hovered: false
    property bool focused: false

    readonly property bool isChecked: checkState === Qt.Checked
    readonly property bool isPartial: checkState === Qt.PartiallyChecked

    implicitWidth: 16
    implicitHeight: 16

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusXs
        color: root.isChecked ? Theme.primary
             : root.isPartial ? Theme.primaryBg
             : "transparent"
        border.width: 1
        border.color: root.isChecked || root.isPartial || root.focused ? Theme.primary
                    : root.hovered ? Theme.textDim
                    : Theme.border
    }

    Image {
        anchors.centerIn: parent
        visible: root.isChecked
        width: 12
        height: 12
        sourceSize.width: 12
        sourceSize.height: 12
        source: visible ? "image://icons/check?color=" + encodeURIComponent(Theme.onAccent) : ""
    }

    Text {
        anchors.centerIn: parent
        visible: root.isPartial
        text: "\u2212"
        color: Theme.primary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeMd
        font.bold: true
    }
}
