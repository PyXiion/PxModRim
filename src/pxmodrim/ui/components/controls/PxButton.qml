import QtQuick
import QtQuick.Controls

Button {
    id: control

    // "secondary" | "primary" | "danger" | "ghost"
    property string variant: "secondary"
    // Name from the image://icons provider; icon-only when text is empty.
    property string iconName: ""
    property real iconRotation: 0

    readonly property bool isPrimary: variant === "primary"
    readonly property bool isDanger: variant === "danger"
    readonly property bool isGhost: variant === "ghost"

    implicitHeight: 32
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    leftPadding: text.length > 0 ? 12 : 8
    rightPadding: leftPadding
    topPadding: 0
    bottomPadding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSizeMd
    font.weight: isPrimary ? Font.DemiBold : Font.Medium

    HoverHandler { cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }

    readonly property color foreground: isPrimary ? Theme.elevate0
        : isDanger && hovered ? Theme.textMain
        : isDanger ? Theme.danger
        : hovered ? Theme.textMain
        : Theme.textMuted

    ToolTip.visible: hovered && ToolTip.text.length > 0
    ToolTip.delay: 400

    contentItem: Item {
        implicitWidth: content.implicitWidth
        implicitHeight: content.implicitHeight
        opacity: control.enabled ? 1 : 0.45

        Row {
            id: content
            anchors.centerIn: parent
            spacing: 6

            Image {
                visible: control.iconName.length > 0
                anchors.verticalCenter: parent.verticalCenter
                width: 16
                height: 16
                sourceSize.width: 16
                sourceSize.height: 16
                rotation: control.iconRotation
                source: visible ? "image://icons/" + control.iconName + "?color=" + encodeURIComponent(control.foreground) : ""
            }

            Text {
                visible: control.text.length > 0
                anchors.verticalCenter: parent.verticalCenter
                text: control.text
                font: control.font
                color: control.foreground
            }
        }
    }

    background: Rectangle {
        implicitWidth: 32
        implicitHeight: 32
        radius: Theme.radiusMd
        opacity: control.enabled ? 1 : 0.45
        color: {
            if (control.isPrimary)
                return control.down || control.hovered ? Theme.primaryHover : Theme.primary
            if (control.isDanger)
                return control.down || control.hovered ? Theme.danger : Theme.dangerBg
            if (control.isGhost)
                return control.down || control.hovered ? Theme.elevate3 : "transparent"
            return control.down || control.hovered ? Theme.elevate4 : Theme.elevate3
        }
        border.width: 1
        border.color: {
            if (control.visualFocus)
                return Theme.primary
            if (control.isPrimary)
                return control.hovered ? Theme.primaryHover : Theme.primary
            if (control.isDanger)
                return Theme.danger
            if (control.isGhost)
                return control.hovered ? Theme.border : "transparent"
            return control.hovered ? Theme.elevate4 : Theme.border
        }

        Behavior on color { ColorAnimation { duration: 100 } }
    }
}
