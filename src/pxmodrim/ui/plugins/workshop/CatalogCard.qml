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
    required property string sourceLabel
    required property string versions
    required property string stateLabel
    required property string state
    required property string actionLabel
    required property bool incompatible
    required property int memberCount

    color: Theme.elevate2
    radius: Theme.radiusMd
    border.color: Theme.border
    clip: true

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 110
            color: Theme.elevate3
            radius: Theme.radiusMd
            clip: true
            Image {
                id: preview
                anchors.fill: parent
                source: card.previewUrl
                asynchronous: true
                fillMode: Image.PreserveAspectCrop
                sourceSize.width: 480
                sourceSize.height: 240
            }
            Image {
                anchors.centerIn: parent
                width: 32
                height: 32
                visible: preview.status !== Image.Ready
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
            font.pixelSize: Theme.fontSizeSm
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            text: card.kind === "collection" ? card.sourceLabel + " · " + card.memberCount + " members" : card.stateLabel
            textFormat: Text.PlainText
            color: card.state === "outdated" ? Theme.primary : Theme.textDim
            font.pixelSize: Theme.fontSizeSm
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            text: card.incompatible ? "⚠ Incompatible with your game" : "Versions: " + card.versions
            textFormat: Text.PlainText
            color: card.incompatible ? Theme.warning : Theme.textDim
            font.pixelSize: Theme.fontSizeXs
            elide: Text.ElideRight
        }
        Item { Layout.fillHeight: true }
        RowLayout {
            Layout.fillWidth: true
            PxButton {
                text: "Details"
                variant: "ghost"
                onClicked: workshopPanel.openItem(card.itemId, card.kind)
            }
            Item { Layout.fillWidth: true }
            PxButton {
                text: card.actionLabel
                variant: "primary"
                enabled: !workshopPanel.busy && !workshopPanel.downloading && card.state !== "installed"
                onClicked: workshopPanel.downloadItem(card.itemId, card.kind)
            }
        }
    }
}
