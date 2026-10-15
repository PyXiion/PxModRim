import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components/controls"

Rectangle {
    id: root
    color: Theme.elevate1
    clip: true

    property var sourceData: null

    readonly property bool hasData: sourceData !== null && sourceData !== undefined

    property string view: "mods"
    property string filterText: ""
    property bool activeOnly: false
    property bool slowOnly: false
    property string sortBy: "time"
    property bool includeBase: true

    // JS objects are not observable; bump the revision to re-evaluate bindings.
    property var expandedMods: ({})
    property var expandedPhases: ({})
    property var showAllPhases: ({})
    property int expandRev: 0

    readonly property real slowThreshold: 3.0
    readonly property int phasePreview: 6

    readonly property var visibleMods: {
        if (!hasData)
            return []
        const q = filterText.trim().toLowerCase()
        const rows = sourceData.mods.filter(m =>
            (!activeOnly || m.active)
            && (!slowOnly || m.seconds >= slowThreshold)
            && (q.length === 0 || m.name.toLowerCase().indexOf(q) !== -1))
        if (sortBy === "name")
            rows.sort((a, b) => a.name.localeCompare(b.name))
        return rows
    }

    readonly property var phases: hasData
        ? (includeBase ? sourceData.phases_with_base : sourceData.phases_mods_only)
        : []

    onSourceDataChanged: {
        if (!sourceData)
            return
        const key = sourceData.selected_key
        const selectedRow = sourceData.mods.find(m => m.key === key)
        expandedMods = key ? { [key]: true } : {}
        expandedPhases = {}
        showAllPhases = {}
        expandRev++
        filterText = ""
        activeOnly = sourceData.active_count > 0 && (!selectedRow || selectedRow.active)
        Qt.callLater(scrollToMod, key)
    }

    function toggle(map, key) {
        map[key] = !map[key]
        expandRev++
    }

    function scrollToMod(key) {
        if (!key)
            return
        const idx = visibleMods.findIndex(m => m.key === key)
        if (idx >= 0)
            modsList.positionViewAtIndex(idx, ListView.Center)
    }

    function openMod(key) {
        const row = sourceData.mods.find(m => m.key === key)
        if (!row)
            return
        filterText = ""
        slowOnly = false
        if (!row.active)
            activeOnly = false
        expandedMods[key] = true
        expandRev++
        view = "mods"
        Qt.callLater(scrollToMod, key)
    }

    component Chip: Rectangle {
        id: chip
        property string text
        property bool checked: false
        signal clicked()

        implicitHeight: 28
        implicitWidth: chipLabel.implicitWidth + 24
        radius: Theme.radiusPill
        color: checked ? Qt.alpha(Theme.primary, 0.14)
             : chipMouse.containsMouse ? Theme.elevate4 : Theme.elevate3

        Text {
            id: chipLabel
            anchors.centerIn: parent
            text: chip.text
            color: chip.checked ? Theme.primary : Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            font.weight: Font.Medium
        }
        MouseArea {
            id: chipMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: chip.clicked()
        }
    }

    component Caption: Text {
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        font.weight: Font.DemiBold
        font.letterSpacing: 0.6
        font.capitalization: Font.AllUppercase
        color: Theme.textDim
    }

    component Bar: Rectangle {
        property real fraction: 0
        property color fill: Theme.primary
        implicitHeight: 8
        radius: height / 2
        color: Theme.elevate4
        Rectangle {
            width: Math.max(parent.fraction > 0 ? parent.height : 0, parent.width * Math.min(1, parent.fraction))
            height: parent.height
            radius: parent.radius
            color: parent.fill
        }
    }

    component Divider: Rectangle {
        Layout.fillWidth: true
        implicitHeight: 1
        color: Theme.border
    }

    component Card: Rectangle {
        id: card
        property string title
        property string value
        property string detail
        Layout.fillWidth: true
        implicitHeight: cardCol.implicitHeight + 24
        radius: Theme.radiusLg
        color: Theme.elevate2
        border.width: 1
        border.color: Theme.border
        ColumnLayout {
            id: cardCol
            anchors.fill: parent
            anchors.margins: 12
            spacing: 4
            Caption { text: card.title }
            Text {
                Layout.fillWidth: true
                text: card.value
                color: Theme.textMain
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXl
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: card.detail
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                wrapMode: Text.Wrap
            }
        }
    }

    EmptyState {
        anchors.fill: parent
        visible: !hasData
        iconName: "clock"
        title: "No startup impact data"
        detail: "Launch the game with the loading progress mod to record it."
    }

    ColumnLayout {
        anchors.fill: parent
        visible: hasData
        spacing: 0

        // -- Summary --
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 20
            Layout.bottomMargin: 16
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Text {
                    text: hasData ? sourceData.last_launch : ""
                    color: Theme.textMain
                    font.family: Theme.fontFamily
                    font.pixelSize: 28
                    font.weight: Font.Bold
                }
                ColumnLayout {
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 0
                    Text {
                        text: "last launch"
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeMd
                    }
                    Text {
                        visible: text.length > 0
                        text: hasData && sourceData.estimated
                              ? "≈ " + sourceData.estimated + " estimated for your current mod list" : ""
                        color: Theme.textDim
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm
                    }
                }
                Item { Layout.fillWidth: true }
                Text {
                    visible: text.length > 0
                    text: hasData && sourceData.timestamp ? "report from " + sourceData.timestamp : ""
                    color: Theme.textDim
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                }
            }

            Item {
                id: summaryBar
                Layout.fillWidth: true
                implicitHeight: 10
                readonly property var parts: hasData ? sourceData.summary : []
                Row {
                    anchors.fill: parent
                    spacing: 2
                    Repeater {
                        model: summaryBar.parts
                        delegate: Rectangle {
                            width: Math.max(2, modelData.fraction * (summaryBar.width - 2 * (summaryBar.parts.length - 1)))
                            height: summaryBar.height
                            radius: 3
                            color: modelData.color
                            opacity: modelData.parallel ? 0.45 : 1
                        }
                    }
                }
            }

            Flow {
                Layout.fillWidth: true
                spacing: 18
                Repeater {
                    model: summaryBar.parts
                    delegate: Row {
                        spacing: 6
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 10; height: 10; radius: 2
                            color: modelData.color
                            opacity: modelData.parallel ? 0.45 : 1
                        }
                        Text {
                            text: modelData.label + "  " + modelData.value
                            color: Theme.textMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                        }
                    }
                }
            }
        }

        Divider {}

        // -- Toolbar --
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: 10
            Layout.bottomMargin: 10
            spacing: 8

            Chip { text: "Mods"; checked: view === "mods"; onClicked: view = "mods" }
            Chip { text: "Phases"; checked: view === "phases"; onClicked: view = "phases" }
            Item { Layout.fillWidth: true }

            PxTextField {
                visible: view === "mods"
                Layout.preferredWidth: 220
                implicitHeight: 28
                placeholderText: "Filter mods…"
                text: root.filterText
                onTextEdited: root.filterText = text
            }
            Chip {
                visible: view === "mods" && hasData && sourceData.active_count > 0
                text: "Active only"
                checked: activeOnly
                onClicked: activeOnly = !activeOnly
            }
            Chip {
                visible: view === "mods"
                text: "Slow > 3 s"
                checked: slowOnly
                onClicked: slowOnly = !slowOnly
            }
            Chip {
                visible: view === "mods"
                text: sortBy === "time" ? "Sort: Time" : "Sort: Name"
                onClicked: sortBy = sortBy === "time" ? "name" : "time"
            }
            Chip {
                visible: view === "phases"
                text: "Include base game"
                checked: includeBase
                onClicked: includeBase = !includeBase
            }
        }

        Divider {}

        // -- Mods view --
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: view === "mods"
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20 + Theme.scrollbarWidth
                Layout.topMargin: 10
                Layout.bottomMargin: 4
                spacing: 12
                Item { Layout.preferredWidth: 28 }
                Caption { Layout.fillWidth: true; text: "Mod" }
                Caption { Layout.preferredWidth: modsList.width * 0.36; text: "Phase breakdown" }
                Caption { Layout.preferredWidth: 72; horizontalAlignment: Text.AlignRight; text: "Time" }
            }

            ListView {
                id: modsList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: root.visibleMods
                ScrollBar.vertical: PxScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Item {
                    id: modRow
                    required property var modelData
                    readonly property bool expanded: root.expandRev >= 0 && root.expandedMods[modelData.key] === true

                    width: ListView.view.width
                    height: rowContent.implicitHeight

                    ColumnLayout {
                        id: rowContent
                        width: parent.width
                        spacing: 0

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.leftMargin: 12
                            Layout.rightMargin: 12 + Theme.scrollbarWidth
                            implicitHeight: 36
                            radius: Theme.radiusMd
                            color: modRow.modelData.selected ? Qt.alpha(Theme.primary, 0.10)
                                 : rowMouse.containsMouse ? Theme.elevate3 : "transparent"

                            Rectangle {
                                visible: modRow.modelData.selected
                                width: 3; height: parent.height - 12
                                anchors.verticalCenter: parent.verticalCenter
                                radius: 2
                                color: Theme.primary
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: 12
                                opacity: modRow.modelData.active || !root.hasData || root.sourceData.active_count === 0 ? 1 : 0.55

                                Text {
                                    Layout.preferredWidth: 28
                                    horizontalAlignment: Text.AlignRight
                                    text: modRow.modelData.rank
                                    color: Theme.textDim
                                    font.family: Theme.fontMono
                                    font.pixelSize: Theme.fontSizeSm
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: modRow.modelData.name
                                    color: Theme.textMain
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeMd
                                    elide: Text.ElideRight
                                }
                                Rectangle {
                                    Layout.preferredWidth: modsList.width * 0.36
                                    implicitHeight: 8
                                    radius: 4
                                    color: Theme.elevate4
                                    clip: true
                                    Row {
                                        height: parent.height
                                        Repeater {
                                            model: modRow.modelData.segments
                                            delegate: Rectangle {
                                                required property var modelData
                                                width: modelData.fraction * parent.parent.width
                                                height: parent.height
                                                color: modelData.color
                                            }
                                        }
                                    }
                                }
                                Text {
                                    Layout.preferredWidth: 72
                                    horizontalAlignment: Text.AlignRight
                                    text: modRow.modelData.time
                                    color: modRow.modelData.color
                                    font.family: Theme.fontMono
                                    font.pixelSize: Theme.fontSizeMd
                                    font.weight: Font.Bold
                                }
                            }

                            MouseArea {
                                id: rowMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.toggle(root.expandedMods, modRow.modelData.key)
                            }
                        }

                        Rectangle {
                            visible: modRow.expanded
                            Layout.fillWidth: true
                            Layout.leftMargin: 60
                            Layout.rightMargin: 12 + Theme.scrollbarWidth
                            Layout.topMargin: 2
                            Layout.bottomMargin: 8
                            implicitHeight: visible ? detailCol.implicitHeight + 24 : 0
                            radius: Theme.radiusMd
                            color: Theme.elevate0
                            border.width: 1
                            border.color: Theme.border

                            ColumnLayout {
                                id: detailCol
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 6

                                Caption {
                                    Layout.fillWidth: true
                                    text: "Where " + modRow.modelData.name + " spends its time"
                                    elide: Text.ElideRight
                                }
                                Repeater {
                                    model: modRow.expanded ? modRow.modelData.phases : []
                                    delegate: RowLayout {
                                        id: phaseLine
                                        required property var modelData
                                        Layout.fillWidth: true
                                        spacing: 10
                                        Rectangle { width: 10; height: 10; radius: 2; color: phaseLine.modelData.color }
                                        Text {
                                            Layout.preferredWidth: 150
                                            text: phaseLine.modelData.label
                                            color: Theme.textMain
                                            font.family: Theme.fontFamily
                                            font.pixelSize: Theme.fontSizeMd
                                            HoverHandler { id: phaseHover }
                                            PxToolTip {
                                                visible: phaseHover.hovered && phaseLine.modelData.tooltip.length > 0
                                                text: phaseLine.modelData.tooltip
                                            }
                                        }
                                        Bar {
                                            Layout.fillWidth: true
                                            implicitHeight: 6
                                            fraction: phaseLine.modelData.fraction
                                            fill: phaseLine.modelData.color
                                        }
                                        Text {
                                            Layout.preferredWidth: 72
                                            horizontalAlignment: Text.AlignRight
                                            text: phaseLine.modelData.value
                                            color: Theme.textMain
                                            font.family: Theme.fontMono
                                            font.pixelSize: Theme.fontSizeSm
                                        }
                                    }
                                }
                                Text {
                                    visible: modRow.modelData.phases.length === 0
                                    text: "Under 1 ms in every phase"
                                    color: Theme.textMuted
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeSm
                                }
                                Text {
                                    visible: modRow.modelData.off_thread.length > 0
                                    text: "Plus " + modRow.modelData.off_thread + " off the main thread"
                                    color: Theme.textMuted
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeSm
                                }
                            }
                        }
                    }
                }

                EmptyState {
                    anchors.centerIn: parent
                    width: parent.width
                    visible: modsList.count === 0
                    iconName: "search"
                    title: "No mods match"
                }
            }
        }

        // -- Phases view --
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: view === "phases"
            spacing: 0

            ListView {
                id: phasesList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: root.phases
                topMargin: 8
                bottomMargin: 8
                ScrollBar.vertical: PxScrollBar { policy: ScrollBar.AsNeeded }

                header: RowLayout {
                    width: ListView.view.width
                    spacing: 12
                    Item { Layout.preferredWidth: 32 }
                    Caption { Layout.fillWidth: true; text: "Phase" }
                    Caption { Layout.preferredWidth: 72; horizontalAlignment: Text.AlignRight; text: "Time" }
                    Caption { Layout.preferredWidth: 44; Layout.rightMargin: 20 + Theme.scrollbarWidth; horizontalAlignment: Text.AlignRight; text: "Share" }
                }

                delegate: Item {
                    id: phaseRow
                    required property var modelData
                    readonly property bool expanded: root.expandRev >= 0 && root.expandedPhases[modelData.label] === true
                    readonly property bool showAll: root.expandRev >= 0 && root.showAllPhases[modelData.label] === true
                    readonly property var shownMods: showAll ? modelData.mods : modelData.mods.slice(0, root.phasePreview)

                    width: ListView.view.width
                    height: phaseContent.implicitHeight

                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12 + Theme.scrollbarWidth
                        anchors.bottomMargin: 2
                        radius: Theme.radiusMd
                        color: phaseRow.expanded ? Theme.elevate2 : "transparent"
                    }

                    ColumnLayout {
                        id: phaseContent
                        width: parent.width
                        spacing: 0

                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 38

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 20
                                anchors.rightMargin: 20 + Theme.scrollbarWidth
                                spacing: 12

                                Text {
                                    Layout.preferredWidth: 10
                                    text: phaseRow.expanded ? "▾" : "▸"
                                    color: Theme.textDim
                                    font.pixelSize: Theme.fontSizeSm
                                }
                                Rectangle { width: 10; height: 10; radius: 2; color: phaseRow.modelData.color }
                                Text {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 120
                                    text: phaseRow.modelData.label
                                    color: Theme.textMain
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeMd
                                    elide: Text.ElideRight
                                }
                                Bar {
                                    Layout.preferredWidth: phasesList.width * 0.28
                                    fraction: phaseRow.modelData.fraction
                                    fill: phaseRow.modelData.color
                                }
                                Text {
                                    Layout.preferredWidth: 72
                                    horizontalAlignment: Text.AlignRight
                                    text: phaseRow.modelData.value
                                    color: Theme.textMain
                                    font.family: Theme.fontMono
                                    font.pixelSize: Theme.fontSizeMd
                                    font.weight: Font.Bold
                                }
                                Text {
                                    Layout.preferredWidth: 44
                                    horizontalAlignment: Text.AlignRight
                                    text: phaseRow.modelData.share
                                    color: Theme.textMuted
                                    font.family: Theme.fontMono
                                    font.pixelSize: Theme.fontSizeSm
                                }
                            }

                            MouseArea {
                                id: phaseMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.toggle(root.expandedPhases, phaseRow.modelData.label)
                            }
                            PxToolTip {
                                visible: phaseMouse.containsMouse && phaseRow.modelData.tooltip.length > 0
                                text: phaseRow.modelData.tooltip
                            }
                        }

                        ColumnLayout {
                            visible: phaseRow.expanded
                            Layout.fillWidth: true
                            Layout.leftMargin: 54
                            Layout.rightMargin: 20 + Theme.scrollbarWidth
                            Layout.bottomMargin: 10
                            spacing: 2

                            Repeater {
                                model: phaseRow.expanded ? phaseRow.shownMods : []
                                delegate: RowLayout {
                                    id: contributor
                                    required property var modelData
                                    Layout.fillWidth: true
                                    implicitHeight: 26
                                    spacing: 12

                                    Text {
                                        Layout.fillWidth: true
                                        text: contributor.modelData.name
                                        color: contributor.modelData.selected ? Theme.primary
                                             : contributor.modelData.base ? Theme.textDim
                                             : nameMouse.containsMouse ? Theme.textMain : Theme.textMuted
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontSizeSm
                                        font.italic: contributor.modelData.base
                                        font.weight: contributor.modelData.selected ? Font.DemiBold : Font.Normal
                                        font.underline: nameMouse.containsMouse && !contributor.modelData.base
                                        elide: Text.ElideRight
                                        MouseArea {
                                            id: nameMouse
                                            anchors.fill: parent
                                            enabled: !contributor.modelData.base
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: root.openMod(contributor.modelData.key)
                                        }
                                    }
                                    Bar {
                                        Layout.preferredWidth: phasesList.width * 0.22
                                        implicitHeight: 5
                                        fraction: contributor.modelData.fraction
                                        fill: Qt.alpha(phaseRow.modelData.color, 0.7)
                                    }
                                    Text {
                                        Layout.preferredWidth: 72 + 44 + 12
                                        horizontalAlignment: Text.AlignRight
                                        text: contributor.modelData.value
                                        color: Theme.textMuted
                                        font.family: Theme.fontMono
                                        font.pixelSize: Theme.fontSizeSm
                                    }
                                }
                            }

                            Text {
                                readonly property int hidden: phaseRow.modelData.mods.length - root.phasePreview
                                visible: hidden > 0
                                text: phaseRow.showAll ? "Show fewer" : "+ " + hidden + " more mods…"
                                color: moreMouse.containsMouse ? Theme.primary : Theme.textDim
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeSm
                                topPadding: 4
                                MouseArea {
                                    id: moreMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.toggle(root.showAllPhases, phaseRow.modelData.label)
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillHeight: true
                implicitWidth: 1
                color: Theme.border
            }

            ColumnLayout {
                Layout.fillWidth: false
                Layout.preferredWidth: 260
                Layout.maximumWidth: 260
                Layout.fillHeight: true
                Layout.margins: 16
                spacing: 12

                Card {
                    title: "Last launch"
                    value: hasData ? sourceData.last_launch : ""
                    detail: hasData ? "Base game " + sourceData.base_value + " · Mods " + sourceData.mods_value : ""
                }
                Card {
                    visible: root.phases.length > 0
                    title: "Costliest phase"
                    value: visible ? root.phases[0].label : ""
                    detail: visible ? root.phases[0].value + " · " + root.phases[0].share + " of measured time" : ""
                }
                Card {
                    visible: hasData && sourceData.mods.length > 0
                    title: "Costliest mod"
                    value: visible ? sourceData.mods[0].name : ""
                    detail: visible ? sourceData.mods[0].time
                                      + (sourceData.mods[0].dominant ? " · mostly " + sourceData.mods[0].dominant.toLowerCase() : "")
                                    : ""
                }
                Card {
                    title: "Off main thread"
                    value: hasData ? sourceData.off_value : ""
                    detail: "Runs in parallel with loading"
                }
                Item { Layout.fillHeight: true }
                Text {
                    Layout.fillWidth: true
                    text: "Hover a phase to see the game's own step names. Click a mod to jump to it."
                    color: Theme.textDim
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    wrapMode: Text.Wrap
                }
            }
        }

        Divider {}

        // -- Footer --
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: 8
            Layout.bottomMargin: 8
            spacing: 16

            Flow {
                Layout.fillWidth: true
                spacing: 14
                Repeater {
                    model: hasData ? sourceData.legend : []
                    delegate: Row {
                        required property var modelData
                        spacing: 6
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 10; height: 10; radius: 2
                            color: parent.modelData.color
                        }
                        Text {
                            text: parent.modelData.label
                            color: Theme.textMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }
                    }
                }
            }
            Text {
                visible: view === "mods"
                text: hasData ? root.visibleMods.length + " of " + sourceData.mods.length + " mods shown" : ""
                color: Theme.textDim
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }
    }
}
