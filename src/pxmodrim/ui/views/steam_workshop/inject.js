(function pxmodrimMain() {
    "use strict";
    console.log("[pxmodrim] SCRIPT LOADED");

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
    let _bridgeReadyPromise = null;
    let _badgeRaf = null;
    let _activeRoute = null;
    let _depCache = new Map();
    let _depFetching = new Set();
    let _depFetchAborters = new Map();
    const DEPTH_MAX = 3;
    const DEP_SECTION_ID = "pxmodrim-deps";
    const DEP_SOLO_LINK_ID = "pxmodrim-solo-link";
    let _isResolvingDeps = false;
    let _loadingDeps = false;
    let _detailInjectedUrl = null;
    let _depSectionInjectedUrl = null;
    let _createDepSectionRunning = false;

    const CSS_STYLES = `
        /* Instant-hide styles to prevent flicker */
        #global_header, header, .sharedfiles_item_page_header { display: none !important; }
        #footer [style*='--grid-area'] { display: none !important; }
        /* Hide original Steam Subscribe button to avoid React hydration conflicts */
        #SubscribeItemBtn { display: none !important; }

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

        /* Dependency tree styles */
        .rimsort-deps-section {
            margin-top: 12px;
            padding-top: 12px;
            border-top: 1px solid rgba(255,255,255,0.1);
            font-size: 13px;
        }
        .rimsort-deps-header {
            font-weight: bold;
            margin-bottom: 8px;
            color: #ccc;
        }
        .rimsort-dep-node {
            display: flex;
            align-items: center;
            gap: 6px;
            padding: 4px 0;
            white-space: nowrap;
        }
        .rimsort-dep-expand {
            display: inline-flex;
            align-items: center;
            justify-content: center;
            width: 18px;
            height: 18px;
            cursor: pointer;
            color: #888;
            user-select: none;
            transition: transform 0.15s ease, color 0.15s ease;
        }
        .rimsort-dep-expand.expanded { transform: rotate(90deg); color: #ccc; }
        .rimsort-dep-expand:empty { visibility: hidden; }
        .rimsort-dep-badge {
            display: inline-flex;
            align-items: center;
            justify-content: center;
            min-width: 18px;
            height: 18px;
            padding: 0 4px;
            border-radius: 4px;
            font-weight: bold;
            font-size: 11px;
            color: white;
            user-select: none;
            cursor: pointer;
            transition: transform 0.1s ease, box-shadow 0.1s ease;
            flex-shrink: 0;
        }
        .rimsort-dep-badge.pressed { transform: scale(0.9); }
        .rimsort-dep-title {
            color: #fff;
            text-decoration: none;
            transition: color 0.15s ease;
            max-width: 250px;
            overflow: hidden;
            text-overflow: ellipsis;
            white-space: nowrap;
            display: inline-block;
            vertical-align: middle;
        }
        .rimsort-dep-title:hover { color: #4CAF50; }
        .rimsort-dep-children {
            margin-left: 24px;
            border-left: 1px solid rgba(255,255,255,0.1);
            padding-left: 8px;
        }
        .rimsort-dep-loading-container {
            display: flex;
            align-items: center;
            gap: 10px;
            margin-top: 12px;
            padding-top: 12px;
            border-top: 1px solid rgba(255,255,255,0.1);
            color: #888;
            font-size: 13px;
        }
        .rimsort-dep-spinner {
            width: 18px;
            height: 18px;
            border: 2px solid rgba(255,255,255,0.15);
            border-top-color: #4CAF50;
            border-radius: 50%;
            flex-shrink: 0;
            animation: rimsort-spin 0.7s linear infinite;
        }
        @keyframes rimsort-spin {
            to { transform: rotate(360deg); }
        }
        .rimsort-dep-loading {
            color: #888;
            font-style: italic;
            font-size: 12px;
            padding: 4px 0;
        }
        .rimsort-dep-error {
            color: #f44336;
            font-size: 12px;
            padding: 4px 0;
            cursor: pointer;
        }
        .rimsort-dep-circular {
            color: #888;
            font-size: 12px;
            font-style: italic;
            padding: 4px 0;
        }
        .rimsort-dep-maxdepth {
            color: #888;
            font-size: 12px;
            padding: 4px 0;
            border-top: 1px dashed rgba(255,255,255,0.1);
            margin-top: 4px;
        }
        .rimsort-deps-resolving {
            position: relative;
            pointer-events: none;
            opacity: 0.8;
        }
        .rimsort-deps-resolving::after {
            content: "";
            position: absolute;
            inset: 0;
            border-radius: inherit;
            background: linear-gradient(90deg, transparent, rgba(255,255,255,0.15), transparent);
            animation: rimsort-shimmer 1.5s infinite;
        }
        @keyframes rimsort-shimmer {
            0% { transform: translateX(-100%); }
            100% { transform: translateX(100%); }
        }
        .rimsort-solo-link {
            display: block;
            margin-top: 8px;
            padding: 6px 12px;
            background: rgba(255,255,255,0.05);
            border: 1px solid rgba(255,255,255,0.1);
            border-radius: 4px;
            color: #aaa;
            text-decoration: none;
            font-size: 12px;
            text-align: center;
            transition: background 0.15s ease, color 0.15s ease, border-color 0.15s ease;
            cursor: pointer;
        }
        .rimsort-solo-link:hover {
            background: rgba(255,255,255,0.1);
            color: #fff;
            border-color: rgba(255,255,255,0.2);
        }
        .rimsort-btn-hidden {
            display: none !important;
        }
    `;

    // ── Route dispatcher ──────────────────────────────────────────────────
    const ROUTES = [
        {
            name: "details",
            match: (url) => url.startsWith("https://steamcommunity.com/sharedfiles/filedetails/?id="),
            init() {
                createDetailButton();
                createDepSection();
            },
            onMutation() {
                const currentUrl = window.location.href;
                if (document.getElementById("SubscribeItemBtn") && !document.getElementById("pxmodrim-subscribe-btn")) {
                    createDetailButton();
                }
                if (_depSectionInjectedUrl !== currentUrl) {
                    // URL changed, let init handle it
                    return;
                }
                if (document.getElementById("RequiredItems") && !document.getElementById(DEP_SECTION_ID)) {
                    createDepSection();
                }
            },
        },
        {
            name: "grid",
            match: () => true,
            init() {
                window.updateAllModBadges();
            },
            onMutation(mutations) {
                for (const m of mutations) {
                    for (const node of m.addedNodes) {
                        if (node.nodeType !== Node.ELEMENT_NODE) continue;
                        if (node.querySelector('a[href*="sharedfiles/filedetails/?id="]') && node.querySelector("img")) {
                            scheduleBadgeUpdate();
                            return;
                        }
                    }
                }
            },
        },
    ];

    function getActiveRoute() {
        return ROUTES.find(r => r.match(window.location.href));
    }

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

    function getDepBadgeState(modId) {
        if (_installedIds.has(modId)) return BadgeState.INSTALLED;
        if (_checkedIds.has(modId)) return BadgeState.CHECKED;
        return BadgeState.DEFAULT;
    }

    function makeBadgeClickHandler(badge, modId, getTitle) {
        return (e) => {
            e.stopPropagation();
            e.preventDefault();
            if (!_bridge) return;
            if (badge.classList.contains("rimsort-mod-installed")) return;
            badge.classList.add("pressed");
            setTimeout(() => badge.classList.remove("pressed"), 150);
            if (badge.classList.contains("rimsort-mod-default")) {
                _checkedIds.add(modId);
                setBadgeVisuals(badge, BadgeState.CHECKED);
                _bridge.toggle_download_checked(modId, getTitle(), true);
            } else if (badge.classList.contains("rimsort-mod-checked")) {
                _checkedIds.delete(modId);
                setBadgeVisuals(badge, BadgeState.DEFAULT);
                _bridge.toggle_download_checked(modId, "", false);
            }
            refreshAllDepsBadges();
        };
    }

    function parseHTML(htmlText) {
        return new DOMParser().parseFromString(htmlText, "text/html");
    }

    function scrapeDepsFromContainer(containerEl) {
        const deps = [];
        const links = containerEl.querySelectorAll('a[href*="filedetails/?id="]');
        links.forEach((link) => {
            const match = link.href.match(/[?&]id=(\d+)/);
            const modId = match ? match[1] : null;
            const titleEl = link.querySelector(".requiredItem");
            const title = titleEl ? titleEl.textContent.trim() : link.textContent.trim();
            if (modId) deps.push({ id: modId, title: title || `Mod ${modId}` });
        });
        return deps;
    }

    let _bridgeReadyResolve = null;
    let _bridgeReadyReject = null;

    function waitForBridge(timeoutMs = 10000) {
        if (_bridgeReady) return Promise.resolve();
        if (!_bridgeReadyPromise) {
            _bridgeReadyPromise = new Promise((resolve, reject) => {
                _bridgeReadyResolve = resolve;
                _bridgeReadyReject = reject;
                setTimeout(() => {
                    _bridgeReadyPromise = null;
                    _bridgeReadyResolve = null;
                    _bridgeReadyReject = null;
                    reject(new Error("Bridge connection timeout"));
                }, timeoutMs);
            });
        }
        return _bridgeReadyPromise;
    }

    function resolveBridgeReady() {
        if (_bridgeReadyResolve) {
            _bridgeReadyResolve();
            _bridgeReadyResolve = null;
            _bridgeReadyReject = null;
            _bridgeReadyPromise = null;
        }
    }

    // ── Async bridge result dispatch ─────────────────────────────────────
    // Stores Promise resolves for pending dep fetches. Python calls
    // window.__pxmDepsFetched(modId, jsonResult) via runJavaScript when
    // the async HTTP fetch completes.
    window.__pxmPendingDeps = {};

    window.__pxmDepsFetched = function (modId, tree) {
        console.log(`[pxmodrim] __pxmDepsFetched called for ${modId}, result:`, tree);
        const resolve = window.__pxmPendingDeps[modId];
        if (resolve) {
            const valid = tree && tree.id;
            console.log(`[pxmodrim] __pxmDepsFetched for ${modId}: valid=${valid}, deps=${tree?.deps?.length ?? 0}`);
            resolve(valid ? tree : null);
            delete window.__pxmPendingDeps[modId];
        } else {
            console.warn(`[pxmodrim] __pxmDepsFetched: no pending resolve for ${modId}`);
        }
    };

    // ── Dependency resolution strategies (Strategy Pattern) ────────────────────

    const apiStrategy = {
        name: "api",
        async fetch(modId) {
            try {
                console.log(`[pxmodrim] apiStrategy: waiting for bridge for ${modId}`);
                await waitForBridge(8000);
                console.log(`[pxmodrim] apiStrategy: bridge ready, calling fetch_mod_deps for ${modId}`);
                const result = await new Promise((resolve) => {
                    window.__pxmPendingDeps[modId] = resolve;
                    _bridge.fetch_mod_deps(modId);
                    console.log(`[pxmodrim] apiStrategy: fetch_mod_deps called for ${modId}, waiting for __pxmDepsFetched...`);
                });
                console.log(`[pxmodrim] apiStrategy: Promise resolved for ${modId}, tree=`, result ? result.id : null);
                return result;
            } catch (e) {
                console.warn("[pxmodrim] API deps failed:", e);
                return null;
            }
        }
    };

    const domStrategy = {
        name: "dom",
        async fetch(modId) {
            console.log(`[pxmodrim] domStrategy: building tree for ${modId}`);
            return await buildDomDepTree(modId, 0, new Set());
        }
    };

    async function buildDomDepTree(modId, depth, seen) {
        if (depth >= DEPTH_MAX) {
            console.log(`[pxmodrim] buildDomDepTree depth limit for ${modId}`);
            return null;
        }
        if (seen.has(modId)) {
            console.log(`[pxmodrim] buildDomDepTree circular for ${modId}`);
            return null;
        }
        seen.add(modId);

        let tree = _depCache.get(modId);
        if (tree) {
            console.log(`[pxmodrim] buildDomDepTree cache hit for ${modId}`);
            return tree;
        }

        if (_depFetching.has(modId)) {
            console.log(`[pxmodrim] buildDomDepTree waiting for ${modId}`);
            while (_depFetching.has(modId)) {
                await new Promise(r => setTimeout(r, 50));
            }
            tree = _depCache.get(modId);
            if (tree) return tree;
        }

        _depFetching.add(modId);
        const controller = new AbortController();
        _depFetchAborters.set(modId, controller);
        try {
            const resp = await fetch(`https://steamcommunity.com/sharedfiles/filedetails/?id=${modId}`, { signal: controller.signal });
            if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
            const html = await resp.text();
            const doc = parseHTML(html);

            const titleEl = doc.querySelector(".workshopItemTitle");
            const title = titleEl ? titleEl.textContent.trim() : `Mod ${modId}`;

            const container = doc.getElementById("RequiredItems");
            const deps = container ? scrapeDepsFromContainer(container) : [];

            tree = { id: modId, title, deps: [] };
            for (const dep of deps) {
                const child = await buildDomDepTree(dep.id, depth + 1, new Set(seen));
                if (child) tree.deps.push(child);
                else tree.deps.push({ id: dep.id, title: dep.title, deps: [] });
            }

            _depCache.set(modId, tree);
            return tree;
        } catch (e) {
            if (e.name === "AbortError") return null;
            console.warn(`[pxmodrim] DOM dep fetch failed for ${modId}:`, e);
            return null;
        } finally {
            _depFetching.delete(modId);
            _depFetchAborters.delete(modId);
        }
    }

    async function resolveDepTree(modId) {
        const cached = _depCache.get(modId);
        if (cached) {
            console.log(`[pxmodrim] resolveDepTree cache hit for ${modId}`);
            return cached;
        }

        for (const strategy of [apiStrategy, domStrategy]) {
            try {
                console.log(`[pxmodrim] resolveDepTree trying ${strategy.name} for ${modId}`);
                const tree = await strategy.fetch(modId);
                if (tree) {
                    console.log(`[pxmodrim] resolveDepTree ${strategy.name} succeeded for ${modId}`);
                    _depCache.set(modId, tree);
                    return tree;
                }
                console.log(`[pxmodrim] resolveDepTree ${strategy.name} returned null for ${modId}`);
            } catch (e) {
                console.warn(`[pxmodrim] ${strategy.name} strategy failed for ${modId}:`, e);
            }
        }
        console.warn(`[pxmodrim] resolveDepTree all strategies failed for ${modId}`);
        return null;
    }

    function flattenDepTree(node, seen) {
        const result = [];
        if (!node.deps) return result;
        for (const dep of node.deps) {
            if (seen.has(dep.id)) continue;
            if (_installedIds.has(dep.id)) continue;
            if (_checkedIds.has(dep.id)) continue;
            seen.add(dep.id);
            result.push({ id: dep.id, title: dep.title });
            result.push(...flattenDepTree(dep, new Set(seen)));
        }
        return result;
    }

    function cancelPendingDepFetches() {
        _depFetchAborters.forEach((controller) => controller.abort());
        _depFetchAborters.clear();
        _depFetching.clear();
    }

    async function queueModWithDeps(modId, title) {
        if (_isResolvingDeps) return;
        _isResolvingDeps = true;

        const btn = document.getElementById("pxmodrim-subscribe-btn");
        if (btn) {
            btn.classList.add("rimsort-deps-resolving");
            btn.textContent = "Resolving deps...";
        }

        _checkedIds.add(modId);
        if (_bridge) _bridge.toggle_download_checked(modId, title, true);

        const tree = await resolveDepTree(modId);
        if (tree) {
            const allDeps = flattenDepTree(tree, new Set([modId]));
            for (const dep of allDeps) {
                _checkedIds.add(dep.id);
                if (_bridge) _bridge.toggle_download_checked(dep.id, dep.title, true);
            }
        } else {
            console.warn("[pxmodrim] Failed to resolve deps for", modId);
        }

        if (btn) {
            btn.classList.remove("rimsort-deps-resolving");
            setDetailBtnVisuals(btn, getDetailButtonState(modId));
        }
        refreshAllDepsBadges();
        updateSoloLinkVisibility();
        _isResolvingDeps = false;
    }

    function queueModSolo(modId, title) {
        _checkedIds.add(modId);
        if (_bridge) _bridge.toggle_download_checked(modId, title, true);
        const btn = document.getElementById("pxmodrim-subscribe-btn");
        if (btn) setDetailBtnVisuals(btn, getDetailButtonState(modId));
        refreshAllDepsBadges();
        updateSoloLinkVisibility();
    }

    function refreshAllDepsBadges() {
        document.querySelectorAll(`.rimsort-dep-badge`).forEach((badge) => {
            const modId = badge.dataset.modid;
            if (!modId) return;
            setBadgeVisuals(badge, getDepBadgeState(modId));
        });
    }

    function updateSoloLinkVisibility() {
        const soloLink = document.getElementById(DEP_SOLO_LINK_ID);
        const requiredContainer = document.getElementById("RequiredItems");
        const btn = document.getElementById("pxmodrim-subscribe-btn");
        if (!soloLink) return;
        const isInstalled = btn?.classList.contains("rimsort-mod-installed");
        const isChecked = btn?.classList.contains("rimsort-mod-checked");
        if (requiredContainer && !isInstalled && !isChecked) {
            soloLink.classList.remove("hidden");
        } else {
            soloLink.classList.add("hidden");
        }
    }

    async function createDepSection() {
        if (_createDepSectionRunning) {
            console.log(`[pxmodrim] createDepSection already running, skipping`);
            return;
        }
        _createDepSectionRunning = true;
        const currentUrl = window.location.href;
        console.log(`[pxmodrim] createDepSection START, url=${currentUrl}`);
        if (_depSectionInjectedUrl !== currentUrl) {
            _depSectionInjectedUrl = currentUrl;
            const existing = document.getElementById(DEP_SECTION_ID);
            if (existing) existing.remove();
        } else if (document.getElementById(DEP_SECTION_ID)) {
            refreshAllDepsBadges();
            updateSoloLinkVisibility();
            _createDepSectionRunning = false;
            return;
        }

        const modId = (window.location.href.match(/[?&]id=(\d+)/) || [])[1];
        console.log(`[pxmodrim] createDepSection modId=${modId}`);
        if (!modId) {
            _createDepSectionRunning = false;
            return;
        }

        // Insert loading placeholder and hide buttons
        const loadingId = "pxmodrim-dep-loading";
        const loading = document.createElement("div");
        loading.id = loadingId;
        loading.className = "rimsort-dep-loading-container";
        loading.innerHTML = '<div class="rimsort-dep-spinner"></div><span>Resolving dependencies...</span>';

        const removeLoading = () => {
            const el = document.getElementById(loadingId);
            if (el) el.remove();
        };

        const hideButtons = () => {
            const wrapper = document.querySelector("#pxmodrim-subscribe-btn")?.closest("div[style*='flex']");
            if (wrapper) {
                wrapper.classList.add("rimsort-btn-hidden");
            }
        };
        // Loading div will be inserted once buttons area exists.  Before that we
        // hide any existing wrapper and set a flag so createDetailButton hides new ones.
        _loadingDeps = true;
        hideButtons();
        console.log(`[pxmodrim] createDepSection: starting resolveDepTree for ${modId}`);

        const loadingInterval = setInterval(() => {
            const wrapper = document.querySelector("#pxmodrim-subscribe-btn")?.closest("div[style*='flex']");
            if (wrapper) {
                wrapper.classList.add("rimsort-btn-hidden");
                clearInterval(loadingInterval);
            }
            const btn = document.getElementById("SubscribeItemBtn");
            if (btn && btn.parentElement && !document.getElementById(loadingId)) {
                btn.parentElement.insertBefore(loading, btn.nextSibling);
                console.log(`[pxmodrim] createDepSection: loading div inserted after SubscribeItemBtn`);
                clearInterval(loadingInterval);
            } else if (document.body && !document.getElementById(loadingId)) {
                document.body.appendChild(loading);
                console.log(`[pxmodrim] createDepSection: loading div appended to body`);
                clearInterval(loadingInterval);
            }
        }, 30);

        const tree = await resolveDepTree(modId);
        console.log(`[pxmodrim] createDepSection: resolveDepTree returned`, tree ? `id=${tree.id} deps=${tree.deps?.length}` : `null`);
        clearInterval(loadingInterval);
        removeLoading();
        _loadingDeps = false;

        // Show buttons again
        document.querySelectorAll(".rimsort-btn-hidden").forEach(el => el.classList.remove("rimsort-btn-hidden"));

        if (tree && tree.deps?.length) {
            const section = document.createElement("div");
            section.id = DEP_SECTION_ID;
            section.className = "rimsort-deps-section";

            const header = document.createElement("div");
            header.className = "rimsort-deps-header";
            header.textContent = `Dependencies (${tree.deps.length}):`;
            section.appendChild(header);

            const list = document.createElement("div");
            list.className = "rimsort-dep-list";
            renderDepTree(list, tree.deps, 0, new Set());
            section.appendChild(list);

            const btn = document.getElementById("pxmodrim-subscribe-btn") || document.getElementById("SubscribeItemBtn");
            if (btn && btn.parentElement) {
                btn.parentElement.insertBefore(section, btn.nextSibling);
            } else if (document.body) {
                document.body.appendChild(section);
            }
            updateSoloLinkVisibility();
            _createDepSectionRunning = false;
            return;
        }

        // Fallback: scrape immediate deps from current page DOM
        const requiredContainer = document.getElementById("RequiredItems");
        if (!requiredContainer) {
            _createDepSectionRunning = false;
            return;
        }
        const deps = scrapeDepsFromContainer(requiredContainer);

        const section = document.createElement("div");
        section.id = DEP_SECTION_ID;
        section.className = "rimsort-deps-section";

        const header = document.createElement("div");
        header.className = "rimsort-deps-header";
        header.textContent = deps.length ? `Dependencies (${deps.length}):` : "No dependencies found";
        section.appendChild(header);

        const list = document.createElement("div");
        list.className = "rimsort-dep-list";
        if (deps.length) {
            deps.forEach((dep) => {
                renderDepNode(list, dep, 0, new Set());
            });
        } else {
            const empty = document.createElement("div");
            empty.className = "rimsort-dep-empty";
            empty.textContent = "This mod has no required items";
            empty.style.padding = "8px";
            empty.style.color = "#888";
            list.appendChild(empty);
        }
        section.appendChild(list);

        const btn = document.getElementById("pxmodrim-subscribe-btn") || document.getElementById("SubscribeItemBtn");
        if (btn && btn.parentElement) {
            btn.parentElement.insertBefore(section, btn.nextSibling);
        } else if (document.body) {
            document.body.appendChild(section);
        }
        updateSoloLinkVisibility();
        console.log(`[pxmodrim] createDepSection END, section created`);
        _createDepSectionRunning = false;
    }

    function createDepNode(dep, depth) {
        const node = document.createElement("div");
        node.className = "rimsort-dep-node";
        node.style.paddingLeft = `${depth * 16}px`;

        const hasChildren = dep.deps && dep.deps.length > 0 && depth + 1 < DEPTH_MAX;

        const expand = document.createElement("span");
        expand.className = "rimsort-dep-expand";
        expand.textContent = hasChildren ? "▸" : "";
        if (!hasChildren) expand.classList.add("empty");

        expand.addEventListener("click", (e) => {
            e.stopPropagation();
            if (expand.classList.contains("empty")) return;
            expand.classList.toggle("expanded");
            const children = node.nextElementSibling;
            if (children && children.classList.contains("rimsort-dep-children")) {
                children.style.display = expand.classList.contains("expanded") ? "block" : "none";
            }
        });

        const badge = document.createElement("span");
        badge.className = "rimsort-dep-badge";
        badge.dataset.modid = dep.id;
        badge.title = dep.id;
        setBadgeVisuals(badge, getDepBadgeState(dep.id));

        badge.addEventListener("click", makeBadgeClickHandler(badge, dep.id, () => dep.title));

        const titleLink = document.createElement("a");
        titleLink.className = "rimsort-dep-title";
        titleLink.href = `https://steamcommunity.com/sharedfiles/filedetails/?id=${dep.id}`;
        titleLink.target = "_blank";
        titleLink.rel = "noopener noreferrer";
        titleLink.textContent = dep.title;
        titleLink.title = dep.title;

        node.appendChild(expand);
        node.appendChild(badge);
        node.appendChild(titleLink);
        return node;
    }

    function renderDepTree(container, deps, depth, seenIds) {
        deps.forEach((dep) => {
            if (seenIds.has(dep.id)) {
                const circ = document.createElement("div");
                circ.className = "rimsort-dep-circular";
                circ.textContent = `↻ ${dep.title} (circular)`;
                circ.style.paddingLeft = `${depth * 16}px`;
                container.appendChild(circ);
                return;
            }
            seenIds.add(dep.id);

            const node = createDepNode(dep, depth);
            container.appendChild(node);

            if (dep.deps && dep.deps.length) {
                if (depth + 1 >= DEPTH_MAX) {
                    const maxDepth = document.createElement("div");
                    maxDepth.className = "rimsort-dep-maxdepth";
                    maxDepth.style.paddingLeft = `${(depth + 1) * 16}px`;
                    maxDepth.textContent = `${dep.deps.length} required item(s) (max depth ${DEPTH_MAX})`;
                    container.appendChild(maxDepth);
                } else {
                    const childrenContainer = document.createElement("div");
                    childrenContainer.className = "rimsort-dep-children";
                    childrenContainer.style.display = "none";
                    renderDepTree(childrenContainer, dep.deps, depth + 1, new Set(seenIds));
                    container.appendChild(childrenContainer);
                }
            }
        });
    }

    // Legacy renderDepNode for fallback (not used when API strategy succeeds)
    function renderDepNode(container, dep, depth, seenIds) {
        if (seenIds.has(dep.id)) {
            const circ = document.createElement("div");
            circ.className = "rimsort-dep-circular";
            circ.textContent = `↻ ${dep.title} (circular)`;
            container.appendChild(circ);
            return;
        }
        seenIds.add(dep.id);

        const node = document.createElement("div");
        node.className = "rimsort-dep-node";
        node.style.paddingLeft = `${depth * 16}px`;

        const expand = document.createElement("span");
        expand.className = "rimsort-dep-expand";
        expand.textContent = "▸";
        expand.title = "Expand to see dependencies";

        const badge = document.createElement("span");
        badge.className = "rimsort-dep-badge";
        badge.dataset.modid = dep.id;
        badge.title = dep.id;
        setBadgeVisuals(badge, getDepBadgeState(dep.id));

        badge.addEventListener("click", makeBadgeClickHandler(badge, dep.id, () => dep.title));

        const titleLink = document.createElement("a");
        titleLink.className = "rimsort-dep-title";
        titleLink.href = `https://steamcommunity.com/sharedfiles/filedetails/?id=${dep.id}`;
        titleLink.target = "_blank";
        titleLink.rel = "noopener noreferrer";
        titleLink.textContent = dep.title;
        titleLink.title = dep.title;

        node.appendChild(expand);
        node.appendChild(badge);
        node.appendChild(titleLink);
        container.appendChild(node);

        expand.addEventListener("click", (e) => {
            e.stopPropagation();
            if (expand.classList.contains("empty")) return;
            if (expand.classList.contains("expanded")) {
                expand.classList.remove("expanded");
                const children = node.nextElementSibling;
                if (children && children.classList.contains("rimsort-dep-children")) {
                    children.style.display = "none";
                }
            }
        });
    }

    // ── UI component management ──────────────────────────────────────────
    window.updateModBadge = function (modId, status) {
        if (_activeRoute?.name !== "grid") return;

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

            badge.addEventListener("click", makeBadgeClickHandler(badge, modId, () => getModTitle(tile)));
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
        const currentUrl = window.location.href;
        if (_detailInjectedUrl !== currentUrl) {
            // New detail page - reset injection state
            _detailInjectedUrl = currentUrl;
            const existing = document.getElementById("pxmodrim-subscribe-btn");
            if (existing) existing.remove();
            const existingSection = document.getElementById(DEP_SECTION_ID);
            if (existingSection) existingSection.remove();
        }

        let btn = document.getElementById("pxmodrim-subscribe-btn");
        if (btn) {
            const modId = (window.location.href.match(/[?&]id=(\d+)/) || [])[1];
            if (modId) setDetailBtnVisuals(btn, getDetailButtonState(modId));
            updateSoloLinkVisibility();
            return true;
        }

        const oldBtn = document.getElementById("SubscribeItemBtn");
        if (!oldBtn) return false;

        const modId = (window.location.href.match(/[?&]id=(\d+)/) || [])[1];
        if (!modId) return false;

        const h1 = document.querySelector(".game_area_purchase_game h1");
        const title = h1 ? h1.textContent.replace(/Subscribe to download\s*/i, "").trim() : "";

        const wrapper = document.createElement("div");
        wrapper.style.display = "flex";
        wrapper.style.flexDirection = "column";
        wrapper.style.gap = "8px";

        btn = document.createElement("a");
        btn.id = "pxmodrim-subscribe-btn";
        btn.style.textAlign = "center";

        const requiredContainer = document.getElementById("RequiredItems");
        const hasDeps = !!requiredContainer;

        function updateButtonVisuals() {
            const state = getDetailButtonState(modId);
            if (state === "installed") {
                btn.className = "rimsort-detail-btn rimsort-mod-installed";
                btn.textContent = "Installed";
                btn.style.pointerEvents = "none";
            } else if (state === "checked") {
                btn.className = "rimsort-detail-btn rimsort-mod-checked";
                btn.textContent = "✓ In Queue";
            } else {
                btn.className = "rimsort-detail-btn rimsort-mod-default";
                btn.textContent = hasDeps ? "Add to Queue (with deps)" : "Add to Queue";
            }
        }

        btn.addEventListener("click", function (e) {
            e.preventDefault();
            e.stopPropagation();

            if (!_bridge || btn.classList.contains("rimsort-mod-installed")) return;
            if (_isResolvingDeps) return;

            btn.classList.add("pressed");
            requestAnimationFrame(() => btn.classList.remove("pressed"));

            const state = getDetailButtonState(modId);
            if (state === "checked") {
                _checkedIds.delete(modId);
                updateButtonVisuals();
                _bridge.toggle_download_checked(modId, "", false);
                updateSoloLinkVisibility();
            } else {
                queueModWithDeps(modId, title);
            }
        });

        updateButtonVisuals();
        if (_loadingDeps) {
            wrapper.classList.add("rimsort-btn-hidden");
        }
        wrapper.appendChild(btn);

        if (hasDeps) {
            const soloLink = document.createElement("a");
            soloLink.id = DEP_SOLO_LINK_ID;
            soloLink.className = "rimsort-solo-link";
            soloLink.textContent = "Queue only this mod";
            soloLink.href = "#";
            soloLink.addEventListener("click", (e) => {
                e.preventDefault();
                e.stopPropagation();
                if (!_bridge || btn.classList.contains("rimsort-mod-installed")) return;
                if (_isResolvingDeps) return;
                queueModSolo(modId, title);
            });
            wrapper.appendChild(soloLink);
            updateSoloLinkVisibility();
        }

        oldBtn.insertAdjacentElement("afterend", wrapper);
        return true;
    }

    // ── Python integration points (external calls) ────────────────────────
    window.__pxmSetInstalled = function (modIds) {
        _installedIds.clear();
        (modIds || []).forEach(id => _installedIds.add(id));
        if (_bridgeReady) {
            window.updateAllModBadges();
            refreshAllDepsBadges();
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
        refreshAllDepsBadges();
        window.__pxmodrim.onStateChange?.();
    };

    window.__pxmClearChecked = function () {
        _checkedIds.clear();
        window.updateAllModBadges();
        refreshAllDepsBadges();
        window.__pxmodrim.onStateChange?.();
    };

    // Render detail page button on state changes from Python
    window.__pxmodrim.onStateChange = function () {
        const modId = (window.location.href.match(/[?&]id=(\d+)/) || [])[1];
        const btn = document.getElementById("pxmodrim-subscribe-btn");
        if (btn && modId) setDetailBtnVisuals(btn, getDetailButtonState(modId));
        refreshAllDepsBadges();
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
                resolveBridgeReady();

                let initInstalled = false;
                let initChecked = false;

                const renderInitialState = () => {
                    if (initInstalled && initChecked) {
                        _activeRoute.init();
                        refreshAllDepsBadges();
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
        if (_activeRoute?.name !== "grid") return;
        _badgeRaf = requestAnimationFrame(() => {
            _badgeRaf = null;
            if (_bridgeReady) window.updateAllModBadges();
        });
    }

    // ── Safe MutationObserver init after root is ready ─────────────────────
    function initObservers() {
        console.log("[pxmodrim] initObservers START");
        _activeRoute = getActiveRoute();
        console.log("[pxmodrim] Active route:", _activeRoute?.name, _activeRoute?.match(window.location.href));

        const mainObserver = new MutationObserver((mutations) => {
            _activeRoute.onMutation(mutations);
            runDomCleanup();
        });

        mainObserver.observe(document.documentElement, {
            childList: true,
            subtree: true,
        });

        runDomCleanup();
        _activeRoute.init();
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