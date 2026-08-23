import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtWebEngine
import QtWebChannel 1.15

Rectangle {
    id: root
    color: Theme.elevate0

    property bool _firstLoad: true
    property string _placeholderText: "Initializing Steam Workshop browser\u2026"
    property bool _showError: false
    property bool _loadedOnce: false
    property bool _pendingReload: false

    function _navigate() {
        if (!root._loadedOnce) {
            root._loadedOnce = true
            webView.url = "https://steamcommunity.com/workshop/browse/?appid=294100"
        } else if (root._pendingReload) {
            root._pendingReload = false
            webView.reload()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            color: Theme.elevate2

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 4
                anchors.rightMargin: 4
                spacing: 4

                Rectangle {
                    id: homeBtn
                    width: 28; height: 28
                    radius: Theme.radiusSm
                    color: homeMouse.containsMouse ? Theme.elevate3 : "transparent"

                    Image {
                        anchors.centerIn: parent
                        source: "image://icons/home?color=" + encodeURIComponent(Theme.textDim)
                        sourceSize.width: 14; sourceSize.height: 14
                    }

                    MouseArea {
                        id: homeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: steamWorkshopPanel.navigateHome()
                    }
                }

                Rectangle {
                    id: backBtn
                    width: 28; height: 28
                    radius: Theme.radiusSm
                    color: backMouse.containsMouse ? Theme.elevate3 : "transparent"
                    opacity: webView.canGoBack ? 1.0 : 0.4

                    Image {
                        anchors.centerIn: parent
                        source: "image://icons/chevron-left?color=" + encodeURIComponent(Theme.textDim)
                        sourceSize.width: 14; sourceSize.height: 14
                    }

                    MouseArea {
                        id: backMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: webView.canGoBack ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: {
                            if (webView.canGoBack)
                                webView.goBack()
                        }
                    }
                }

                Rectangle {
                    id: fwdBtn
                    width: 28; height: 28
                    radius: Theme.radiusSm
                    color: fwdMouse.containsMouse ? Theme.elevate3 : "transparent"
                    opacity: webView.canGoForward ? 1.0 : 0.4

                    Image {
                        anchors.centerIn: parent
                        source: "image://icons/chevron?color=" + encodeURIComponent(Theme.textDim)
                        sourceSize.width: 14; sourceSize.height: 14
                    }

                    MouseArea {
                        id: fwdMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: webView.canGoForward ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: {
                            if (webView.canGoForward)
                                webView.goForward()
                        }
                    }
                }

                Rectangle {
                    id: clearCacheBtn
                    width: 28; height: 28
                    radius: Theme.radiusSm
                    color: clearCacheMouse.containsMouse ? Theme.elevate3 : "transparent"

                    Image {
                        anchors.centerIn: parent
                        source: "image://icons/trash?color=" + encodeURIComponent(Theme.textDim)
                        sourceSize.width: 14; sourceSize.height: 14
                    }

                    MouseArea {
                        id: clearCacheMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        // Stale Steam SSR JS in the disk cache fails SRI and gets
                        // blocked; drop it and reload once the clear completes.
                        onClicked: {
                            root._pendingReload = true
                            webView.profile.clearHttpCache()
                        }
                    }
                }

                Rectangle {
                    id: reloadBtn
                    width: 28; height: 28
                    radius: Theme.radiusSm
                    color: reloadMouse.containsMouse ? Theme.elevate3 : "transparent"

                    Image {
                        anchors.centerIn: parent
                        source: "image://icons/refresh?color=" + encodeURIComponent(Theme.textDim)
                        sourceSize.width: 14; sourceSize.height: 14
                    }

                    MouseArea {
                        id: reloadMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: webView.reload()
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 24
                    radius: Theme.radiusSm
                    color: Theme.elevate0
                    border.width: 1
                    border.color: Theme.elevate3

                    TextInput {
                        id: urlInput
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        verticalAlignment: TextInput.AlignVCenter
                        color: Theme.textMain
                        font.pixelSize: Theme.fontSizeSm
                        text: webView.url.toString()
                        onAccepted: steamWorkshopPanel.navigateToUrl(text)
                    }
                }

                Text {
                    text: "\u27F3"
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSizeMd
                    visible: webView.loading
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            WebChannel {
                id: channel

                Component.onCompleted: {
                    if (_bridge) {
                        channel.registerObject("bridge", _bridge)
                    }
                }
            }

            WebEngineView {
                id: webView
                objectName: "workshopWeb"
                webChannel: channel
                anchors.fill: parent

                profile: WebEngineProfile {
                    id: steamProfile
                    storageName: "pxmodrim-steam"
                    httpCacheType: WebEngineProfile.DiskHttpCache
                    httpCacheMaximumSize: 52428800
                    offTheRecord: false
                }

                // Steam redeploys its SSR JS with new SRI hashes, so the persistent
                // disk cache can hold stale bytes that fail the integrity check and
                // get blocked. Clear it before the first navigation; url is set only
                // after clearHttpCacheCompleted to avoid navigating mid-clear.

                settings {
                    pluginsEnabled: false
                    pdfViewerEnabled: false
                    fullScreenSupportEnabled: false
                    hyperlinkAuditingEnabled: false
                    errorPageEnabled: false
                    localStorageEnabled: true
                }

                Component.onCompleted: {
                    var inj = WebEngine.script()
                    inj.name = "inject"
                    inj.sourceCode = _injectCode
                    inj.injectionPoint = WebEngineScript.DocumentCreation
                    inj.worldId = WebEngineScript.MainWorld
                    inj.runsOnSubFrames = false
                    webView.userScripts.insert(inj)

                    webView.profile.clearHttpCache()
                }

                Connections {
                    target: steamProfile

                    function onClearHttpCacheCompleted() {
                        root._navigate()
                    }
                }

                onLoadingChanged: function(loadRequest) {
                    if (loadRequest.status === WebEngineView.LoadStartedStatus) {
                        root._placeholderText = "Initializing Steam Workshop browser\u2026"
                        root._showError = false
                    } else if (loadRequest.status === WebEngineView.LoadSucceededStatus) {
                        root._firstLoad = false
                    } else if (loadRequest.status === WebEngineView.LoadFailedStatus) {
                        console.warn("[steam] load failed:", loadRequest.url, loadRequest.errorString,
                                     "domain:", loadRequest.errorDomain, "code:", loadRequest.errorCode)
                        root._placeholderText = "Failed to load Steam Workshop.\n\nCheck your network connection and try again."
                        root._showError = true
                    }
                }

                onLoadProgressChanged: {
                    if (root._firstLoad && webView.loadProgress < 100) {
                        root._placeholderText = "Initializing Steam Workshop browser\u2026 " + webView.loadProgress + "%"
                    }
                }
            }

            Rectangle {
                anchors.fill: parent
                color: Theme.elevate0
                visible: root._firstLoad || root._showError

                Text {
                    anchors.centerIn: parent
                    text: root._placeholderText
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSizeMd
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
