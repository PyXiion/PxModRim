(function pxmodrimMain() {
    /* eslint-disable max-lines-per-function */
    "use strict";

    const DEBUG = true;
    const log = DEBUG ? console.log.bind(console, "[pxmodrim]") : () => {};
    const warn = console.warn.bind(console, "[pxmodrim]");

    log("SCRIPT LOADED");

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

    const BadgeState = Object.freeze({
        INSTALLED: "installed",
        CHECKED: "checked",
        DEFAULT: "default",
        RESOLVING: "resolving",
    });

    let _installedIds = window.__pxmodrim.installedIds;
    let _checkedIds = window.__pxmodrim.checkedIds;
    let _bridge = null;
    let _bridgeReady = false;
    let _bridgeReadyPromise = null;
    let _badgeRaf = null;
    let _activeRoute = null;

    const Config = Object.freeze({
        DEPTH_MAX: 3,
        DEP_SECTION_ID: "pxmodrim-deps",
        DEP_SOLO_LINK_ID: "pxmodrim-solo-link",
    });

    const DepState = {
        cache: new Map(),
        fetching: new Set(),
        aborters: new Map(),
    };

    const DetailState = {
        injectedUrl: null,
        resolvingDeps: false,
    };

    let _bridgeDataReady = false;
    let _bridgeDataResolve = null;
    let _bridgeDataReject = null;
    let _bridgeDataPromise = null;
    let _depsLoading = false;
    const _resolveTreeLocks = new Set();

    function waitForBridgeData(timeoutMs = 15000) {
        if (!_bridgeDataPromise) {
            _bridgeDataPromise = new Promise((resolve, reject) => {
                _bridgeDataResolve = resolve;
                _bridgeDataReject = reject;
                setTimeout(() => {
                    _bridgeDataPromise = null;
                    _bridgeDataResolve = null;
                    _bridgeDataReject = null;
                    reject(new Error("Bridge data timeout"));
                }, timeoutMs);
            });
        }
        if (_bridgeDataReady) {
            _bridgeDataResolve?.();
        }
        return _bridgeDataPromise;
    }

    // CSS_STYLES is prepended at script injection time in SteamWorkshop.qml

    // ── Route dispatcher ──────────────────────────────────────────────────
    const ROUTES = [
        {
            name: "details",
            match: (url) => url.startsWith("https://steamcommunity.com/sharedfiles/filedetails/?id="),
            init() {
                createDetailButton();
            },
            onMutation() {
                const currentUrl = window.location.href;
                if (document.getElementById("SubscribeItemBtn") && !document.getElementById("pxmodrim-subscribe-btn")) {
                    createDetailButton();
                    return;
                }
                if (DetailState.injectedUrl !== currentUrl) {
                    return;
                }
                if (document.getElementById("RequiredItems") && !document.getElementById(Config.DEP_SECTION_ID)) {
                    createDetailButton();
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
        badge.classList.remove("rimsort-mod-installed", "rimsort-mod-checked", "rimsort-mod-default", "rimsort-mod-resolving");
        if (status === BadgeState.INSTALLED) {
            badge.title = "Already installed";
            badge.innerHTML = "&#10003;";
            badge.classList.add("rimsort-mod-installed");
        } else if (status === BadgeState.CHECKED) {
            badge.title = "Preparing to download";
            badge.innerHTML = "&#8722;";
            badge.classList.add("rimsort-mod-checked");
        } else if (status === BadgeState.RESOLVING) {
            badge.title = "Resolving dependencies...";
            badge.innerHTML = '<div class="rimsort-dep-spinner" style="width:14px;height:14px;border-width:2px;"></div>';
            badge.classList.add("rimsort-mod-resolving");
        } else {
            badge.title = "Add to list";
            badge.innerHTML = "+";
            badge.classList.add("rimsort-mod-default");
        }
    }

    function setDetailBtnVisuals(el, state, defaultLabel) {
        el.className = "rimsort-detail-btn";
        if (state === "installed") {
            el.classList.add("rimsort-mod-installed");
            el.textContent = "Installed";
            el.style.pointerEvents = "none";
        } else if (state === "checked") {
            el.classList.add("rimsort-mod-checked");
            el.textContent = "\u2713 In Queue";
            el.style.pointerEvents = "";
        } else {
            el.classList.add("rimsort-mod-default");
            el.textContent = defaultLabel || "Add to Queue";
            el.style.pointerEvents = "";
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
        return async (e) => {
            e.stopPropagation();
            e.preventDefault();
            if (!_bridge) return;
            if (badge.classList.contains("rimsort-mod-installed")) return;
            if (badge.classList.contains("rimsort-mod-resolving")) return;
            badge.classList.add("pressed");
            setTimeout(() => badge.classList.remove("pressed"), 150);
            if (badge.classList.contains("rimsort-mod-default")) {
                setBadgeVisuals(badge, BadgeState.RESOLVING);

                const title = getTitle();
                const tree = await resolveDepTree(modId);

                const toggles = [{ id: modId, title }];
                _checkedIds.add(modId);

                if (tree) {
                    const allDeps = flattenDepTree(tree, new Set([modId]));
                    for (const dep of allDeps) {
                        _checkedIds.add(dep.id);
                        toggles.push(dep);
                    }
                } else {
                    warn(`Failed to resolve deps for badge ${modId}`);
                }

                batchToggleChecked(toggles, true);

                setBadgeVisuals(badge, BadgeState.CHECKED);
                refreshAllDepsBadges();
                window.updateAllModBadges();
            } else if (badge.classList.contains("rimsort-mod-checked")) {
                _checkedIds.delete(modId);
                setBadgeVisuals(badge, BadgeState.DEFAULT);
                _bridge.toggle_download_checked(modId, "", false);
                refreshAllDepsBadges();
                window.updateAllModBadges();
            }
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
        log(`__pxmDepsFetched called for ${modId}, result:`, tree);
        const resolve = window.__pxmPendingDeps[modId];
        if (resolve) {
            const valid = tree != null;
            log(`__pxmDepsFetched for ${modId}: valid=${valid}, itemsCount=${tree?.totalItemsLoaded ?? 0}`);
            resolve(valid ? tree : null);
            delete window.__pxmPendingDeps[modId];
        } else {
            warn(`__pxmDepsFetched: no pending resolve for ${modId}`);
        }
    };

    // ── Flat items map → tree converter ──────────────────────────────────────
    function convertItemsToTree(items, rootId, maxDepth = Config.DEPTH_MAX) {
        function buildNode(id, depth, seen) {
            if (seen.has(id)) {
                return { id, title: items[id]?.title || `Mod ${id}`, deps: [] };
            }
            if (depth >= maxDepth) {
                const item = items[id];
                if (!item) return { id, title: `Mod ${id}`, deps: [] };
                return { id: item.id, title: item.title || `Mod ${id}`, deps: [] };
            }
            seen.add(id);
            const item = items[id];
            if (!item) return { id, title: `Mod ${id}`, deps: [] };
            return {
                id: item.id,
                title: item.title || `Mod ${id}`,
                deps: (item.deps || []).map(depId => buildNode(depId, depth + 1, new Set(seen))),
            };
        }
        return buildNode(rootId, 0, new Set());
    }

    // ── Dependency resolution strategies (Strategy Pattern) ────────────────────

    const apiStrategy = {
        name: "api",
        async fetch(modId) {
            try {
                await waitForBridge(8000);

                const MAX_ATTEMPTS = 30;

                for (let attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
                    const result = await new Promise((resolve) => {
                        window.__pxmPendingDeps[modId] = resolve;
                        _bridge.fetch_mod_deps(modId);
                    });

                    if (!result || !result.items || !result.rootId) {
                        warn(`apiStrategy: invalid response for ${modId}`);
                        return null;
                    }

                    if (result.isComplete) {
                        log(`apiStrategy: complete for ${modId} after ${attempt + 1} attempt(s)`);
                        return convertItemsToTree(result.items, result.rootId);
                    }

                    log(`apiStrategy: incomplete for ${modId} (${result.totalItemsLoaded} items), retry ${attempt + 1}/30`);
                    await new Promise(r => setTimeout(r, 300));
                }

                warn(`apiStrategy: still incomplete after 30 attempts for ${modId}`);
                return null;
            } catch (e) {
                warn("API deps failed:", e);
                return null;
            }
        }
    };

    const domStrategy = {
        name: "dom",
        async fetch(modId) {
            log(`domStrategy: building tree for ${modId}`);
            return await buildDomDepTree(modId, 0, new Set());
        }
    };

    async function buildDomDepTree(modId, depth, seen) {
        if (depth >= Config.DEPTH_MAX) {
            log(`buildDomDepTree depth limit for ${modId}`);
            return null;
        }
        if (seen.has(modId)) {
            log(`buildDomDepTree circular for ${modId}`);
            return null;
        }
        seen.add(modId);

        let tree = DepState.cache.get(modId);
        if (tree) {
            log(`buildDomDepTree cache hit for ${modId}`);
            return tree;
        }

        if (DepState.fetching.has(modId)) {
            log(`buildDomDepTree waiting for ${modId}`);
            while (DepState.fetching.has(modId)) {
                await new Promise(r => setTimeout(r, 50));
            }
            tree = DepState.cache.get(modId);
            if (tree) return tree;
        }

        DepState.fetching.add(modId);
        const controller = new AbortController();
        DepState.aborters.set(modId, controller);
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

            DepState.cache.set(modId, tree);
            return tree;
        } catch (e) {
            if (e.name === "AbortError") return null;
            warn(`DOM dep fetch failed for ${modId}:`, e);
            return null;
        } finally {
            DepState.fetching.delete(modId);
            DepState.aborters.delete(modId);
        }
    }

    async function resolveDepTree(modId) {
        const cached = DepState.cache.get(modId);
        if (cached) {
            log(`resolveDepTree cache hit for ${modId}`);
            return cached;
        }

        if (_resolveTreeLocks.has(modId)) {
            log(`resolveDepTree waiting for concurrent fetch of ${modId}`);
            while (_resolveTreeLocks.has(modId)) {
                await new Promise(r => setTimeout(r, 50));
            }
            return DepState.cache.get(modId) || null;
        }

        _resolveTreeLocks.add(modId);
        try {
            for (const strategy of [apiStrategy, domStrategy]) {
                try {
                    log(`resolveDepTree trying ${strategy.name} for ${modId}`);
                    const tree = await strategy.fetch(modId);
                    if (tree) {
                        log(`resolveDepTree ${strategy.name} succeeded for ${modId}`);
                        DepState.cache.set(modId, tree);
                        return tree;
                    }
                    log(`resolveDepTree ${strategy.name} returned null for ${modId}`);
                } catch (e) {
                    warn(`${strategy.name} strategy failed for ${modId}:`, e);
                }
            }
            warn(`resolveDepTree all strategies failed for ${modId}`);
            return null;
        } finally {
            _resolveTreeLocks.delete(modId);
        }
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
        DepState.aborters.forEach((controller) => controller.abort());
        DepState.aborters.clear();
        DepState.fetching.clear();
    }

    async function queueModWithDeps(modId, title) {
        if (DetailState.resolvingDeps) return;
        DetailState.resolvingDeps = true;

        const btn = document.getElementById("pxmodrim-subscribe-btn");
        if (btn) {
            btn.classList.add("rimsort-deps-resolving");
            btn.textContent = "Resolving deps...";
        }

        _checkedIds.add(modId);

        const toggles = [{ id: modId, title }];
        const tree = await resolveDepTree(modId);
        if (tree) {
            const allDeps = flattenDepTree(tree, new Set([modId]));
            for (const dep of allDeps) {
                _checkedIds.add(dep.id);
                toggles.push(dep);
            }
        } else {
            warn("Failed to resolve deps for", modId);
        }

        batchToggleChecked(toggles, true);

        if (btn) {
            btn.classList.remove("rimsort-deps-resolving");
            setDetailBtnVisuals(btn, getDetailButtonState(modId));
        }
        refreshAllDepsBadges();
        updateSoloLinkVisibility();
        DetailState.resolvingDeps = false;
    }

    function queueModSolo(modId, title) {
        _checkedIds.add(modId);
        if (_bridge) _bridge.toggle_download_checked(modId, title, true);
        const btn = document.getElementById("pxmodrim-subscribe-btn");
        if (btn) setDetailBtnVisuals(btn, getDetailButtonState(modId));
        refreshAllDepsBadges();
        updateSoloLinkVisibility();
    }

    function batchToggleChecked(mods, checked) {
        if (!_bridge || !mods.length) return;
        const ids = mods.map(m => m.id);
        const titles = mods.map(m => m.title);
        _bridge.batch_toggle_download_checked(ids, titles, checked);
    }

    function refreshAllDepsBadges() {
        document.querySelectorAll(`.rimsort-dep-badge`).forEach((badge) => {
            const modId = badge.dataset.modid;
            if (!modId) return;
            setBadgeVisuals(badge, getDepBadgeState(modId));
        });
    }

    function updateSoloLinkVisibility() {
        const soloLink = document.getElementById(Config.DEP_SOLO_LINK_ID);
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

    function createLoadingPlaceholder(loadingFactory, afterLoadedFactory, event) {
        const loadingEl = loadingFactory();
        const container = document.createElement("div");
        container.id = "pxmodrim-loading-container";
        container.style.display = "flex";
        container.style.flexDirection = "column";
        container.style.gap = "8px";
        container.appendChild(loadingEl);

        event().then(
            () => afterLoadedFactory(loadingEl, container),
            () => {
                warn("Bridge data timeout — showing error state");
                loadingEl.className = "rimsort-error-btn";
                loadingEl.textContent = "ERROR";
            }
        );

        return container;
    }

    function finalizeButton(btn, container, modId, title, hasDeps) {
        btn.id = "pxmodrim-subscribe-btn";
        btn.className = "rimsort-detail-btn";
        btn.style.textAlign = "center";

        function updateButtonVisuals() {
            setDetailBtnVisuals(btn, getDetailButtonState(modId),
                hasDeps ? "Add to Queue (with deps)" : "Add to Queue");
        }

        btn.addEventListener("click", function (e) {
            e.preventDefault();
            e.stopPropagation();
            if (!_bridge || btn.classList.contains("rimsort-mod-installed")) return;
            if (DetailState.resolvingDeps) return;

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

        if (hasDeps) {
            const soloLink = document.createElement("a");
            soloLink.id = Config.DEP_SOLO_LINK_ID;
            soloLink.className = "rimsort-solo-link";
            soloLink.textContent = "Queue only this mod";
            soloLink.href = "#";
            soloLink.addEventListener("click", (e) => {
                e.preventDefault();
                e.stopPropagation();
                if (!_bridge || btn.classList.contains("rimsort-mod-installed")) return;
                if (DetailState.resolvingDeps) return;
                queueModSolo(modId, title);
            });
            container.appendChild(soloLink);
            updateSoloLinkVisibility();
        }
    }

    function buildDepSection(tree) {
        const section = document.createElement("div");
        section.id = Config.DEP_SECTION_ID;
        section.className = "rimsort-deps-section";

        if (tree && tree.deps?.length) {
            const header = document.createElement("div");
            header.className = "rimsort-deps-header";
            header.textContent = `Dependencies (${tree.deps.length}):`;
            section.appendChild(header);
            const list = document.createElement("div");
            list.className = "rimsort-dep-list";
            renderDepTree(list, tree.deps, 0, new Set());
            section.appendChild(list);
            return section;
        }

        // Authoritative empty result from API — no need for DOM fallback
        if (tree) {
            const header = document.createElement("div");
            header.className = "rimsort-deps-header";
            header.textContent = "No dependencies found";
            section.appendChild(header);
            const list = document.createElement("div");
            list.className = "rimsort-dep-list";
            const empty = document.createElement("div");
            empty.className = "rimsort-dep-empty";
            empty.textContent = "This mod has no required items";
            empty.style.padding = "8px";
            empty.style.color = "#888";
            list.appendChild(empty);
            section.appendChild(list);
            return section;
        }

        const required = document.getElementById("RequiredItems");
        if (!required) return null;
        const deps = scrapeDepsFromContainer(required);
        const header = document.createElement("div");
        header.className = "rimsort-deps-header";
        header.textContent = deps.length ? `Dependencies (${deps.length}):` : "No dependencies found";
        section.appendChild(header);
        const list = document.createElement("div");
        list.className = "rimsort-dep-list";
        if (deps.length) {
            deps.forEach(d => list.appendChild(createDepNode({ id: d.id, title: d.title, deps: [] }, 0)));
        } else {
            const empty = document.createElement("div");
            empty.className = "rimsort-dep-empty";
            empty.textContent = "This mod has no required items";
            empty.style.padding = "8px";
            empty.style.color = "#888";
            list.appendChild(empty);
        }
        section.appendChild(list);
        return section;
    }

    function createDepNode(dep, depth) {
        const node = document.createElement("div");
        node.className = "rimsort-dep-node";
        node.style.paddingLeft = `${depth * 16}px`;

        const hasChildren = dep.deps && dep.deps.length > 0 && depth + 1 < Config.DEPTH_MAX;

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
                if (depth + 1 >= Config.DEPTH_MAX) {
                    const maxDepth = document.createElement("div");
                    maxDepth.className = "rimsort-dep-maxdepth";
                    maxDepth.style.paddingLeft = `${(depth + 1) * 16}px`;
                    maxDepth.textContent = `${dep.deps.length} required item(s) (max depth ${Config.DEPTH_MAX})`;
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
        log(`Badges updated: links=${links.length} installed=${badged}`);
    };

    async function createDetailButton() {
        const currentUrl = window.location.href;
        if (DetailState.injectedUrl !== currentUrl) {
            DetailState.injectedUrl = currentUrl;
            document.getElementById("pxmodrim-subscribe-btn")?.remove();
            document.getElementById(Config.DEP_SECTION_ID)?.remove();
            document.getElementById("pxmodrim-loading-container")?.remove();
        }

        let btn = document.getElementById("pxmodrim-subscribe-btn");
        let container;

        if (btn) {
            const modId = (window.location.href.match(/[?&]id=(\d+)/) || [])[1];
            if (modId) setDetailBtnVisuals(btn, getDetailButtonState(modId));
            updateSoloLinkVisibility();
            container = btn.closest("div[style*='flex']") || btn.parentElement;
        } else if (document.getElementById("pxmodrim-loading-container")) {
            return;
        } else {
            const steamBtn = document.getElementById("SubscribeItemBtn");
            if (!steamBtn) return;

            const modId = (window.location.href.match(/[?&]id=(\d+)/) || [])[1];
            if (!modId) return;

            const h1 = document.querySelector(".game_area_purchase_game h1");
            const title = h1 ? h1.textContent.replace(/Subscribe to download\s*/i, "").trim() : "";
            const hasDeps = !!document.getElementById("RequiredItems");

            if (_bridgeDataReady) {
                container = document.createElement("div");
                container.style.display = "flex";
                container.style.flexDirection = "column";
                container.style.gap = "8px";
                btn = document.createElement("a");
                container.appendChild(btn);
                finalizeButton(btn, container, modId, title, hasDeps);
                steamBtn.replaceWith(container);
            } else {
                container = createLoadingPlaceholder(
                    () => {
                        const el = document.createElement("a");
                        el.className = "rimsort-loading-btn";
                        el.textContent = "Loading...";
                        return el;
                    },
                    (loadingEl, ctr) => {
                        finalizeButton(loadingEl, ctr, modId, title, hasDeps);
                        createDetailButton();
                    },
                    () => waitForBridgeData(10000)
                );
                steamBtn.replaceWith(container);
                return;
            }
        }

        if (!document.getElementById("RequiredItems")) return;

        const modId = (window.location.href.match(/[?&]id=(\d+)/) || [])[1];
        if (!modId) return;

        if (document.getElementById(Config.DEP_SECTION_ID)) {
            refreshAllDepsBadges();
            updateSoloLinkVisibility();
            return;
        }

        if (_depsLoading) return;
        _depsLoading = true;
        const loading = document.createElement("div");
        loading.className = "rimsort-dep-loading-container";
        loading.innerHTML = '<div class="rimsort-dep-spinner"></div><span>Resolving dependencies...</span>';
        container.insertAdjacentElement("afterend", loading);

        try {
            const tree = await resolveDepTree(modId);

            loading.remove();
            const section = buildDepSection(tree);
            if (section) container.insertAdjacentElement("afterend", section);
            updateSoloLinkVisibility();
        } finally {
            _depsLoading = false;
        }
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
                log("QWebChannel connection established");
                resolveBridgeReady();

                let initInstalled = false;
                let initChecked = false;

                const renderInitialState = () => {
                    if (initInstalled && initChecked) {
                        _bridgeDataReady = true;
                        _bridgeDataResolve?.();
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
                warn("QWebChannel not available after " + _WC_MAX_RETRIES + " retries; giving up.");
                return;
            }
            // Qt environment object may load slightly later
            setTimeout(setupWebChannel, 30);
        }
    }

    // ── Steam DOM cleanup ────────────────────────────────────────────────
    function runDomCleanup() {
        // Remove grid-area children that cause layout issues.
        // Only operates on footer elements — never touches React-managed DOM.
        const footer = document.getElementById("footer");
        footer?.querySelectorAll("[style*='--grid-area']").forEach((el) => {
            const area = el.style.getPropertyValue("--grid-area");
            if (area && area !== "main") el.remove();
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
        log("initObservers START");
        _activeRoute = getActiveRoute();
        log("Active route:", _activeRoute?.name, _activeRoute?.match(window.location.href));

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
        log("Base styles injected at Document Creation. Starting lifecycle...");
        setupWebChannel();
        initObservers();
    }

    if (document.documentElement) {
        startScript();
    } else {
        waitForRoot();
    }
})();