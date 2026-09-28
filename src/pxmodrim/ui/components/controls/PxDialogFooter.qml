import QtQuick
import QtQuick.Layouts

// Dialog footer strip; children are laid out in a right-aligned row.
Rectangle {
    id: root

    default property alias content: row.data

    implicitHeight: 56
    radius: Theme.radiusLg
    color: Theme.elevate1
    border.width: 1
    border.color: Theme.border

    // Squares the top corners so only the bottom follows the dialog's radius.
    Rectangle {
        width: parent.width
        height: Theme.radiusLg
        color: Theme.elevate1

        Rectangle { width: 1; height: parent.height; color: Theme.border }
        Rectangle { x: parent.width - 1; width: 1; height: parent.height; color: Theme.border }
        Rectangle { width: parent.width; height: 1; color: Theme.border }
    }

    RowLayout {
        id: row

        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 8

        Item { Layout.fillWidth: true }
    }
}
