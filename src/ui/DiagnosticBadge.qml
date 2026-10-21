import QtQuick

// "error" | "warning" glyph with an optional hover tooltip.
Item {
    id: root

    property string level: "error"
    property string tooltip: ""

    readonly property color tint: level === "error" ? Theme.danger : Theme.warning

    implicitWidth: 16
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

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }

    PxToolTip {
        visible: hover.hovered && root.tooltip.length > 0
        text: root.tooltip
    }
}
