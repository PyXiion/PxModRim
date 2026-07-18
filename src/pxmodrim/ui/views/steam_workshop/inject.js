(function () {
    "use strict";

    if (window.__pxmodrimInited) {
        return;
    }
    window.__pxmodrimInited = true;

    const style = document.createElement("style");
    style.textContent = `
        .rimsort-modstatus-badge {
            position: absolute;
            top: 5px;
            right: 5px;
            color: white;
            width: 32px;
            height: 32px;
            border-radius: 6px;
            display: flex;
            align-items: center;
            justify-content: center;
            font-weight: bold;
            font-size: 20px;
            box-shadow: 0 0 4px black;
            cursor: default;
            user-select: none;
            transition: transform 0.1s ease, box-shadow 0.1s ease, opacity 0.2s ease, visibility 0.2s ease;
            z-index: 10;
        }

        .rimsort-modstatus-badge.rimsort-badge-hovered {
            transform: scale(1.05);
            box-shadow: 0 0 8px rgba(0,0,0,0.4);
        }

        .rimsort-modstatus-badge.pressed {
            transform: scale(0.9);
        }

        .rimsort-mod-installed {
            background-color: #4CAF50;
        }

        .rimsort-mod-default {
            background-color: #2196F3;
            cursor: pointer;
        }
    `;
    document.head.appendChild(style);

    const BadgeState = {
        INSTALLED: "installed",
        DEFAULT: "default",
    };

    function getInstalledMods() {
        return Array.isArray(window.__pxmodrimInstalledMods)
            ? window.__pxmodrimInstalledMods
            : [];
    }

    window.updateModBadge = function (modId, status) {
        const tile = document
            .querySelector(`.workshopItem a[href*="id=${modId}"]`)
            ?.closest(".workshopItem");
        if (!tile) {
            return;
        }

        let badge = tile.querySelector(".rimsort-modstatus-badge");
        if (!badge) {
            badge = document.createElement("div");
            badge.className = "rimsort-modstatus-badge";
            tile.style.position = "relative";
            tile.appendChild(badge);
        }

        if (status === BadgeState.INSTALLED) {
            badge.title = "Already installed";
            badge.innerHTML = "✓";
            badge.classList.remove("rimsort-mod-default");
            badge.classList.add("rimsort-mod-installed");
            badge.style.opacity = "1";
            badge.style.visibility = "visible";
            const titleEl = tile.querySelector(".workshopItemTitle");
            if (titleEl) {
                titleEl.style.color = "#4CAF50";
            }
        } else {
            badge.title = "Add to list";
            badge.innerHTML = "+";
            badge.classList.remove("rimsort-mod-installed");
            badge.classList.add("rimsort-mod-default");
            const titleEl = tile.querySelector(".workshopItemTitle");
            if (titleEl) {
                titleEl.style.color = "";
            }
            badge.style.opacity = "0";
            badge.style.visibility = "hidden";
        }
    };

    window.updateAllModBadges = function () {
        const installedMods = getInstalledMods();
        const tiles = document.querySelectorAll(".workshopItem");
        let badged = 0;
        tiles.forEach(function (tile) {
            const link = tile.querySelector('a[href*="id="]');
            if (!link) {
                return;
            }
            const match = link.href.match(/id=(\d+)/);
            if (!match) {
                return;
            }
            const modId = match[1];
            if (installedMods.includes(modId)) {
                window.updateModBadge(modId, BadgeState.INSTALLED);
                badged++;
            } else {
                window.updateModBadge(modId, BadgeState.DEFAULT);
            }
        });
        console.log("[pxmodrim] updateAllModBadges: tiles=" + tiles.length + " installed=" + badged);
    };

    // Watcher: badge tiles as Steam streams them in (infinite scroll / async),
    // so badges appear instantly instead of only once per page load.
    let _badgeRaf = null;
    function _scheduleBadgeUpdate() {
        if (_badgeRaf !== null) {
            return;
        }
        _badgeRaf = requestAnimationFrame(function () {
            _badgeRaf = null;
            window.updateAllModBadges();
        });
    }

    const _badgeObserver = new MutationObserver(function (mutations) {
        for (const m of mutations) {
            for (const node of m.addedNodes) {
                if (
                    node.nodeType === Node.ELEMENT_NODE &&
                    (node.classList.contains("workshopItem") ||
                        node.querySelector(".workshopItem"))
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
    console.log("[pxmodrim] MutationObserver installed");

    // Seed from any list already present, then watch for live updates.
    window.updateAllModBadges();
})();
