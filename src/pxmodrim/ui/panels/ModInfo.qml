import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components/controls"

Rectangle {
    id: root
    color: Theme.elevate2
    clip: true

    property var info: null
    property string description: ""
    property var startup: null
    readonly property bool startupKnown: startup !== null && startup !== undefined
    property bool descExpanded: false
    property bool workshopBusy: false
    property bool updatingThis: false
    property bool neededExpanded: false

    signal openFolder()
    signal openUrl()
    signal updateWorkshop()
    signal selectMod(string uuid)
    signal copyText(string text)
    signal openStartupDetails()
    signal descToggled(bool expanded)

    readonly property bool hasInfo: info !== null && info !== undefined
    readonly property var statusData: hasInfo ? info.status : null
    readonly property color levelColor: !statusData ? Theme.textMuted
        : statusData.level === "error" ? Theme.danger
        : statusData.level === "warning" ? Theme.warning
        : Theme.success
    readonly property color levelBg: !statusData ? Theme.elevate3
        : statusData.level === "error" ? Theme.dangerBg
        : statusData.level === "warning" ? Theme.warningBg
        : Theme.successBg

    function stateColor(state) {
        return state === "active" ? Theme.success : state === "inactive" ? Theme.warning : Theme.danger
    }
    function stateBg(state) {
        return state === "active" ? Theme.successBg : state === "inactive" ? Theme.warningBg : Theme.dangerBg
    }
    function stateMark(state) {
        return state === "active" ? "\u2713 " : state === "inactive" ? "\u25CF " : "\u2715 "
    }
    function stateHint(state) {
        return state === "active" ? "Active" : state === "inactive" ? "Installed but not active" : "Not installed"
    }

    component Card: Rectangle {
        default property alias content: column.data
        property color tint: "transparent"
        property color edge: Theme.border
        Layout.fillWidth: true
        implicitHeight: column.implicitHeight + 24
        radius: Theme.radiusMd
        color: Theme.elevate3
        border.width: 1
        border.color: edge

        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: parent.tint
        }

        ColumnLayout {
            id: column
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8
        }
    }

    // Badge that jumps to another mod when it has a uuid.
    component ModBadge: PxBadge {
        property string uuid: ""
        readonly property bool linked: uuid.length > 0

        HoverHandler {
            id: badgeHover
            enabled: parent.linked
            cursorShape: Qt.PointingHandCursor
        }
        TapHandler {
            enabled: parent.linked
            onTapped: root.selectMod(parent.uuid)
        }
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: Theme.textMain
            opacity: badgeHover.hovered ? 0.12 : 0
        }
    }

    component CardLabel: Text {
        Layout.fillWidth: true
        color: Theme.textDim
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        font.weight: Font.Bold
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.8
    }

    function resetScroll() {
        flick.contentY = 0
        neededExpanded = false
    }

    Flickable {
        id: flick
        objectName: "flick"
        anchors.fill: parent
        visible: root.hasInfo
        contentHeight: page.implicitHeight + 28
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: PxScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: page
            x: 14
            y: 14
            width: parent.width - 28
            spacing: 12

            // ── Status ──
            Card {
                tint: root.levelBg
                edge: root.statusData && root.statusData.level !== "ok" ? root.levelColor : Theme.border

                Flow {
                    Layout.fillWidth: true
                    spacing: 6

                    PxBadge {
                        text: root.statusData ? root.statusData.title : ""
                        textColor: root.levelColor
                        fillColor: root.levelBg
                    }
                    PxBadge {
                        visible: root.hasInfo && root.info.version.known
                        text: root.hasInfo && root.info.version.ok
                            ? "\u2713 Supports " + root.info.version.target
                            : "\u2715 No " + (root.hasInfo ? root.info.version.target : "") + " support"
                        textColor: root.hasInfo && root.info.version.ok ? Theme.success : Theme.warning
                        fillColor: root.hasInfo && root.info.version.ok ? Theme.successBg : Theme.warningBg
                    }
                    PxBadge {
                        visible: root.statusData !== null && root.statusData.obsolete
                        text: "Obsolete"
                        textColor: Theme.warning
                        fillColor: Theme.warningBg
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: !root.statusData ? ""
                        : root.statusData.active
                            ? "Active \u00B7 position #" + root.statusData.position + " of " + root.statusData.total
                            : "Inactive"
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeMd
                }

                Repeater {
                    model: root.statusData ? root.statusData.issues : []

                    delegate: ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            Layout.fillWidth: true
                            text: modelData.title
                            color: modelData.isError ? Theme.danger : Theme.warning
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeMd
                            font.weight: Font.Bold
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: modelData.detail.length > 0
                            text: modelData.detail
                            color: Theme.textMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            // ── Relationships ──
            Card {
                CardLabel { text: "Requires" }
                Text {
                    visible: root.hasInfo && root.info.needs.length === 0
                    text: "No dependencies"
                    color: Theme.textDim
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeMd
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: root.hasInfo && root.info.needs.length > 0

                    Repeater {
                        model: root.hasInfo ? root.info.needs : []
                        delegate: ModBadge {
                            uuid: modelData.uuid
                            text: root.stateMark(modelData.state) + modelData.name
                            textColor: root.stateColor(modelData.state)
                            fillColor: root.stateBg(modelData.state)
                            tooltip: root.stateHint(modelData.state) + (modelData.uuid.length > 0 ? " \u2014 click to open" : "")
                        }
                    }
                }

                CardLabel {
                    Layout.topMargin: 4
                    visible: root.hasInfo && root.info.conflicts.length > 0
                    text: "Conflicts with (active)"
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: root.hasInfo && root.info.conflicts.length > 0

                    Repeater {
                        model: root.hasInfo ? root.info.conflicts : []
                        delegate: PxBadge {
                            text: "\u2715 " + modelData
                            textColor: Theme.danger
                            fillColor: Theme.dangerBg
                        }
                    }
                }

                CardLabel {
                    Layout.topMargin: 4
                    visible: root.hasInfo && root.info.neededBy.length > 0
                    text: "Needed by"
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: root.hasInfo && root.info.neededBy.length > 0

                    Repeater {
                        model: !root.hasInfo ? [] : root.neededExpanded ? root.info.neededBy : root.info.neededBy.slice(0, 6)
                        delegate: ModBadge {
                            uuid: modelData.uuid
                            text: modelData.name
                            tooltip: "Click to open"
                            textColor: Theme.textMuted
                            fillColor: Theme.elevate4
                        }
                    }
                    Text {
                        visible: root.hasInfo && root.info.neededBy.length > 6
                        text: root.neededExpanded ? "Show less" : "+" + (root.hasInfo ? root.info.neededBy.length - 6 : 0) + " more"
                        color: Theme.primary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                        height: 20
                        verticalAlignment: Text.AlignVCenter

                        TapHandler { onTapped: root.neededExpanded = !root.neededExpanded }
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                    }
                }
            }

            // ── Description ──
            Card {
                visible: root.description.length > 0

                CardLabel { text: "Description" }
                Item {
                    id: descClip
                    FontMetrics { id: descMetrics; font: descText.font }
                    readonly property real collapsedHeight: root.description.indexOf("<img") >= 0 ? 220 : descMetrics.lineSpacing * 4
                    readonly property bool overflows: descText.implicitHeight > collapsedHeight + 1
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.descExpanded ? descText.implicitHeight
                        : Math.min(descText.implicitHeight, collapsedHeight)
                    clip: true

                    Rectangle {
                        z: 1
                        visible: descClip.overflows && !root.descExpanded
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 24
                        gradient: Gradient {
                            GradientStop { position: 0; color: "transparent" }
                            GradientStop { position: 1; color: Theme.elevate3 }
                        }
                    }

                    TextEdit {
                        id: descText
                        objectName: "descText"
                        width: parent.width
                        text: root.hasInfo
                            ? "<style>a { color: " + Theme.primary + "; }"
                              + " h1 { font-size: 18px; color: " + Theme.textMain + "; }"
                              + " h2 { font-size: 16px; color: " + Theme.textMain + "; }"
                              + " h3 { font-size: 14px; color: " + Theme.textMain + "; }"
                              + " blockquote { color: " + Theme.textDim + "; }</style>"
                              + root.description.replace(/__IMG_WIDTH__/g, Math.max(1, Math.floor(width)))
                            : ""
                        textFormat: TextEdit.RichText
                        readOnly: true
                        selectByMouse: true
                        selectedTextColor: Theme.onAccent
                        selectionColor: Theme.primary
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeMd
                        wrapMode: TextEdit.WordWrap
                        onLinkActivated: link => Qt.openUrlExternally(link)

                        HoverHandler {
                            cursorShape: descText.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.IBeamCursor
                        }
                    }
                }
                Text {
                    visible: descClip.overflows
                    text: root.descExpanded ? "Show less" : "Show more"
                    color: Theme.primary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm

                    TapHandler {
                        onTapped: {
                            root.descExpanded = !root.descExpanded
                            root.descToggled(root.descExpanded)
                        }
                    }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }
            }

            // ── Startup impact ──
            Card {
                id: startupCard
                readonly property bool ready: root.startupKnown && root.startup.available

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    CardLabel { text: "Startup impact" }
                    Text {
                        visible: startupCard.ready
                        text: "Full breakdown \u203A"
                        color: startupHover.hovered ? Theme.primaryHover : Theme.primary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: !startupCard.ready
                    text: !root.startupKnown ? "Loading\u2026" : (root.startup.message || "")
                    color: Theme.textDim
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeMd
                    wrapMode: Text.WordWrap
                }

                ColumnLayout {
                    visible: startupCard.ready
                    Layout.fillWidth: true
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            text: startupCard.ready ? root.startup.own : ""
                            color: startupCard.ready ? root.startup.color : Theme.textMain
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXl
                            font.weight: Font.Bold
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: startupCard.ready ? root.startup.rank : ""
                            color: Theme.textMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: startupCard.ready && root.startup.estimated
                            ? "Estimated extra game load time if you activate this mod"
                            : "Extra game load time caused by this mod"
                        color: Theme.textDim
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                        wrapMode: Text.WordWrap
                    }

                    Repeater {
                        model: startupCard.ready ? root.startup.rows : []

                        delegate: ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    elide: Text.ElideRight
                                    color: Theme.textMuted
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeSm
                                }
                                Text {
                                    text: modelData.value
                                    color: Theme.textMain
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeSm
                                }
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                height: 4
                                radius: 2
                                color: Theme.elevate4

                                Rectangle {
                                    width: parent.width * modelData.fraction
                                    height: parent.height
                                    radius: 2
                                    color: root.startup.color
                                }
                            }
                        }
                    }
                }

                HoverHandler {
                    id: startupHover
                    enabled: startupCard.ready
                    cursorShape: Qt.PointingHandCursor
                }
                TapHandler {
                    enabled: startupCard.ready
                    onTapped: root.openStartupDetails()
                }
            }

            // ── Details ──
            Card {
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    CardLabel { text: "Details" }
                    PxButton {
                        visible: root.hasInfo && root.info.canOpenFolder
                        implicitHeight: 24
                        implicitWidth: 24
                        variant: "ghost"
                        iconName: "folder"
                        ToolTip.text: "Open mod folder"
                        onClicked: root.openFolder()
                    }
                    PxButton {
                        visible: root.hasInfo && root.info.url.length > 0
                        implicitHeight: 24
                        implicitWidth: 24
                        variant: "ghost"
                        iconName: "link"
                        ToolTip.text: root.hasInfo ? "Open mod URL: " + root.info.url : "Open mod URL"
                        onClicked: root.openUrl()
                    }

                    PxButton {
                        visible: root.hasInfo && root.info.canUpdateWorkshop && !root.updatingThis
                        enabled: !root.workshopBusy
                        implicitHeight: 24
                        implicitWidth: 24
                        variant: "ghost"
                        iconName: "download"
                        ToolTip.text: root.workshopBusy ? "Workshop download in progress" : "Update from Steam Workshop"
                        onClicked: root.updateWorkshop()
                    }

                    BusyIndicator {
                        visible: root.updatingThis
                        running: visible
                        implicitHeight: 24
                        implicitWidth: 24
                        ToolTip.text: "Updating from Steam Workshop\u2026"
                        ToolTip.visible: hovered
                    }
                }

                Repeater {
                    model: root.hasInfo ? root.info.details : []

                    delegate: RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            Layout.preferredWidth: 80
                            Layout.alignment: Qt.AlignTop
                            text: modelData.label
                            color: Theme.textDim
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.value
                            color: Theme.textMain
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeMd
                            wrapMode: Text.WrapAnywhere
                        }
                        PxButton {
                            visible: modelData.copy === true
                            Layout.preferredHeight: 24
                            Layout.preferredWidth: 24
                            implicitHeight: 24
                            implicitWidth: 24
                            variant: "ghost"
                            iconName: "copy"
                            ToolTip.text: "Copy"
                            onClicked: root.copyText(modelData.value)
                        }
                    }
                }
            }
        }
    }
}
