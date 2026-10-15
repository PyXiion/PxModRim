import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebEngine
import QtWebChannel
import "../../components/controls"

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

                PxButton {
                    id: homeBtn
                    variant: "ghost"
                    iconName: "home"
                    implicitWidth: 28
                    implicitHeight: 28
                    ToolTip.text: "Home"
                    onClicked: steamWorkshopPanel.navigateHome()
                }

                PxButton {
                    id: backBtn
                    variant: "ghost"
                    iconName: "chevron-left"
                    implicitWidth: 28
                    implicitHeight: 28
                    ToolTip.text: "Back"
                    enabled: webView.canGoBack
                    onClicked: webView.goBack()
                }

                PxButton {
                    id: fwdBtn
                    variant: "ghost"
                    iconName: "chevron"
                    implicitWidth: 28
                    implicitHeight: 28
                    ToolTip.text: "Forward"
                    enabled: webView.canGoForward
                    onClicked: webView.goForward()
                }

                PxButton {
                    id: clearCacheBtn
                    variant: "ghost"
                    iconName: "trash"
                    implicitWidth: 28
                    implicitHeight: 28
                    ToolTip.text: "Clear cache"
                    onClicked: {
                        // Stale Steam SSR JS in the disk cache fails SRI and gets
                        // blocked; drop it and reload once the clear completes.
                        root._pendingReload = true
                        webView.profile.clearHttpCache()
                    }
                }

                PxButton {
                    id: reloadBtn
                    variant: "ghost"
                    iconName: "refresh"
                    implicitWidth: 28
                    implicitHeight: 28
                    ToolTip.text: "Reload"
                    onClicked: webView.reload()
                }

                PxTextField {
                    id: urlInput
                    Layout.fillWidth: true
                    implicitHeight: 24
                    font.pixelSize: Theme.fontSizeSm
                    text: webView.url.toString()
                    onAccepted: steamWorkshopPanel.navigateToUrl(text)
                }

                PxProgressBar {
                    Layout.preferredWidth: 48
                    indeterminate: true
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
                        root._placeholderText = "Failed to load Steam Workshop"
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

                EmptyState {
                    anchors.fill: parent
                    iconName: root._showError ? "warning" : "steam"
                    title: root._placeholderText
                    detail: root._showError ? "Check your network connection and try again." : ""
                }
            }
        }
    }
}
