import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components/controls"

Rectangle {
    id: root
    color: Theme.elevate2
    clip: true

    property var info: null
    property var startup: null
    property bool descExpanded: false

    signal openFolder()
    signal openUrl()
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
    }

    Flickable {
        id: flick
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
                        delegate: PxBadge {
                            text: root.stateMark(modelData.state) + modelData.name
                            textColor: root.stateColor(modelData.state)
                            fillColor: root.stateBg(modelData.state)
                            tooltip: root.stateHint(modelData.state)
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
                        model: root.hasInfo ? root.info.neededBy : []
                        delegate: PxBadge {
                            text: modelData
                            textColor: Theme.textMuted
                            fillColor: Theme.elevate4
                        }
                    }
                    Text {
                        visible: root.hasInfo && root.info.neededByMore > 0
                        text: "+" + (root.hasInfo ? root.info.neededByMore : 0) + " more"
                        color: Theme.textDim
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                        height: 20
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            // ── Description ──
            Card {
                visible: root.hasInfo && root.info.description.length > 0

                CardLabel { text: "Description" }
                Item {
                    id: descClip
                    readonly property real collapsedHeight: descText.lineHeight * 4
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

                    Text {
                        id: descText
                        width: parent.width
                        text: root.hasInfo ? root.info.description : ""
                        textFormat: Text.RichText
                        color: Theme.textMuted
                        linkColor: Theme.primary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeMd
                        wrapMode: Text.WordWrap
                        lineHeightMode: Text.FixedHeight
                        lineHeight: 19
                        onLinkActivated: link => Qt.openUrlExternally(link)
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
                visible: root.startup !== null

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    CardLabel { text: "Startup impact" }
                    Text {
                        visible: root.startup !== null
                        text: "Details \u203A"
                        color: startupHover.hovered ? Theme.primaryHover : Theme.primary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }
                }

                RowLayout {
                    visible: root.startup !== null
                    Layout.fillWidth: true
                    spacing: 12

                    Row {
                        spacing: 3
                        Layout.alignment: Qt.AlignBottom

                        Repeater {
                            model: root.startup ? root.startup.spark : []
                            delegate: Rectangle {
                                width: 6
                                height: Math.max(3, 30 * modelData)
                                anchors.bottom: parent.bottom
                                radius: 1
                                color: root.startup ? root.startup.color : Theme.primary
                            }
                        }
                    }
                    Item { Layout.fillWidth: true }
                    ColumnLayout {
                        spacing: 0
                        Text {
                            Layout.alignment: Qt.AlignRight
                            text: root.startup ? root.startup.own : ""
                            color: Theme.textMain
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeLg
                            font.weight: Font.Bold
                        }
                        Text {
                            Layout.alignment: Qt.AlignRight
                            text: root.startup ? root.startup.rank : ""
                            color: Theme.textMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                        }
                    }
                }

                HoverHandler {
                    id: startupHover
                    enabled: root.startup !== null
                    cursorShape: Qt.PointingHandCursor
                }
                TapHandler {
                    enabled: root.startup !== null
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
                        ToolTip.text: "Open mod URL"
                        onClicked: root.openUrl()
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
