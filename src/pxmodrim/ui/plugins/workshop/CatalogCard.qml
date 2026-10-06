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
    required property string summary
    required property string compatLabel
    required property string state
    required property string actionLabel
    required property bool incompatible
    required property bool queued
    required property int memberCount
    required property string votes
    required property string fileSize
    required property bool active

    readonly property bool isMod: kind === "mod"
    readonly property string meta: [
        isMod ? author : memberCount + " mods · " + author,
        isMod && votes !== "Unrated" ? votes + " liked" : "",
        isMod && fileSize !== "Unknown" ? fileSize : ""
    ].filter(part => part.length > 0).join(" · ")

    color: hover.hovered ? Qt.lighter(Theme.elevate2, 1.08) : Theme.elevate2
    radius: Theme.radiusMd
    border.color: hover.hovered ? Theme.elevate4 : Theme.border
    clip: true
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: workshopPanel.openItem(card.itemId, card.kind) }
    Accessible.role: Accessible.Button
    Accessible.name: card.title
    Accessible.onPressAction: workshopPanel.openItem(card.itemId, card.kind)

    FontMetrics { id: bodyMetrics; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeMd }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6
        CatalogThumb {
            Layout.fillWidth: true
            Layout.preferredHeight: 100
            previewUrl: card.previewUrl
            collage: card.collage
        }
        Text {
            Layout.fillWidth: true
            Layout.preferredHeight: bodyMetrics.lineSpacing * 2
            text: card.title
            textFormat: Text.PlainText
            color: Theme.textMain
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeMd
            font.bold: true
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            verticalAlignment: Text.AlignTop
        }
        Text {
            Layout.fillWidth: true
            text: card.meta
            textFormat: Text.PlainText
            color: Theme.textDim
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            Layout.preferredHeight: bodyMetrics.lineSpacing * 2
            text: card.summary
            textFormat: Text.PlainText
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeMd
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            verticalAlignment: Text.AlignTop
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Text {
                Layout.fillWidth: true
                visible: card.compatLabel.length > 0
                text: card.compatLabel
                color: card.incompatible ? Theme.warning : card.compatLabel.indexOf("Works") === 0 ? Theme.success : Theme.textDim
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                elide: Text.ElideRight
            }
            PxBadge {
                visible: card.isMod && card.state !== "missing"
                compact: true
                text: card.state === "outdated" ? "UPDATE" : "INSTALLED"
                textColor: card.state === "outdated" ? Theme.primary : Theme.success
                fillColor: card.state === "outdated" ? Theme.primaryBg : Theme.successBg
            }
            PxBadge {
                visible: !card.isMod
                compact: true
                text: "COLLECTION"
                textColor: Theme.textMuted
                fillColor: Theme.elevate3
            }
        }
        Item { Layout.fillHeight: true }
        PxButton {
            Layout.fillWidth: true
            visible: card.state !== "installed"
            text: card.actionLabel
            variant: card.state === "outdated" ? "warning" : "primary"
            enabled: !card.queued
            onClicked: workshopPanel.downloadItem(card.itemId, card.kind)
        }
        PxButton {
            Layout.fillWidth: true
            visible: card.isMod && card.state === "installed"
            text: card.active ? "Deactivate" : "Activate"
            variant: card.active ? "danger" : "success"
            onClicked: workshopPanel.toggleActivation(card.itemId)
        }
    }
}
