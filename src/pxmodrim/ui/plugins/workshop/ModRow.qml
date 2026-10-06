import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../components/controls"

Rectangle {
    id: row
    required property string itemId
    required property string kind
    required property string title
    required property string author
    required property string previewUrl
    required property string state
    required property string actionLabel
    required property bool incompatible
    required property bool queued
    required property bool active
    required property string fileSize

    color: hover.hovered ? Qt.lighter(Theme.elevate2, 1.08) : Theme.elevate2
    radius: Theme.radiusMd
    border.color: row.active ? Theme.success : Theme.border
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: workshopPanel.openItem(row.itemId, row.kind) }
    Accessible.role: Accessible.Button
    Accessible.name: row.title
    Accessible.onPressAction: workshopPanel.openItem(row.itemId, row.kind)

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 10
        anchors.topMargin: 6
        anchors.bottomMargin: 6
        spacing: 10
        Item {
            implicitWidth: activeBox.implicitWidth
            implicitHeight: activeBox.implicitHeight
            PxCheckBox {
                id: activeBox
                objectName: "activeToggle"
                anchors.centerIn: parent
                visible: row.kind === "mod" && row.state !== "missing"
                checked: row.active
                Accessible.name: "Active"
                Accessible.description: "Enable or disable " + row.title
                onToggled: {
                    workshopPanel.toggleActivation(row.itemId)
                    checked = Qt.binding(() => row.active)
                }
            }
        }
        CatalogThumb {
            Layout.fillHeight: true
            Layout.preferredWidth: height
            previewUrl: row.previewUrl
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1
            Text {
                Layout.fillWidth: true
                text: row.title
                textFormat: Text.PlainText
                color: Theme.textMain
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeMd
                font.bold: true
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: row.incompatible ? "Not for this game version" : row.author + (row.fileSize !== "Unknown" ? " · " + row.fileSize : "")
                textFormat: Text.PlainText
                color: row.incompatible ? Theme.warning : Theme.textDim
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                elide: Text.ElideRight
            }
        }
        PxButton {
            visible: row.state !== "installed"
            text: row.actionLabel
            variant: row.state === "outdated" ? "warning" : "primary"
            enabled: !row.queued
            onClicked: workshopPanel.downloadItem(row.itemId, row.kind)
        }
    }
}
