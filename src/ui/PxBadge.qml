import QtQuick

// Small rounded label used for mod metadata (version, provider, tags, startup impact).
// `outlined` adds a 1px border in `accent` over a tinted fill (provider style).
Rectangle {
    id: root

    property string text: ""
    property color textColor: Theme.primary
    property color fillColor: Theme.primaryBg
    property color accent: "transparent"
    property bool outlined: false
    property bool compact: false
    property string tooltip: ""

    implicitHeight: compact ? 18 : 20
    implicitWidth: label.implicitWidth + 12
    radius: Theme.radiusSm
    color: outlined ? Theme.elevate3 : fillColor
    border.width: outlined ? 1 : 0
    border.color: accent

    Accessible.role: Accessible.StaticText
    Accessible.name: text

    Rectangle {
        visible: root.outlined
        anchors.fill: parent
        radius: parent.radius
        color: root.accent
        opacity: 0.18
    }

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: root.textColor
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        font.weight: Font.Bold
    }

    HoverHandler {
        id: hover
        enabled: root.tooltip.length > 0
    }

    Loader {
        active: hover.hovered && root.tooltip.length > 0
        sourceComponent: PxToolTip {
            parent: root
            visible: true
            text: root.tooltip
        }
    }
}
