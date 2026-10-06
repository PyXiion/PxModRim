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
    required property bool incompatible
    required property bool queued
    required property bool active
    required property string fileSize

    color: hover.hovered ? Qt.lighter(Theme.elevate2, 1.08) : Theme.elevate2
    radius: Theme.radiusMd
    border.color: Theme.border
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: workshopPanel.openItem(row.itemId, row.kind) }
    Accessible.role: Accessible.Button
    Accessible.name: row.title
    Accessible.onPressAction: workshopPanel.openItem(row.itemId, row.kind)

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 12
        anchors.topMargin: 6
        anchors.bottomMargin: 6
        spacing: 12
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
                text: row.author + (row.fileSize !== "Unknown" ? " · " + row.fileSize : "")
                textFormat: Text.PlainText
                color: Theme.textDim
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                elide: Text.ElideRight
            }
        }
        PxBadge {
            visible: row.incompatible
            compact: true
            text: "INCOMPATIBLE"
            textColor: Theme.warning
            fillColor: Theme.warningBg
        }
        PxBadge {
            visible: row.active
            compact: true
            text: "ACTIVE"
            textColor: Theme.success
            fillColor: Theme.successBg
        }
        PxButton {
            visible: row.state === "outdated"
            text: "Update"
            variant: "warning"
            enabled: !row.queued
            onClicked: workshopPanel.downloadItem(row.itemId, row.kind)
        }
        PxButton {
            text: row.active ? "Deactivate" : "Activate"
            variant: row.active ? "danger" : "success"
            onClicked: workshopPanel.toggleActivation(row.itemId)
        }
    }
}
