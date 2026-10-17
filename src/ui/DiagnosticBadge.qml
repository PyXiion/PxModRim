import QtQuick

// "error" | "warning" glyph with an optional count and hover tooltip.
Item {
    id: root

    property string level: "error"
    property int count: 0
    property string tooltip: ""

    readonly property color tint: level === "error" ? Theme.danger : Theme.warning

    implicitWidth: countText.visible ? 16 + 3 + countText.implicitWidth : 16
    implicitHeight: 16

    Accessible.role: Accessible.Graphic
    Accessible.name: tooltip.length > 0 ? tooltip : level

    Image {
        width: 16
        height: 16
        sourceSize.width: 16
        sourceSize.height: 16
        source: "image://icons/" + (root.level === "error" ? "error" : "warning") + "?color=" + encodeURIComponent(root.tint)
    }

    Text {
        id: countText

        visible: root.count > 0
        x: 19
        anchors.verticalCenter: parent.verticalCenter
        text: root.count
        color: root.tint
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeSm
        font.weight: Font.DemiBold
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }

    PxToolTip {
        visible: hover.hovered && root.tooltip.length > 0
        text: root.tooltip
    }
}
