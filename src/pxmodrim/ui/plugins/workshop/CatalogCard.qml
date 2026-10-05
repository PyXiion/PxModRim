import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../components/controls"

Rectangle {
    id: card
    required property string itemId
    required property string kind
    required property string title
    required property string author
    required property string previewUrl
    required property var collage
    required property string sourceLabel
    required property string versions
    required property string stateLabel
    required property string state
    required property string actionLabel
    required property bool incompatible
    required property bool queued
    required property int memberCount
    required property string votes
    required property string fileSize
    required property bool active

    color: hover.hovered ? Qt.lighter(Theme.elevate2, 1.08) : Theme.elevate2
    radius: Theme.radiusMd
    border.color: hover.hovered ? Theme.elevate4 : Theme.border
    clip: true
    HoverHandler { id: hover }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        Rectangle {
            id: thumb
            Layout.fillWidth: true
            Layout.preferredHeight: 110
            color: Theme.elevate3
            radius: Theme.radiusMd
            clip: true
            CatalogImage {
                id: preview
                anchors.fill: parent
                previewUrl: card.previewUrl
            }
            Grid {
                id: collageGrid
                anchors.fill: parent
                visible: card.previewUrl === "" && card.collage.length > 0
                columns: card.collage.length === 1 ? 1 : 2
                readonly property int rowCount: card.collage.length > 2 ? 2 : 1
                Repeater {
                    model: collageGrid.visible ? card.collage : []
                    CatalogImage {
                        required property string modelData
                        width: collageGrid.width / collageGrid.columns
                        height: collageGrid.height / collageGrid.rowCount
                        previewUrl: modelData
                    }
                }
            }
            Image {
                anchors.centerIn: parent
                width: 32
                height: 32
                visible: preview.status !== Image.Ready && !collageGrid.visible
                source: "image://icons/grid?color=" + encodeURIComponent(Theme.textDim)
            }
            TapHandler { onTapped: workshopPanel.openItem(card.itemId, card.kind) }
        }
        Text {
            Layout.fillWidth: true
            text: card.title
            textFormat: Text.PlainText
            color: Theme.textMain
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeMd
            font.bold: true
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            text: card.author
            textFormat: Text.PlainText
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeMd
            elide: Text.ElideRight
        }
        Flow {
            Layout.fillWidth: true
            spacing: 6
            PxBadge {
                visible: card.kind === "collection"
                compact: true
                text: card.sourceLabel.toUpperCase()
                textColor: card.sourceLabel.indexOf("pick") >= 0 ? Theme.warning : Theme.textMuted
                fillColor: card.sourceLabel.indexOf("pick") >= 0 ? Theme.warningBg : Theme.elevate3
            }
            PxBadge {
                visible: card.kind === "collection"
                compact: true
                text: card.memberCount + " MODS"
                textColor: Theme.textMuted
                fillColor: Theme.elevate3
            }
            PxBadge {
                visible: card.kind === "mod" && card.state !== "missing"
                compact: true
                text: "INSTALLED"
                textColor: Theme.success
                fillColor: Theme.successBg
            }
            PxBadge {
                visible: card.kind === "mod" && card.state === "outdated"
                compact: true
                text: "UPDATE AVAILABLE"
            }
            PxBadge {
                visible: card.incompatible
                compact: true
                text: "INCOMPATIBLE"
                textColor: Theme.warning
                fillColor: Theme.warningBg
            }
        }
        Text {
            Layout.fillWidth: true
            text: "Versions: " + card.versions
            textFormat: Text.PlainText
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeMd
            elide: Text.ElideRight
        }
        RowLayout {
            Layout.fillWidth: true
            visible: card.kind === "mod"
            spacing: 6
            Image {
                width: 16; height: 16
                sourceSize.width: 16; sourceSize.height: 16
                source: "image://icons/thumbs-up?color=" + encodeURIComponent(Theme.textMuted)
            }
            Text {
                text: card.votes
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeMd
                Accessible.name: text === "Unrated" ? text : text + " positive votes"
            }
            Item { Layout.fillWidth: true }
            Text {
                text: card.fileSize
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeMd
            }
        }
        Item { Layout.fillHeight: true }
        Flow {
            Layout.fillWidth: true
            spacing: 6
            PxButton {
                text: "Details"
                variant: "ghost"
                onClicked: workshopPanel.openItem(card.itemId, card.kind)
            }
            PxButton {
                visible: card.kind === "mod" && card.state !== "missing"
                text: card.active ? "Deactivate" : "Activate"
                onClicked: workshopPanel.toggleActivation(card.itemId)
            }
            PxButton {
                visible: card.state !== "installed"
                text: card.actionLabel
                variant: card.state === "outdated" ? "primary" : "secondary"
                enabled: !card.queued
                onClicked: workshopPanel.downloadItem(card.itemId, card.kind)
            }
        }
    }
}
