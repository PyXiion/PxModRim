import QtQuick
import QtQuick.Controls

Button {
    id: control

    // "secondary" | "primary" | "success" | "danger" (soft, fills on hover) |
    // "dangerSolid" | "ghost"
    property string variant: "secondary"
    // Name from the image://icons provider; icon-only when text is empty.
    property string iconName: ""
    property real iconRotation: 0

    readonly property bool isPrimary: variant === "primary"
    readonly property bool isSuccess: variant === "success"
    readonly property bool isDangerSolid: variant === "dangerSolid"
    readonly property bool isDanger: variant === "danger"
    readonly property bool isGhost: variant === "ghost"
    readonly property bool isSolid: isPrimary || isSuccess || isDangerSolid
    readonly property bool iconOnly: text.length === 0 && iconName.length > 0

    implicitHeight: 32
    implicitWidth: iconOnly ? 32 : Math.max(implicitBackgroundWidth + leftInset + rightInset,
                                            implicitContentWidth + leftPadding + rightPadding)
    leftPadding: iconOnly ? 8 : 12
    rightPadding: leftPadding
    topPadding: 0
    bottomPadding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSizeMd
    font.weight: isSolid ? Font.DemiBold : Font.Medium

    HoverHandler { cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }

    readonly property color foreground: isSolid ? Theme.onAccent
        : isDanger && hovered ? Theme.onAccent
        : isDanger ? Theme.danger
        : hovered ? Theme.textMain
        : Theme.textMuted

    Accessible.name: text.length > 0 ? text : ToolTip.text

    PxToolTip {
        visible: control.hovered && text.length > 0
        text: control.ToolTip.text
    }

    contentItem: Item {
        implicitWidth: content.implicitWidth
        implicitHeight: content.implicitHeight
        opacity: control.enabled ? 1 : Theme.disabledOpacity

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
        opacity: control.enabled ? 1 : Theme.disabledOpacity
        color: {
            if (control.isPrimary)
                return control.down || control.hovered ? Theme.primaryHover : Theme.primary
            if (control.isSuccess)
                return control.down || control.hovered ? Theme.successHover : Theme.success
            if (control.isDangerSolid)
                return control.down || control.hovered ? Theme.dangerHover : Theme.danger
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
            if (control.isSuccess)
                return control.hovered ? Theme.successHover : Theme.success
            if (control.isDangerSolid)
                return control.hovered ? Theme.dangerHover : Theme.danger
            if (control.isDanger)
                return Theme.danger
            if (control.isGhost)
                return control.hovered ? Theme.border : "transparent"
            return control.hovered ? Theme.elevate4 : Theme.border
        }

        Behavior on color { ColorAnimation { duration: 100 } }
    }
}
