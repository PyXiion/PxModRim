import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    height: 48
    color: Theme.elevate1

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 6

        // ── Logo + title ──
        RowLayout {
            spacing: 8
            Layout.alignment: Qt.AlignVCenter

            Image {
                source: "image://icons/logo?color=" + encodeURIComponent(Theme.primary)
                sourceSize.width: 20
                sourceSize.height: 20
                fillMode: Image.PreserveAspectFit
            }

            Text {
                text: "PxModRim"
                color: Theme.primary
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
        }

        Item { Layout.fillWidth: true }

        // ── Icon buttons ──
        HeaderButton {
            iconName: "refresh"
            tooltip: "Refresh (F5)"
            onClicked: headerController.refresh()
        }

        HeaderButton {
            iconName: "sort"
            tooltip: "Auto-sort"
            isPrimary: true
            onClicked: headerController.autoSort()
        }

        HeaderButton {
            iconName: "save"
            tooltip: "Save (Ctrl+S)"
            onClicked: headerController.save()
        }

        HeaderButton {
            iconName: "settings"
            tooltip: "Settings"
            onClicked: headerController.openSettings()
        }
    }

    // ── Bottom border ──
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.border
    }

    // ── Reusable button component ──
    component HeaderButton: Rectangle {
        id: btn
        width: 32; height: 32
        radius: Theme.radiusMd
        color: mouseArea.containsMouse ? Theme.elevate3 : "transparent"
        border.color: isPrimary ? "transparent" : "transparent"
        property bool isPrimary: false
        property string iconName: ""
        property string tooltip: ""

        signal clicked()

        Image {
            anchors.centerIn: parent
            source: {
                if (btn.iconName === "") return ""
                var color = btn.isPrimary ? "#0b0d10" : (mouseArea.containsMouse ? "#f2f3f5" : "#949ba4")
                return "image://icons/" + btn.iconName + "?color=" + encodeURIComponent(color)
            }
            sourceSize.width: 16; sourceSize.height: 16
            fillMode: Image.PreserveAspectFit
        }

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusMd
            visible: btn.isPrimary
            color: mouseArea.containsMouse ? Theme.primaryHover : Theme.primary
            z: -1
        }

        MouseArea {
            id: mouseArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: btn.clicked()

            ToolTip {
                text: btn.tooltip
                visible: mouseArea.containsMouse && btn.tooltip !== ""
                delay: 300
            }
        }
    }
}
