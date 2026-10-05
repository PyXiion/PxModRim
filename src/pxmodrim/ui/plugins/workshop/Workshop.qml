import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../components/controls"

Rectangle {
    id: root
    color: Theme.elevate0

    readonly property var detail: workshopPanel.detail

    function applyFilters() {
        workshopPanel.filter(search.text.trim(), version.currentIndex === 0 ? "" : version.currentText,
            source.currentValue, sort.currentValue, tag.text.trim())
    }

    component Copy: Text {
        textFormat: Text.PlainText
        color: Theme.textMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeMd
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
    component Heading: Copy {
        color: Theme.textMain
        font.pixelSize: Theme.fontSizeLg
        font.bold: true
    }
    component CatalogGrid: GridView {
        property int columns: Math.max(1, Math.floor(width / 250))
        Layout.fillWidth: true
        Layout.preferredHeight: Math.ceil(count / columns) * cellHeight
        cellWidth: width / columns
        cellHeight: 298
        interactive: false
        clip: true
        delegate: CatalogCard {
            width: GridView.view.cellWidth - 12
            height: GridView.view.cellHeight - 12
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 2
                Heading { text: "Workshop" }
                Copy { text: "Mods & collections for RimWorld"; font.pixelSize: Theme.fontSizeSm }
            }
            PxTextField {
                id: search
                Layout.fillWidth: true
                Layout.maximumWidth: 540
                placeholderText: "Search mods or collections…"
                Accessible.name: "Search Workshop"
                maximumLength: 200
                enabled: workshopPanel.configured && !workshopPanel.busy
                onAccepted: root.applyFilters()
            }
            PxButton { text: "Search"; enabled: workshopPanel.configured && !workshopPanel.busy; onClicked: root.applyFilters() }
            Copy {
                Layout.fillWidth: false
                visible: root.width > 1000
                text: workshopPanel.gameVersion ? "Your game: " + workshopPanel.gameVersion : "Game version unknown"
                font.pixelSize: Theme.fontSizeSm
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Repeater {
                model: ["Discover", "Mods", "Collections", "Installed"]
                PxButton {
                    required property string modelData
                    text: modelData
                    variant: workshopPanel.tab === modelData ? "primary" : "ghost"
                    enabled: !workshopPanel.busy
                    onClicked: workshopPanel.selectTab(modelData)
                }
            }
            Item { Layout.fillWidth: true }
            PxButton {
                visible: workshopPanel.tab === "Installed"
                text: "Update all (" + workshopPanel.updateCount + ")"
                enabled: !workshopPanel.busy && !workshopPanel.downloading && workshopPanel.updateCount > 0
                onClicked: workshopPanel.updateAll()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: workshopPanel.configured && !workshopPanel.hasDetail && workshopPanel.tab !== "Installed"
            Copy { Layout.fillWidth: false; text: "Game version" }
            PxComboBox {
                id: version
                model: workshopPanel.versionOptions
                Accessible.name: "Game version filter"
                Component.onCompleted: currentIndex = Math.max(0, model.indexOf(workshopPanel.gameVersion))
                enabled: !workshopPanel.busy
                onActivated: root.applyFilters()
            }
            PxComboBox {
                id: source
                visible: workshopPanel.tab === "Collections"
                textRole: "text"
                valueRole: "value"
                model: [ {text: "All sources", value: "all"}, {text: "Steam collections", value: "steam"}, {text: "PxModRim picks", value: "picked"} ]
                Accessible.name: "Collection source"
                enabled: !workshopPanel.busy
                onActivated: root.applyFilters()
            }
            PxTextField {
                id: tag
                Layout.fillWidth: true
                Layout.maximumWidth: 230
                placeholderText: "Tag (exact match)"
                maximumLength: 100
                Accessible.name: "Tag filter"
                enabled: !workshopPanel.busy
                onAccepted: root.applyFilters()
            }
            Item { Layout.fillWidth: true }
            PxComboBox {
                id: sort
                textRole: "text"
                valueRole: "value"
                model: [ {text: "Popular", value: "popular"}, {text: "Recently updated", value: "updated"}, {text: "Newest", value: "newest"}, {text: "Trending", value: "trending"}, {text: "Relevance", value: "relevance"} ]
                Accessible.name: "Catalog sort order"
                enabled: !workshopPanel.busy
                onActivated: root.applyFilters()
            }
        }
        PxProgressBar { Layout.fillWidth: true; visible: workshopPanel.busy; indeterminate: true }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: errorRow.implicitHeight + 24
            visible: workshopPanel.error.length > 0
            color: Theme.elevate2
            radius: Theme.radiusMd
            RowLayout {
                id: errorRow
                anchors.fill: parent
                anchors.margins: 12
                Copy { text: workshopPanel.error; color: Theme.danger }
                PxButton { text: "Retry"; enabled: !workshopPanel.busy; onClicked: workshopPanel.retry() }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: noticeColumn.implicitHeight + 24
            visible: workshopPanel.notice.length > 0
            color: Theme.elevate2
            radius: Theme.radiusMd
            ColumnLayout {
                id: noticeColumn
                anchors.fill: parent
                anchors.margins: 12
                Copy { text: workshopPanel.notice; color: workshopPanel.pendingPlan ? Theme.warning : Theme.textMuted }
                RowLayout {
                    visible: workshopPanel.pendingPlan
                    PxButton { text: "Download available mods"; enabled: workshopPanel.canDownloadAvailable && !workshopPanel.busy && !workshopPanel.downloading; onClicked: workshopPanel.downloadAvailable() }
                    PxButton { text: "Cancel"; variant: "ghost"; onClicked: workshopPanel.dismissPlan() }
                }
            }
        }
        ScrollView {
            id: contentScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: contentScroll.availableWidth
                spacing: 16
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: !workshopPanel.configured
                    Heading { text: "Workshop catalog is disabled" }
                    Copy { text: "Set a Workshop catalog URL in Settings to browse and download mods." }
                    PxButton { text: "Open Settings"; onClicked: workshopPanel.openSettings() }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: workshopPanel.configured && !workshopPanel.hasDetail
                    Heading { text: workshopPanel.tab === "Discover" ? "Discover your next colony" : workshopPanel.tab }
                    Copy { visible: workshopPanel.tab === "Discover"; text: "Explore Steam mods and collections alongside PxModRim picks. Downloads include required dependencies, without activating mods." }
                    RowLayout {
                        visible: workshopPanel.tab === "Discover"
                        Heading { text: "Collections" }
                        PxButton { text: "Browse all"; variant: "ghost"; onClicked: workshopPanel.selectTab("Collections") }
                    }
                    CatalogGrid {
                        visible: workshopPanel.tab === "Discover" || workshopPanel.tab === "Collections"
                        model: catalogCollections
                    }
                    RowLayout {
                        visible: workshopPanel.tab === "Discover"
                        Heading { text: "Popular mods" }
                        PxButton { text: "Browse all"; variant: "ghost"; onClicked: workshopPanel.selectTab("Mods") }
                    }
                    CatalogGrid { visible: workshopPanel.tab !== "Collections"; model: catalogMods }
                    Copy {
                        visible: !workshopPanel.busy && !workshopPanel.error && (workshopPanel.tab === "Collections" ? catalogCollections.count === 0 : catalogMods.count === 0)
                        text: workshopPanel.tab === "Installed" ? "No installed Workshop mods were found." : "No matching items. Try another search or filter."
                    }
                    PxButton {
                        text: "Load more"
                        visible: workshopPanel.tab === "Collections" ? catalogCollections.hasMore : workshopPanel.tab === "Mods" && catalogMods.hasMore
                        enabled: !workshopPanel.busy
                        onClicked: workshopPanel.loadMore()
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: workshopPanel.configured && workshopPanel.hasDetail
                    PxButton { text: "← Back"; variant: "ghost"; onClicked: workshopPanel.back() }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 240
                        color: Theme.elevate2
                        radius: Theme.radiusMd
                        clip: true
                        Image {
                            id: detailImage
                            anchors.fill: parent
                            source: root.detail.previewUrl || ""
                            asynchronous: true
                            sourceSize.width: 1000
                            sourceSize.height: 480
                            fillMode: Image.PreserveAspectCrop
                        }
                        Image {
                            anchors.centerIn: parent
                            width: 48
                            height: 48
                            visible: detailImage.status !== Image.Ready
                            source: "image://icons/grid?color=" + encodeURIComponent(Theme.textDim)
                        }
                    }
                    Copy { text: root.detail.sourceLabel || ""; color: Theme.primary; font.pixelSize: Theme.fontSizeSm }
                    Heading { text: root.detail.title || ""; font.pixelSize: 26 }
                    Copy { text: "By " + (root.detail.author || "") }
                    Copy { text: "Versions: " + (root.detail.versions || "") }
                    Copy { text: root.detail.tags || "" }
                    Copy { visible: !!root.detail.incompatible; text: "⚠ This item does not support your running game version. Downloading does not make it compatible."; color: Theme.warning }
                    Copy { visible: !!root.detail.warning; text: root.detail.warning || ""; color: Theme.warning }
                    RowLayout {
                        PxButton {
                            text: root.detail.actionLabel || "Download"
                            variant: "primary"
                            enabled: !workshopPanel.busy && !workshopPanel.downloading && root.detail.state !== "installed"
                            onClicked: workshopPanel.downloadItem(root.detail.itemId, root.detail.kind)
                        }
                        PxButton { visible: !!root.detail.workshopUrl; text: "View on Steam"; onClicked: workshopPanel.openLink(root.detail.workshopUrl) }
                    }
                    Copy { text: "Downloads go to Local mods. Nothing is enabled or subscribed automatically."; font.pixelSize: Theme.fontSizeSm }
                    Copy { visible: root.detail.kind === "mod"; text: "Size: " + (root.detail.fileSize || "") + " · Votes: " + (root.detail.votes || "") }
                    Heading { text: "Description" }
                    Text {
                        Layout.fillWidth: true
                        text: root.detail.description || ""
                        textFormat: Text.RichText
                        wrapMode: Text.Wrap
                        color: Theme.textMuted
                        linkColor: Theme.primary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeMd
                        onLinkActivated: link => workshopPanel.openLink(link)
                    }
                    Heading { text: root.detail.kind === "collection" ? "Collection members (" + catalogMembers.count + ")" : "Required dependencies" }
                    Copy { visible: catalogMembers.count === 0; text: "None listed." }
                    Repeater {
                        model: catalogMembers
                        Rectangle {
                            id: member
                            required property string itemId
                            required property string kind
                            required property string title
                            required property string versions
                            required property string stateLabel
                            required property string state
                            required property string actionLabel
                            required property bool incompatible
                            Layout.fillWidth: true
                            implicitHeight: memberRow.implicitHeight + 24
                            radius: Theme.radiusMd
                            color: Theme.elevate2
                            RowLayout {
                                id: memberRow
                                anchors.fill: parent
                                anchors.margins: 12
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Copy { text: member.title; color: Theme.textMain }
                                    Copy { text: member.stateLabel + " · Versions: " + member.versions; font.pixelSize: Theme.fontSizeSm }
                                    Copy { visible: member.incompatible; text: "⚠ Incompatible with your game"; color: Theme.warning; font.pixelSize: Theme.fontSizeSm }
                                }
                                PxButton { text: "Details"; variant: "ghost"; onClicked: workshopPanel.openItem(member.itemId, member.kind) }
                                PxButton { text: member.actionLabel; enabled: !workshopPanel.busy && !workshopPanel.downloading && member.state !== "installed"; onClicked: workshopPanel.downloadItem(member.itemId, member.kind) }
                            }
                        }
                    }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            visible: workshopPanel.downloading
            implicitHeight: downloadRow.implicitHeight + 24
            color: Theme.elevate2
            radius: Theme.radiusMd
            ColumnLayout {
                id: downloadRow
                anchors.fill: parent
                anchors.margins: 12
                RowLayout {
                    Layout.fillWidth: true
                    Copy { Layout.fillWidth: true; text: "Downloading · " + workshopPanel.queueSummary; font.pixelSize: Theme.fontSizeSm }
                    PxButton { text: "Stop"; variant: "danger"; onClicked: workshopPanel.stop() }
                    PxButton { text: "Open Downloads"; variant: "ghost"; onClicked: workshopPanel.openDownloads() }
                }
                PxProgressBar { Layout.fillWidth: true; value: workshopPanel.queueProgress }
            }
        }
    }
    Component.onCompleted: workshopPanel.refresh()
}
