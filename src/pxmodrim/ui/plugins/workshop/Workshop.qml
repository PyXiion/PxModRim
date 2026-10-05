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
            source.currentValue, sort.currentValue, tag.selectedTag)
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
        cellHeight: 382
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
                Copy { text: "Mods & collections for RimWorld" }
            }
            PxTextField {
                id: search
                Layout.fillWidth: true
                Layout.maximumWidth: 540
                placeholderText: "Search mods or collections…"
                Accessible.name: "Search Workshop"
                maximumLength: 200
                enabled: workshopPanel.configured
                onAccepted: root.applyFilters()
            }
            PxButton { text: "Search"; variant: "primary"; enabled: workshopPanel.configured; onClicked: root.applyFilters() }
            PxProgressBar {
                Layout.preferredWidth: 48
                Layout.alignment: Qt.AlignVCenter
                thickness: 3
                indeterminate: true
                opacity: workshopPanel.busy || workshopPanel.planning ? 1 : 0
                Accessible.name: "Loading"
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                Repeater {
                    model: ["Discover", "Mods", "Collections", "Installed"]
                    Item {
                        id: tabItem
                        required property string modelData
                        readonly property bool current: workshopPanel.tab === modelData
                        activeFocusOnTab: true
                        Keys.onReturnPressed: workshopPanel.selectTab(modelData)
                        Keys.onSpacePressed: workshopPanel.selectTab(modelData)
                        Rectangle {
                            anchors.fill: parent
                            visible: tabItem.activeFocus
                            color: "transparent"
                            border.color: Theme.primary
                            radius: Theme.radiusSm
                        }
                        implicitWidth: tabLabel.implicitWidth + 32
                        implicitHeight: 36
                        Text {
                            id: tabLabel
                            anchors.centerIn: parent
                            text: tabItem.modelData
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeMd
                            font.weight: tabItem.current ? Font.DemiBold : Font.Medium
                            color: tabItem.current ? Theme.primary : tabHover.hovered ? Theme.textMain : Theme.textMuted
                        }
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 2
                            color: tabItem.current ? Theme.primary : "transparent"
                        }
                        HoverHandler { id: tabHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: workshopPanel.selectTab(tabItem.modelData) }
                        Accessible.role: Accessible.PageTab
                        Accessible.name: modelData
                        Accessible.selected: current
                        Accessible.onPressAction: workshopPanel.selectTab(modelData)
                    }
                }
                Item { Layout.fillWidth: true }
                PxButton {
                    visible: workshopPanel.tab === "Installed"
                    text: "Update all (" + workshopPanel.updateCount + ")"
                    variant: "warning"
                    enabled: workshopPanel.updateCount > 0 && !workshopPanel.planning
                    onClicked: workshopPanel.updateAll()
                }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: workshopPanel.configured && !workshopPanel.hasDetail && (workshopPanel.tab === "Mods" || workshopPanel.tab === "Collections")
            Copy { Layout.fillWidth: false; text: "Game version" }
            PxComboBox {
                id: version
                model: workshopPanel.versionOptions
                Accessible.name: "Game version filter"
                Component.onCompleted: currentIndex = Math.max(0, model.indexOf(workshopPanel.gameVersion))
                onActivated: root.applyFilters()
            }
            PxComboBox {
                id: source
                visible: workshopPanel.tab === "Collections"
                textRole: "text"
                valueRole: "value"
                model: [ {text: "All sources", value: "all"}, {text: "Steam collections", value: "steam"}, {text: "PxModRim picks", value: "picked"} ]
                Accessible.name: "Collection source"
                onActivated: root.applyFilters()
            }
            TagPicker {
                id: tag
                Layout.fillWidth: true
                Layout.maximumWidth: 230
                options: workshopPanel.tagOptions
                onSelectedTagChanged: root.applyFilters()
            }
            Item { Layout.fillWidth: true }
            PxComboBox {
                id: sort
                textRole: "text"
                valueRole: "value"
                model: [ {text: "Popular", value: "popular"}, {text: "Recently updated", value: "updated"}, {text: "Newest", value: "newest"}, {text: "Trending", value: "trending"}, {text: "Relevance", value: "relevance"} ]
                Accessible.name: "Catalog sort order"
                onActivated: root.applyFilters()
            }
        }
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
                    PxButton { text: "Download available mods"; enabled: workshopPanel.canDownloadAvailable; onClicked: workshopPanel.downloadAvailable() }
                    PxButton { text: "Cancel"; variant: "ghost"; onClicked: workshopPanel.dismissPlan() }
                }
            }
        }
        ScrollView {
            id: contentScroll
            objectName: "listingScroll"
            visible: !workshopPanel.hasDetail
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
                    visible: workshopPanel.configured
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
                        visible: workshopPanel.tab === "Collections" ? catalogCollections.hasMore : (workshopPanel.tab === "Mods" || workshopPanel.tab === "Installed") && catalogMods.hasMore
                        enabled: !workshopPanel.busy
                        onClicked: workshopPanel.loadMore()
                    }
                }
            }
        }
        ScrollView {
            id: detailScroll
            objectName: "detailScroll"
            visible: workshopPanel.hasDetail
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: detailScroll.availableWidth
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 1200
                    Layout.alignment: Qt.AlignHCenter
                    visible: workshopPanel.configured && workshopPanel.hasDetail
                    spacing: 16
                    PxButton {
                        text: "Back to " + workshopPanel.tab
                        iconName: "chevron-left"
                        Accessible.description: "Return to the listing with the same scroll position and filters"
                        onClicked: workshopPanel.back()
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 16
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 3
                            Layout.alignment: Qt.AlignTop
                            Layout.preferredHeight: Math.max(220, Math.min(340, width * 0.5))
                            color: Theme.elevate2
                            border.color: Theme.border
                            radius: Theme.radiusMd
                            clip: true
                            CatalogImage {
                                id: detailImage
                                anchors.fill: parent
                                previewUrl: root.detail.previewUrl || ""
                                widthStep: 128
                                heightStep: 64
                                fillMode: Image.PreserveAspectFit
                            }
                            Grid {
                                id: detailCollage
                                anchors.fill: parent
                                visible: !root.detail.previewUrl && (root.detail.collage || []).length > 0
                                columns: (root.detail.collage || []).length === 1 ? 1 : 2
                                readonly property int rowCount: (root.detail.collage || []).length > 2 ? 2 : 1
                                Repeater {
                                    model: detailCollage.visible ? root.detail.collage : []
                                    CatalogImage {
                                        required property string modelData
                                        width: detailCollage.width / detailCollage.columns
                                        height: detailCollage.height / detailCollage.rowCount
                                        previewUrl: modelData
                                    }
                                }
                            }
                            Image {
                                anchors.centerIn: parent
                                width: 48
                                height: 48
                                visible: detailImage.status !== Image.Ready && !detailCollage.visible
                                source: "image://icons/grid?color=" + encodeURIComponent(Theme.textDim)
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 2
                            Layout.alignment: Qt.AlignTop
                            spacing: 10
                            Flow {
                                Layout.fillWidth: true
                                spacing: 6
                                PxBadge { text: (root.detail.sourceLabel || "").toUpperCase(); textColor: Theme.textMuted }
                                PxBadge { visible: root.detail.state !== "missing"; text: "INSTALLED"; textColor: Theme.success }
                                PxBadge { visible: root.detail.state === "outdated"; text: "UPDATE AVAILABLE" }
                                PxBadge { visible: !!root.detail.incompatible; text: "INCOMPATIBLE"; textColor: Theme.warning }
                            }
                            Heading { Layout.fillWidth: true; text: root.detail.title || ""; font.pixelSize: 26; wrapMode: Text.Wrap }
                            Copy { text: "by " + (root.detail.author || "") }
                            Copy { Layout.fillWidth: true; text: "Supports " + (root.detail.versions || "") }
                            Copy { Layout.fillWidth: true; visible: !!root.detail.tags; text: root.detail.tags || "" }
                            Copy { Layout.fillWidth: true; visible: !!root.detail.incompatible; text: "Does not list your game version. Downloading does not make it compatible."; color: Theme.warning }
                            Copy { Layout.fillWidth: true; visible: !!root.detail.warning; text: root.detail.warning || ""; color: Theme.warning }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 8
                                PxButton {
                                    readonly property bool pack: root.detail.kind === "collection"
                                    visible: root.detail.state !== "missing"
                                    text: (root.detail.active ? "Deactivate" : "Activate") + (pack ? " all" : "")
                                    variant: root.detail.active ? "danger" : "success"
                                    onClicked: pack ? workshopPanel.toggleCollection() : workshopPanel.toggleActivation(root.detail.itemId)
                                }
                                PxButton {
                                    visible: root.detail.kind === "collection" && root.detail.state === "installed"
                                    text: "Activate only this pack"
                                    variant: "warning"
                                    ToolTip.text: "Activate this collection and its dependencies, deactivate every other Workshop mod"
                                    onClicked: workshopPanel.activateOnlyCollection()
                                }
                                PxButton {
                                    visible: root.detail.state !== "installed"
                                    text: root.detail.actionLabel || "Download"
                                    variant: root.detail.state === "outdated" ? "warning" : "primary"
                                    enabled: root.detail.state !== "installed" && !root.detail.queued
                                    onClicked: workshopPanel.downloadItem(root.detail.itemId, root.detail.kind)
                                }
                                PxButton { visible: !!root.detail.workshopUrl; text: "View on Steam"; variant: "ghost"; onClicked: workshopPanel.openLink(root.detail.workshopUrl) }
                            }
                            Copy { Layout.fillWidth: true; text: "Local mods · downloading does not enable or subscribe." }
                            RowLayout {
                                visible: root.detail.kind === "mod"
                                spacing: 8
                                Image {
                                    width: 16; height: 16
                                    sourceSize.width: 16; sourceSize.height: 16
                                    source: "image://icons/thumbs-up?color=" + encodeURIComponent(Theme.textMuted)
                                }
                                Copy { Layout.fillWidth: false; text: root.detail.votes || "Unrated" }
                                Copy { Layout.fillWidth: false; text: (root.detail.fileSize || "Unknown") }
                            }
                            RowLayout {
                                visible: !!root.detail.votesUp
                                spacing: 8
                                Image {
                                    width: 16; height: 16
                                    sourceSize.width: 16; sourceSize.height: 16
                                    source: "image://icons/thumbs-up?color=" + encodeURIComponent(Theme.textMuted)
                                }
                                Copy { Layout.fillWidth: false; text: root.detail.votesUp || ""; Accessible.name: text + " positive votes" }
                                Image {
                                    width: 16; height: 16
                                    sourceSize.width: 16; sourceSize.height: 16
                                    rotation: 180
                                    source: "image://icons/thumbs-up?color=" + encodeURIComponent(Theme.textMuted)
                                }
                                Copy { Layout.fillWidth: false; text: root.detail.votesDown || ""; Accessible.name: text + " negative votes" }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 16
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 3
                            Layout.alignment: Qt.AlignTop
                            implicitHeight: descColumn.implicitHeight + 32
                            color: Theme.elevate2
                            border.color: Theme.border
                            radius: Theme.radiusMd
                            ColumnLayout {
                                id: descColumn
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 10
                                Heading { text: "Description" }
                                ModDescription {
                                    Layout.fillWidth: true
                                    description: root.detail.description || ""
                                    onLinkActivated: link => workshopPanel.openLink(link)
                                }
                            }
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 2
                            Layout.alignment: Qt.AlignTop
                            implicitHeight: membersColumn.implicitHeight + 32
                            color: Theme.elevate2
                            border.color: Theme.border
                            radius: Theme.radiusMd
                            ColumnLayout {
                                id: membersColumn
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 8
                                Heading { text: root.detail.kind === "collection" ? "Collection members · " + catalogMembers.count : "Required mods · " + catalogMembers.count }
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
                                        required property bool queued
                                        Layout.fillWidth: true
                                        implicitHeight: memberRow.implicitHeight + 20
                                        radius: Theme.radiusSm
                                        color: Theme.elevate3
                                        RowLayout {
                                            id: memberRow
                                            anchors.fill: parent
                                            anchors.margins: 10
                                            spacing: 8
                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 2
                                                Copy { Layout.fillWidth: true; text: member.title; color: Theme.textMain; elide: Text.ElideRight; maximumLineCount: 1 }
                                                Copy { text: member.stateLabel + (member.incompatible ? " · incompatible" : ""); font.pixelSize: Theme.fontSizeSm; color: member.incompatible ? Theme.warning : Theme.textMuted }
                                            }
                                            PxButton { text: "Details"; variant: "ghost"; onClicked: workshopPanel.openItem(member.itemId, member.kind) }
                                            PxButton { visible: member.state !== "installed"; text: member.actionLabel; variant: member.state === "outdated" ? "warning" : "primary"; enabled: !member.queued; onClicked: workshopPanel.downloadItem(member.itemId, member.kind) }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    Component.onCompleted: workshopPanel.refresh()
}
