import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components/controls"

Rectangle {
    id: root
    objectName: "settingsRoot"

    color: Theme.elevate2

    property int tab: 0
    readonly property var initial: settings.initial

    function values() {
        return {
            game: game.text.trim(),
            local: local.text.trim(),
            workshop: workshop.text.trim(),
            config: configFolder.text.trim(),
            compact: compact.checked,
            autoUpdateHours: autoUpdate.currentValue,
            useAltIds: useAltIds.checked,
            checkMissing: checkMissing.checked,
            useCommunity: useCommunity.checked
        }
    }

    Connections {
        target: settings
        function onPathPicked(key, path) {
            const fields = { game: game, local: local, workshop: workshop, config: configFolder }
            if (fields[key])
                fields[key].text = path
            if (key === "game" && local.text === "") {
                const candidate = settings.localCandidate(path)
                if (candidate)
                    local.text = candidate
            }
        }
    }

    component Group: Rectangle {
        default property alias content: body.data
        property string title: ""

        Layout.fillWidth: true
        implicitHeight: body.implicitHeight + 52
        radius: Theme.radiusMd
        color: Theme.elevate1
        border.width: 1
        border.color: Theme.border

        Text {
            x: 16
            y: 12
            text: parent.title
            color: Theme.textDim
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            font.weight: Font.Bold
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 0.5
        }

        ColumnLayout {
            id: body
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 16
            anchors.topMargin: 36
            spacing: 10
        }
    }

    component PathRow: RowLayout {
        property string label: ""
        property string key: ""
        property alias text: field.text

        Layout.fillWidth: true
        spacing: 8

        Text {
            Layout.preferredWidth: 150
            text: parent.label
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeMd
        }
        PxTextField {
            id: field
            Layout.fillWidth: true
            monospace: true
            Accessible.name: parent.label
        }
        PxButton {
            text: "Browse…"
            onClicked: settings.browse(parent.key)
        }
    }

    component Tab: Item {
        id: tabItem
        property string label: ""
        property int index: 0
        readonly property bool current: root.tab === index

        implicitWidth: tabText.implicitWidth + 32
        implicitHeight: 40

        Text {
            id: tabText
            anchors.centerIn: parent
            text: tabItem.label
            color: tabItem.current ? Theme.primary : tabMouse.containsMouse ? Theme.textMain : Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeMd
            font.weight: tabItem.current ? Font.Medium : Font.Normal
        }
        Rectangle {
            visible: tabItem.current
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 2
            color: Theme.primary
        }
        MouseArea {
            id: tabMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            Accessible.role: Accessible.PageTab
            Accessible.name: tabItem.label
            onClicked: root.tab = tabItem.index
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 41
            color: "transparent"

            Row {
                x: 8
                Tab { label: "General"; index: 0 }
                Tab { label: "Sorting"; index: 1 }
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.tab

            ScrollView {
                id: generalScroll
                clip: true
                contentWidth: availableWidth
                ScrollBar.vertical: PxScrollBar {}

                ColumnLayout {
                    width: generalScroll.availableWidth - 32
                    x: 16
                    y: 16
                    spacing: 12

                    Group {
                        title: "Folders"
                        PathRow { id: game; label: "Game folder"; key: "game"; text: root.initial.game }
                        PathRow { id: local; label: "Local mods folder"; key: "local"; text: root.initial.local }
                        PathRow { id: workshop; label: "Workshop mods folder"; key: "workshop"; text: root.initial.workshop }
                        PathRow { id: configFolder; label: "Config folder"; key: "config"; text: root.initial.config }
                        PxButton {
                            text: "Auto-detect"
                            onClicked: settings.autoDetect()
                        }
                    }

                    Group {
                        title: "Appearance"
                        PxCheckBox {
                            id: compact
                            text: "Compact mod list"
                            checked: root.initial.compact
                        }
                    }

                    Group {
                        title: "Steam Workshop"
                        visible: root.initial.steamAvailable
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Text {
                                Layout.preferredWidth: 150
                                text: "Auto-update mods"
                                color: Theme.textMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeMd
                            }
                            PxComboBox {
                                id: autoUpdate
                                Layout.preferredWidth: 180
                                Accessible.name: "Auto-update Workshop mods"
                                textRole: "text"
                                valueRole: "value"
                                model: [
                                    { text: "Off", value: 0 },
                                    { text: "Every 6 hours", value: 6 },
                                    { text: "Every 12 hours", value: 12 },
                                    { text: "Daily", value: 24 },
                                    { text: "Every 3 days", value: 72 },
                                    { text: "Weekly", value: 168 }
                                ]
                                Component.onCompleted: currentIndex = Math.max(0, indexOfValue(root.initial.autoUpdateHours))
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "Re-syncs mods downloaded by PxModRim while the app is running."
                            wrapMode: Text.Wrap
                            color: Theme.textDim
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }
                    }
                }
            }

            ScrollView {
                id: sortingScroll
                clip: true
                contentWidth: availableWidth
                ScrollBar.vertical: PxScrollBar {}

                ColumnLayout {
                    width: sortingScroll.availableWidth - 32
                    x: 16
                    y: 16
                    spacing: 12

                    Group {
                        title: "Sorting options"
                        PxCheckBox {
                            id: useAltIds
                            text: "Use alternative package IDs"
                            checked: root.initial.useAltIds
                        }
                        PxCheckBox {
                            id: checkMissing
                            text: "Check missing dependencies"
                            checked: root.initial.checkMissing
                        }
                        PxCheckBox {
                            id: useCommunity
                            text: "Use community rules database"
                            checked: root.initial.useCommunity
                        }
                    }

                    Group {
                        title: "Community rules database"
                        Text {
                            Layout.fillWidth: true
                            text: settings.communityStatus
                            wrapMode: Text.Wrap
                            color: Theme.textMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeMd
                        }
                        PxButton {
                            text: "Download or update"
                            enabled: !settings.communityBusy
                            onClicked: settings.downloadCommunityRules()
                        }
                    }

                    Group {
                        title: "Startup impact cache"
                        Text {
                            Layout.fillWidth: true
                            text: settings.cacheStatus
                            wrapMode: Text.Wrap
                            color: Theme.textMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeMd
                        }
                        PxButton {
                            text: "Clear cache"
                            enabled: settings.cacheAvailable && !settings.cacheBusy
                            onClicked: settings.clearCache()
                        }
                    }
                }
            }
        }

        PxDialogFooter {
            Layout.fillWidth: true
            PxButton {
                text: "Cancel"
                onClicked: settings.cancel()
            }
            PxButton {
                objectName: "settingsSave"
                text: "Save"
                variant: "primary"
                onClicked: settings.save(root.values())
            }
        }
    }
}
