(function pxmodrimMain() {
    "use strict";

    // Prevent double initialization
    if (window.__pxmodrimInited) return;
    window.__pxmodrimInited = true;

    // Global shared state for Python integration
    window.__pxmodrim = {
        bridge: null,
        bridgeReady: false,
        installedIds: new Set(),
        checkedIds: new Set(),
        onStateChange: null,
    };

    const BadgeState = {
        INSTALLED: "installed",
        CHECKED: "checked",
        DEFAULT: "default",
    };

    let _installedIds = window.__pxmodrim.installedIds;
    let _checkedIds = window.__pxmodrim.checkedIds;
    let _bridge = null;
    let _bridgeReady = false;
    let _badgeRaf = null;

    const CSS_STYLES = `
        /* Instant-hide styles to prevent flicker */
        #global_header, header, .sharedfiles_item_page_header { display: none !important; }
        #footer [style*='--grid-area'] { display: none !important; }

        /* Grid badge styles */
        .rimsort-modstatus-badge {
            position: absolute;
            top: 5px;
            right: 5px;
            color: white;
            min-width: 28px;
            height: 28px;
            padding: 0 6px;
            border-radius: 6px;
            display: flex;
            align-items: center;
            justify-content: center;
            font-weight: bold;
            font-size: 16px;
            box-shadow: 0 0 4px black;
            user-select: none;
            transition: transform 0.1s ease, box-shadow 0.1s ease, opacity 0.2s ease, visibility 0.2s ease;
            z-index: 10;
            opacity: 0;
            visibility: hidden;
        }
        .rimsort-modstatus-badge.pressed { transform: scale(0.9); }
        .rimsort-mod-installed { background-color: #4CAF50; cursor: default; opacity: 1; visibility: visible; }
        .rimsort-mod-checked { background-color: #FFA500; cursor: pointer; opacity: 1; visibility: visible; }
        .rimsort-mod-default { background-color: #2196F3; cursor: pointer; }
        .rimsort-tile:hover .rimsort-modstatus-badge { opacity: 1; visibility: visible; }

        /* Detail page button styles */
        .rimsort-detail-btn {
            display: inline-flex;
            align-items: center;
            justify-content: center;
            gap: 6px;
            min-width: 130px;
            height: 36px;
            padding: 0 16px;
            border-radius: 6px;
            font-weight: bold;
            font-size: 14px;
            user-select: none;
            cursor: pointer;
            color: white;
            border: none;
            text-decoration: none;
            box-shadow: 0 0 4px rgba(0,0,0,0.3);
            transition: transform 0.1s ease, box-shadow 0.1s ease;
        }
        .rimsort-detail-btn.rimsort-mod-installed { background-color: #4CAF50; cursor: default; }
        .rimsort-detail-btn.rimsort-mod-checked { background-color: #FFA500; cursor: pointer; }
        .rimsort-detail-btn.rimsort-mod-default { background-color: #2196F3; cursor: pointer; }
        .rimsort-detail-btn.pressed { transform: scale(0.95); box-shadow: 0 0 2px rgba(0,0,0,0.5); }
    `;

    // ── Utility functions ─────────────────────────────────────────────────
    function getModId(link) {
        const m = link.href.match(/[?&]id=(\d+)/);
        return m ? m[1] : null;
    }

    function getModTitle(tile) {
        const links = tile.querySelectorAll('a[href*="sharedfiles/filedetails/?id="]');
        for (const link of links) {
            const text = link.textContent.trim();
            if (text) return text;
        }
        return "";
    }

    function findItemCard(link) {
        let el = link.parentElement;
        // No longer relies on document.body which may not be ready during early init
        while (el) {
            if (el.querySelector("img") && el.querySelectorAll('a[href*="sharedfiles/filedetails/?id="]').length === 2) {
                return el;
            }
            el = el.parentElement;
        }
        return null;
    }

    function setBadgeVisuals(badge, status) {
        badge.classList.remove("rimsort-mod-installed", "rimsort-mod-checked", "rimsort-mod-default");
        if (status === BadgeState.INSTALLED) {
            badge.title = "Already installed";
            badge.innerHTML = "&#10003;";
            badge.classList.add("rimsort-mod-installed");
        } else if (status === BadgeState.CHECKED) {
            badge.title = "Preparing to download";
            badge.innerHTML = "&#8722;";
            badge.classList.add("rimsort-mod-checked");
        } else {
            badge.title = "Add to list";
            badge.innerHTML = "+";
            badge.classList.add("rimsort-mod-default");
        }
    }

    function setDetailBtnVisuals(el, state) {
        el.className = "rimsort-detail-btn";
        if (state === "installed") {
            el.classList.add("rimsort-mod-installed");
            el.innerHTML = "Installed";
        } else if (state === "checked") {
            el.classList.add("rimsort-mod-checked");
            el.innerHTML = "\u2713 In Queue";
        } else {
            el.classList.add("rimsort-mod-default");
            el.innerHTML = "Add to Queue";
        }
    }

    function getDetailButtonState(modId) {
        if (_installedIds.has(modId)) return "installed";
        if (_checkedIds.has(modId)) return "checked";
        return "default";
    }

    // ── UI component management ──────────────────────────────────────────
    window.updateModBadge = function (modId, status) {
        const link = document.querySelector(`a[href*="sharedfiles/filedetails/?id=${modId}"]`);
        if (!link) return;

        const tile = findItemCard(link);
        if (!tile) return;

        let badge = tile.querySelector(".rimsort-modstatus-badge");
        if (!badge) {
            badge = document.createElement("div");
            badge.className = "rimsort-modstatus-badge";
            if (getComputedStyle(tile).position === "static") {
                tile.style.position = "relative";
            }
            tile.classList.add("rimsort-tile");
            tile.appendChild(badge);

            badge.addEventListener("click", function (e) {
                e.stopPropagation();
                e.preventDefault();
                if (!_bridge) return;

                badge.classList.add("pressed");
                setTimeout(() => badge.classList.remove("pressed"), 150);

                if (badge.classList.contains("rimsort-mod-default")) {
                    _checkedIds.add(modId);
                    setBadgeVisuals(badge, BadgeState.CHECKED);
                    _bridge.toggle_download_checked(modId, getModTitle(tile), true);
                } else if (badge.classList.contains("rimsort-mod-checked")) {
                    _checkedIds.delete(modId);
                    setBadgeVisuals(badge, BadgeState.DEFAULT);
                    _bridge.toggle_download_checked(modId, "", false);
                }
            });
        }
        setBadgeVisuals(badge, status);
    };

    window.updateAllModBadges = function () {
        const links = document.querySelectorAll('a[href*="sharedfiles/filedetails/?id="]');
        let badged = 0;
        links.forEach((link) => {
            const modId = getModId(link);
            if (!modId) return;

            if (_installedIds.has(modId)) {
                window.updateModBadge(modId, BadgeState.INSTALLED);
                badged++;
            } else if (_checkedIds.has(modId)) {
                window.updateModBadge(modId, BadgeState.CHECKED);
            } else {
                window.updateModBadge(modId, BadgeState.DEFAULT);
            }
        });
        console.log(`[pxmodrim] Badges updated: links=${links.length} installed=${badged}`);
    };

    function createDetailButton() {
        const oldBtn = document.getElementById("SubscribeItemBtn");
        if (!oldBtn) return false;

        const modId = (window.location.href.match(/[?&]id=(\d+)/) || [])[1];
        if (!modId) return false;

        // If the button is already custom, just update its appearance
        let btn = document.getElementById("pxmodrim-subscribe-btn");
        if (btn) {
            setDetailBtnVisuals(btn, getDetailButtonState(modId));
            return true;
        }

        const h1 = document.querySelector(".game_area_purchase_game h1");
        const title = h1 ? h1.textContent.replace(/Subscribe to download\s*/i, "").trim() : "";

        btn = document.createElement("a");
        btn.id = "pxmodrim-subscribe-btn";

        btn.addEventListener("click", function (e) {
            e.preventDefault();
            e.stopPropagation();

            if (!_bridge || btn.classList.contains("rimsort-mod-installed")) return;

            btn.classList.add("pressed");
            requestAnimationFrame(() => btn.classList.remove("pressed"));

            if (btn.classList.contains("rimsort-mod-checked")) {
                _checkedIds.delete(modId);
                setDetailBtnVisuals(btn, "default");
                _bridge.toggle_download_checked(modId, "", false);
            } else {
                _checkedIds.add(modId);
                setDetailBtnVisuals(btn, "checked");
                _bridge.toggle_download_checked(modId, title, true);
            }
        });

        setDetailBtnVisuals(btn, getDetailButtonState(modId));
        oldBtn.replaceWith(btn);
        return true;
    }

    // ── Python integration points (external calls) ────────────────────────
    window.__pxmSetInstalled = function (modIds) {
        _installedIds.clear();
        (modIds || []).forEach(id => _installedIds.add(id));
        if (_bridgeReady) {
            window.updateAllModBadges();
            window.__pxmodrim.onStateChange?.();
        }
    };

    window.__pxmUncheckMod = function (modId) {
        _checkedIds.delete(modId);
        if (_installedIds.has(modId)) {
            window.updateModBadge(modId, BadgeState.INSTALLED);
        } else {
            window.updateModBadge(modId, BadgeState.DEFAULT);
        }
        window.__pxmodrim.onStateChange?.();
    };

    window.__pxmClearChecked = function () {
        _checkedIds.clear();
        window.updateAllModBadges();
        window.__pxmodrim.onStateChange?.();
    };

    // Render detail page button on state changes from Python
    window.__pxmodrim.onStateChange = function () {
        const modId = (window.location.href.match(/[?&]id=(\d+)/) || [])[1];
        const btn = document.getElementById("pxmodrim-subscribe-btn");
        if (btn && modId) setDetailBtnVisuals(btn, getDetailButtonState(modId));
    };

    // ── Async QWebChannel bridge setup ──────────────────────────────────
    let _wcRetries = 0;
    const _WC_MAX_RETRIES = 100;
    function setupWebChannel() {
        if (typeof QWebChannel !== "undefined" && typeof qt !== "undefined" && qt.webChannelTransport) {
            new QWebChannel(qt.webChannelTransport, function (channel) {
                _bridge = channel.objects.bridge;
                _bridgeReady = true;
                window.__pxmodrim.bridge = _bridge;
                window.__pxmodrim.bridgeReady = true;
                console.log("[pxmodrim] QWebChannel connection established");

                let initInstalled = false;
                let initChecked = false;

                const renderInitialState = () => {
                    if (initInstalled && initChecked) {
                        window.updateAllModBadges();
                        createDetailButton();
                    }
                };

                _bridge.get_installed_ids(function (ids) {
                    _installedIds.clear();
                    (ids || []).forEach(id => _installedIds.add(id));
                    initInstalled = true;
                    renderInitialState();
                });

                _bridge.get_checked_ids(function (ids) {
                    _checkedIds.clear();
                    (ids || []).forEach(id => _checkedIds.add(id));
                    initChecked = true;
                    renderInitialState();
                });
            });
        } else {
            _wcRetries++;
            if (_wcRetries >= _WC_MAX_RETRIES) {
                console.warn("[pxmodrim] QWebChannel not available after " + _WC_MAX_RETRIES + " retries; giving up.");
                return;
            }
            // Qt environment object may load slightly later
            setTimeout(setupWebChannel, 30);
        }
    }

    // ── Steam DOM cleanup ────────────────────────────────────────────────
    function runDomCleanup() {
        document.querySelector("#global_header")?.remove();
        document.querySelector("header")?.remove();
        document.querySelector(".sharedfiles_item_page_header")?.remove();

        const h1 = document.querySelector("h1");
        if (h1 && h1.textContent.trim() === "RimWorld") {
            let t = h1;
            for (let i = 0; i < 5 && t.parentElement; i++) { t = t.parentElement; }
            t?.remove();
        }

        const footer = document.getElementById("footer");
        footer?.querySelectorAll("[style*='--grid-area']").forEach((el) => {
            const area = el.style.getPropertyValue("--grid-area");
            if (area && area !== "main") el.parentElement?.removeChild(el);
        });
    }

    function scheduleBadgeUpdate() {
        if (_badgeRaf !== null) return;
        _badgeRaf = requestAnimationFrame(() => {
            _badgeRaf = null;
            if (_bridgeReady) window.updateAllModBadges();
        });
    }

    // ── Safe MutationObserver init after root is ready ─────────────────────
    function initObservers() {
        // Watches for dynamic content injection (infinite scroll + button appearance)
        const mainObserver = new MutationObserver((mutations) => {
            let shouldUpdateBadges = false;
            let checkDetailBtn = false;

            for (const m of mutations) {
                for (const node of m.addedNodes) {
                    if (node.nodeType !== Node.ELEMENT_NODE) continue;

                    if (node.querySelector('a[href*="sharedfiles/filedetails/?id="]') && node.querySelector("img")) {
                        shouldUpdateBadges = true;
                    }
                    if (node.id === "SubscribeItemBtn" || node.querySelector("#SubscribeItemBtn")) {
                        checkDetailBtn = true;
                    }
                }
            }

            if (shouldUpdateBadges) scheduleBadgeUpdate();
            if (checkDetailBtn) createDetailButton();

            // Run DOM cleanup on every mutation
            runDomCleanup();
        });

        mainObserver.observe(document.documentElement, {
            childList: true,
            subtree: true,
        });

        // Initial cleanup pass for already-rendered DOM
        runDomCleanup();
        createDetailButton();
    }

    function injectStyles() {
        const style = document.createElement("style");
        style.id = "pxmodrim-styles";
        style.textContent = CSS_STYLES;
        document.documentElement.appendChild(style);
    }

    function waitForRoot() {
        const rootWaiter = new MutationObserver(() => {
            if (document.documentElement) {
                rootWaiter.disconnect();
                startScript();
            }
        });
        rootWaiter.observe(document, { childList: true });
    }

    function startScript() {
        injectStyles();
        console.log("[pxmodrim] Base styles injected at Document Creation. Starting lifecycle...");
        setupWebChannel();
        initObservers();
    }

    if (document.documentElement) {
        startScript();
    } else {
        waitForRoot();
    }
})();