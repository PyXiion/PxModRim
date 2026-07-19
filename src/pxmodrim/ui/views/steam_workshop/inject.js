(function () {
    "use strict";

    if (window.__pxmodrimInited) {
        return;
    }
    window.__pxmodrimInited = true;

    // Expose shared state for detail page IIFE
    window.__pxmodrim = {
        bridge: null,
        bridgeReady: false,
        installedIds: new Set(),
        checkedIds: new Set(),
        onStateChange: null,
    };

    // ── CSS injection ──────────────────────────────────────────────────
    const style = document.createElement("style");
    style.textContent = `
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

        .rimsort-modstatus-badge.pressed {
            transform: scale(0.9);
        }

        .rimsort-mod-installed {
            background-color: #4CAF50;
            cursor: default;
        }

        .rimsort-mod-checked {
            background-color: #FFA500;
            cursor: pointer;
            opacity: 1;
            visibility: visible;
        }

        .rimsort-mod-default {
            background-color: #2196F3;
            cursor: pointer;
        }

        .rimsort-tile:hover .rimsort-modstatus-badge {
            opacity: 1;
            visibility: visible;
        }

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

        .rimsort-detail-btn.rimsort-mod-installed {
            background-color: #4CAF50;
            cursor: default;
        }

        .rimsort-detail-btn.rimsort-mod-checked {
            background-color: #FFA500;
            cursor: pointer;
        }

        .rimsort-detail-btn.rimsort-mod-default {
            background-color: #2196F3;
            cursor: pointer;
        }

        .rimsort-detail-btn.pressed {
            transform: scale(0.95);
            box-shadow: 0 0 2px rgba(0,0,0,0.5);
        }
    `;
    document.head.appendChild(style);

    // ── State (closure, no globals) ────────────────────────────────────
    const BadgeState = {
        INSTALLED: "installed",
        CHECKED: "checked",
        DEFAULT: "default",
    };

    let _installedIds = window.__pxmodrim.installedIds;
    let _checkedIds = window.__pxmodrim.checkedIds;
    let _bridge = null;
    let _bridgeReady = false;

    // ── Push endpoints (Python → JS) ───────────────────────────────────
    window.__pxmSetInstalled = function (modIds) {
        _installedIds.clear();
        (modIds || []).forEach(function (id) { _installedIds.add(id); });
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

    // ── Pure visual updater ─────────────────────────────────────────────
    function setBadge(badge, status) {
        badge.classList.remove(
            "rimsort-mod-installed",
            "rimsort-mod-checked",
            "rimsort-mod-default"
        );
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

    // ── Helpers ─────────────────────────────────────────────────────────
    function getModId(link) {
        const m = link.href.match(/[?&]id=(\d+)/);
        return m ? m[1] : null;
    }

    function getModTitle(tile) {
        const links = tile.querySelectorAll(
            'a[href*="sharedfiles/filedetails/?id="]'
        );
        for (const link of links) {
            const text = link.textContent.trim();
            if (text) {
                return text;
            }
        }
        return "";
    }

    function findItemCard(link) {
        let el = link.parentElement;
        while (el && el !== document.body) {
            if (
                el.querySelector("img") &&
                el.querySelectorAll(
                    'a[href*="sharedfiles/filedetails/?id="]'
                ).length === 2
            ) {
                return el;
            }
            el = el.parentElement;
        }
        return null;
    }

    // ── Badge rendering ────────────────────────────────────────────────
    window.updateModBadge = function (modId, status) {
        const link = document.querySelector(
            `a[href*="sharedfiles/filedetails/?id=${modId}"]`
        );
        if (!link) {
            return;
        }

        const tile = findItemCard(link);
        if (!tile) {
            return;
        }

        let badge = tile.querySelector(".rimsort-modstatus-badge");

        if (!badge) {
            badge = document.createElement("div");
            badge.className = "rimsort-modstatus-badge";
            if (getComputedStyle(tile).position === "static") {
                tile.style.position = "relative";
            }
            tile.classList.add("rimsort-tile");
            tile.appendChild(badge);

            // Badge click: optimistic DOM update + fire-and-forget
            badge.addEventListener("click", function (e) {
                e.stopPropagation();
                e.preventDefault();
                if (!_bridge) return;

                badge.classList.add("pressed");
                setTimeout(function () {
                    badge.classList.remove("pressed");
                }, 150);

                if (badge.classList.contains("rimsort-mod-default")) {
                    _checkedIds.add(modId);
                    setBadge(badge, BadgeState.CHECKED);
                    _bridge.toggle_download_checked(
                        modId,
                        getModTitle(tile),
                        true
                    );
                } else if (badge.classList.contains("rimsort-mod-checked")) {
                    _checkedIds.delete(modId);
                    setBadge(badge, BadgeState.DEFAULT);
                    _bridge.toggle_download_checked(modId, "", false);
                }
            });
        }

        setBadge(badge, status);
    };

    window.updateAllModBadges = function () {
        const links = document.querySelectorAll(
            'a[href*="sharedfiles/filedetails/?id="]'
        );
        let badged = 0;
        links.forEach(function (link) {
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
        console.log(
            "[pxmodrim] updateAllModBadges: links=" +
                links.length +
                " installed=" +
                badged
        );
    };

    // ── QWebChannel bridge ─────────────────────────────────────────────
    if (typeof QWebChannel !== "undefined") {
        new QWebChannel(qt.webChannelTransport, function (channel) {
            _bridge = channel.objects.bridge;
            _bridgeReady = true;
            window.__pxmodrim.bridge = _bridge;
            window.__pxmodrim.bridgeReady = true;
            console.log(
                "[pxmodrim] QWebChannel ready, fetching initial state"
            );

            // Pull initial state from Python, render when both arrive
            let initInstalled = false;
            let initChecked = false;

            function tryInitRender() {
                if (initInstalled && initChecked) {
                    window.updateAllModBadges();
                    window.__pxmodrim.onStateChange?.();
                }
            }

            _bridge.get_installed_ids(function (ids) {
                _installedIds.clear();
                (ids || []).forEach(function (id) { _installedIds.add(id); });
                initInstalled = true;
                tryInitRender();
            });

            _bridge.get_checked_ids(function (ids) {
                _checkedIds.clear();
                (ids || []).forEach(function (id) { _checkedIds.add(id); });
                initChecked = true;
                tryInitRender();
            });
        });
    } else {
        console.warn("[pxmodrim] QWebChannel not available");
    }

    // ── MutationObserver for infinite scroll ────────────────────────────
    let _badgeRaf = null;
    function _scheduleBadgeUpdate() {
        if (_badgeRaf !== null) return;
        _badgeRaf = requestAnimationFrame(function () {
            _badgeRaf = null;
            if (_bridgeReady) {
                window.updateAllModBadges();
            }
        });
    }

    const _badgeObserver = new MutationObserver(function (mutations) {
        for (const m of mutations) {
            for (const node of m.addedNodes) {
                if (
                    node.nodeType === Node.ELEMENT_NODE &&
                    node.querySelector(
                        'a[href*="sharedfiles/filedetails/?id="]'
                    ) &&
                    node.querySelector("img")
                ) {
                    _scheduleBadgeUpdate();
                    return;
                }
            }
        }
    });
    _badgeObserver.observe(document.documentElement, {
        childList: true,
        subtree: true,
    });

    console.log("[pxmodrim] Badge script loaded, QWebChannel pending");
})();

// ── Page cleanup (CSS hides instantly, observer removes permanently) ────
(function () {
    // Inject CSS once — hides target elements before first paint
    if (!document.getElementById("pxmodrim-hide-style")) {
        var s = document.createElement("style");
        s.id = "pxmodrim-hide-style";
        s.textContent = [
            "#global_header, header, .sharedfiles_item_page_header { display: none !important; }",
            "#footer [style*='--grid-area'] { display: none !important; }",
        ].join(" ");
        document.documentElement.appendChild(s);
    }

    // Permanently remove hidden elements from DOM (not time-critical)
    var _cleanupTimer = null;
    function doCleanup() {
        var gh = document.querySelector("#global_header");
        if (gh) gh.remove();

        var h = document.querySelector("header");
        if (h) h.remove();

        var h1 = document.querySelector("h1");
        if (h1 && h1.textContent.trim() === "RimWorld") {
            var t = h1;
            for (var i = 0; i < 5 && t.parentElement; i++) { t = t.parentElement; }
            if (t) t.remove();
        }

        var ah = document.querySelector(".sharedfiles_item_page_header");
        if (ah) ah.remove();

        var footer = document.getElementById("footer");
        if (footer) {
            footer.querySelectorAll("[style*='--grid-area']").forEach(function (el) {
                var area = el.style.getPropertyValue("--grid-area");
                if (area && area !== "main") el.parentElement?.removeChild(el);
            });
        }
    }

    var obs = new MutationObserver(function () {
        if (_cleanupTimer) return;
        _cleanupTimer = requestAnimationFrame(function () {
            _cleanupTimer = null;
            doCleanup();
        });
    });
    obs.observe(document.documentElement, { childList: true, subtree: true });
})();

// ── Detail page subscribe button (separate IIFE, re-runs on SPA nav) ──────
(function () {
    "use strict";

    var modId = (window.location.href.match(/[?&]id=(\d+)/) || [])[1];
    if (!modId) return;

    function getModTitle() {
        var h1 = document.querySelector(".game_area_purchase_game h1");
        if (!h1) return "";
        return h1.textContent.replace(/Subscribe to download\s*/i, "").trim();
    }

    function getButtonState() {
        var pxm = window.__pxmodrim;
        if (!pxm) return "default";
        if (pxm.installedIds.has(modId)) return "installed";
        if (pxm.checkedIds.has(modId)) return "checked";
        return "default";
    }

    function setDetailBtn(el, state) {
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

    function createDetailButton() {
        var oldBtn = document.getElementById("SubscribeItemBtn");
        if (!oldBtn) return false;

        var title = getModTitle();

        var btn = document.createElement("a");
        btn.id = "pxmodrim-subscribe-btn";

        btn.addEventListener("click", function (e) {
            e.preventDefault();
            e.stopPropagation();

            if (!window.__pxmodrim || !window.__pxmodrim.bridge) return;
            if (btn.classList.contains("rimsort-mod-installed")) return;

            btn.classList.add("pressed");
            requestAnimationFrame(function () { btn.classList.remove("pressed"); });

            if (btn.classList.contains("rimsort-mod-checked")) {
                window.__pxmodrim.checkedIds.delete(modId);
                setDetailBtn(btn, "default");
                window.__pxmodrim.bridge.toggle_download_checked(modId, "", false);
            } else {
                window.__pxmodrim.checkedIds.add(modId);
                setDetailBtn(btn, "checked");
                window.__pxmodrim.bridge.toggle_download_checked(modId, title, true);
            }
        });

        setDetailBtn(btn, getButtonState());
        oldBtn.replaceWith(btn);
        return true;
    }

    // Register state change callback for Python→JS pushes
    if (window.__pxmodrim) {
        window.__pxmodrim.onStateChange = function () {
            var btn = document.getElementById("pxmodrim-subscribe-btn");
            if (btn) setDetailBtn(btn, getButtonState());
        };
    }

    // Try immediately (full page load or injection after bridge ready)
    if (!createDetailButton()) {
        // Button not in DOM yet — wait for it (SPA nav / lazy render)
        var _detailTimer = null;
        var _detailObserver = new MutationObserver(function () {
            if (document.getElementById("SubscribeItemBtn")) {
                if (_detailTimer) return;
                _detailTimer = requestAnimationFrame(function () {
                    _detailTimer = null;
                    createDetailButton();
                });
            }
        });
        _detailObserver.observe(document.documentElement, {
            childList: true,
            subtree: true,
        });
    }
})();
