import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../components/controls"

Rectangle {
    id: banner
    required property string itemId
    required property string kind
    required property string title
    required property string author
    required property string previewUrl
    required property var collage
    required property string summary
    required property string sourceLabel
    required property string compatLabel
    required property bool incompatible
    required property bool queued
    required property int memberCount
    required property string actionLabel

    color: hover.hovered ? Qt.lighter(Theme.elevate2, 1.08) : Theme.elevate2
    radius: Theme.radiusMd
    border.color: hover.hovered ? Theme.elevate4 : Theme.border
    clip: true
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: workshopPanel.openItem(banner.itemId, banner.kind) }
    Accessible.role: Accessible.Button
    Accessible.name: banner.title
    Accessible.onPressAction: workshopPanel.openItem(banner.itemId, banner.kind)

    RowLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 12
        CatalogThumb {
            Layout.fillHeight: true
            Layout.preferredWidth: Math.min(180, banner.width * 0.4)
            previewUrl: banner.previewUrl
            collage: banner.collage
            widthStep: 128
            heightStep: 64
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 4
            Text {
                Layout.fillWidth: true
                text: banner.title
                textFormat: Text.PlainText
                color: Theme.textMain
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeLg
                font.bold: true
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: banner.memberCount + " mods · " + banner.sourceLabel + " · " + banner.author
                textFormat: Text.PlainText
                color: Theme.textDim
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                Layout.fillHeight: true
                text: banner.summary
                textFormat: Text.PlainText
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeMd
                wrapMode: Text.Wrap
                elide: Text.ElideRight
                verticalAlignment: Text.AlignTop
            }
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: banner.compatLabel
                    color: banner.incompatible ? Theme.warning : Theme.textDim
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                    elide: Text.ElideRight
                }
                PxButton {
                    text: banner.actionLabel
                    variant: "primary"
                    enabled: !banner.queued
                    onClicked: workshopPanel.downloadItem(banner.itemId, banner.kind)
                }
            }
        }
    }
}
