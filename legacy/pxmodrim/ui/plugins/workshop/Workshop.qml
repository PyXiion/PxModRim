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
        property int columns: Math.max(1, Math.floor(width / 230))
        Layout.fillWidth: true
        Layout.preferredHeight: Math.ceil(count / columns) * cellHeight
        cellWidth: width / columns
        cellHeight: 312
        interactive: false
        clip: true
        delegate: CatalogCard {
            width: GridView.view.cellWidth - 12
            height: GridView.view.cellHeight - 12
        }
    }
    component CollectionGrid: GridView {
        property int columns: Math.max(1, Math.floor(width / 400))
        Layout.fillWidth: true
        Layout.preferredHeight: Math.ceil(count / columns) * cellHeight
        cellWidth: width / columns
        cellHeight: 162
        interactive: false
        clip: true
        delegate: CollectionBanner {
            width: GridView.view.cellWidth - 12
            height: GridView.view.cellHeight - 12
        }
    }
    component ListingHeader: Item {
        height: pageHeading.implicitHeight + 16
        Heading { id: pageHeading; width: parent.width; text: workshopPanel.tab }
    }
    component ListingFooter: ColumnLayout {
        required property var listing
        height: implicitHeight
        spacing: 12
        Copy {
            visible: !workshopPanel.busy && !workshopPanel.error && listing.count === 0
            text: workshopPanel.tab === "Installed" ? "No installed Workshop mods were found."
                : workshopPanel.tab === "Favourites" ? (workshopPanel.searchQuery ? "No favourite matches this search." : "No favourites yet. Star a collection to keep it here.")
                : "No matching items. Try another search or filter."
        }
        PxButton {
            Layout.alignment: Qt.AlignHCenter
            text: "Load more"
            visible: listing.hasMore
            enabled: !workshopPanel.busy
            onClicked: workshopPanel.loadMore()
        }
        Item { implicitHeight: 12 }
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
                    model: ["Discover", "Mods", "Collections", "Favourites", "Installed"]
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
            objectName: "discoverScroll"
            visible: !workshopPanel.hasDetail && (!workshopPanel.configured || workshopPanel.tab === "Discover")
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
                    Heading { text: "Discover your next colony" }
                    Copy { text: "Explore Steam mods and collections alongside PxModRim picks. Downloads include required dependencies, without activating mods." }
                    RowLayout {
                        Heading { text: "Collections" }
                        PxButton { text: "Browse all"; variant: "ghost"; onClicked: workshopPanel.selectTab("Collections") }
                    }
                    CollectionGrid { model: visible ? catalogCollections : null }
                    RowLayout {
                        Heading { text: "Popular mods" }
                        PxButton { text: "Browse all"; variant: "ghost"; onClicked: workshopPanel.selectTab("Mods") }
                    }
                    CatalogGrid { model: visible ? catalogMods : null }
                }
            }
        }
        GridView {
            id: listingGrid
            objectName: "listingScroll"
            readonly property bool listingTab: workshopPanel.configured && workshopPanel.tab === "Mods"
            property int columns: Math.max(1, Math.floor(width / 230))
            visible: !workshopPanel.hasDetail && listingTab
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            cellWidth: width / columns
            cellHeight: 312
            model: listingTab ? catalogMods : null
            ScrollBar.vertical: PxScrollBar {}
            header: ListingHeader { width: listingGrid.width }
            footer: ListingFooter { width: listingGrid.width; listing: catalogMods }
            delegate: CatalogCard {
                width: GridView.view.cellWidth - 12
                height: GridView.view.cellHeight - 12
            }
        }
        GridView {
            id: collectionsGrid
            objectName: "collectionsScroll"
            readonly property bool listingTab: workshopPanel.configured && (workshopPanel.tab === "Collections" || workshopPanel.tab === "Favourites")
            property int columns: Math.max(1, Math.floor(width / 400))
            visible: !workshopPanel.hasDetail && listingTab
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            cellWidth: width / columns
            cellHeight: 162
            model: listingTab ? catalogCollections : null
            ScrollBar.vertical: PxScrollBar {}
            header: ListingHeader { width: collectionsGrid.width }
            footer: ListingFooter { width: collectionsGrid.width; listing: catalogCollections }
            delegate: CollectionBanner {
                width: GridView.view.cellWidth - 12
                height: GridView.view.cellHeight - 12
            }
        }
        GridView {
            id: installedGrid
            objectName: "installedScroll"
            readonly property bool active: workshopPanel.configured && workshopPanel.tab === "Installed"
            property int columns: Math.max(1, Math.floor(width / 340))
            visible: !workshopPanel.hasDetail && active
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            cellWidth: width / columns
            cellHeight: 62
            model: active ? catalogMods : null
            ScrollBar.vertical: PxScrollBar {}
            header: ListingHeader { width: installedGrid.width }
            footer: ListingFooter { width: installedGrid.width; listing: catalogMods }
            delegate: ModRow {
                width: GridView.view.cellWidth - 8
                height: GridView.view.cellHeight - 8
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
            function jumpToMembers() {
                const flickable = contentItem
                const target = membersHeading.mapToItem(flickable.contentItem, 0, 0).y
                flickable.contentY = Math.max(0, Math.min(target, flickable.contentHeight - flickable.height))
            }
            ColumnLayout {
                width: detailScroll.availableWidth
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 1200
                    Layout.alignment: Qt.AlignHCenter
                    visible: workshopPanel.configured && workshopPanel.hasDetail
                    spacing: 16
                    PxButton {
                        text: workshopPanel.backLabel
                        iconName: "chevron-left"
                        Accessible.description: "Return to the previous page, keeping the listing's scroll position and filters"
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
                            Copy {
                                objectName: "detailTags"
                                Layout.fillWidth: true
                                text: [root.detail.tags, root.detail.versions === "Not specified" ? "" : root.detail.versions].filter(s => !!s).join(" · ")
                                visible: !!text
                            }
                            Copy { Layout.fillWidth: true; visible: !!root.detail.incompatible; text: "Does not list your game version."; color: Theme.warning }
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
                                PxButton {
                                    objectName: "detailFavourite"
                                    visible: root.detail.kind === "collection"
                                    text: root.detail.favourite ? "Favourited" : "Favourite"
                                    variant: "ghost"
                                    iconName: root.detail.favourite ? "star-filled" : "star"
                                    iconColor: root.detail.favourite ? Theme.warning : "transparent"
                                    onClicked: workshopPanel.toggleFavourite(root.detail.itemId)
                                }
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
                                visible: root.detail.kind === "collection"
                                spacing: 8
                                Copy { Layout.fillWidth: false; text: root.detail.memberCount + " mods" }
                                Copy { Layout.fillWidth: false; visible: root.detail.fileSize !== "Unknown"; text: "· " + root.detail.fileSize + " total" }
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
                    GridLayout {
                        id: detailBody
                        readonly property bool pack: root.detail.kind === "collection"
                        Layout.fillWidth: true
                        columns: pack ? 1 : 2
                        columnSpacing: 16
                        rowSpacing: 16
                        ColumnLayout {
                            id: descColumn
                            objectName: "detailDescription"
                            Layout.row: 0
                            Layout.column: 0
                            Layout.fillWidth: true
                            Layout.preferredWidth: 3
                            Layout.alignment: Qt.AlignTop
                            spacing: 10
                            readonly property bool tall: descriptionText.implicitHeight > detailScroll.availableHeight
                            RowLayout {
                                Layout.fillWidth: true
                                Heading { text: "Description" }
                                PxButton {
                                    objectName: "jumpToMembers"
                                    visible: descColumn.tall && catalogMembers.count > 0
                                    text: detailBody.pack ? "Jump to mods" : "Jump to required mods"
                                    variant: "ghost"
                                    font.pixelSize: Theme.fontSizeSm
                                    onClicked: detailScroll.jumpToMembers()
                                }
                            }
                            ModDescription {
                                id: descriptionText
                                Layout.fillWidth: true
                                description: root.detail.description || ""
                                onLinkActivated: link => workshopPanel.openLink(link)
                            }
                        }
                        ColumnLayout {
                            objectName: "detailMembers"
                            Layout.row: detailBody.pack ? 1 : 0
                            Layout.column: detailBody.pack ? 0 : 1
                            Layout.fillWidth: true
                            Layout.preferredWidth: 2
                            Layout.alignment: Qt.AlignTop
                            spacing: 8
                            RowLayout {
                                id: membersHeading
                                objectName: "membersHeading"
                                Layout.fillWidth: true
                                Heading { text: (detailBody.pack ? "Collection members · " : "Required mods · ") + catalogMembers.count }
                                PxButton {
                                    objectName: "detailBackToTop"
                                    visible: detailScroll.contentItem.contentY > 0
                                    text: "Back to top"
                                    variant: "ghost"
                                    font.pixelSize: Theme.fontSizeSm
                                    onClicked: detailScroll.contentItem.contentY = 0
                                }
                            }
                            Copy { visible: catalogMembers.count === 0; text: "None listed." }
                            GridLayout {
                                objectName: "membersGrid"
                                Layout.fillWidth: true
                                columns: detailBody.pack ? Math.max(1, Math.floor(Math.min(1200, detailScroll.availableWidth) / 340)) : 1
                                columnSpacing: 8
                                rowSpacing: 8
                                Repeater {
                                    model: catalogMembers
                                    ModRow {
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: 1
                                        Layout.preferredHeight: 54
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
