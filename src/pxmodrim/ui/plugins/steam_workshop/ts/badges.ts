import { BadgeState, Config } from "./types";
import type { BadgeStatus } from "./types";
import { log, warn, getModId, getModTitle, findItemCard } from "./utils";
import { setBadgeVisuals, getDepBadgeState } from "./visuals";
import { getDepsFor, flattenDepTree } from "./deps";
import { batchToggleChecked } from "./detail";
import { PxModRimAPI } from "./api";
import { isBridgeDataReady } from "./bridge";
import { _activeRoute } from "./lifecycle";

let _badgeRaf: number | null = null;

export function makeBadgeClickHandler(
  badge: HTMLElement,
  modId: string,
  getTitle: () => string,
): (e: MouseEvent) => Promise<void> {
  return async (e) => {
    e.stopPropagation();
    e.preventDefault();
    if (badge.classList.contains("rimsort-mod-installed")) return;
    if (badge.classList.contains("rimsort-mod-resolving")) return;
    badge.classList.add("pressed");
    requestAnimationFrame(() => badge.classList.remove("pressed"));
    if (badge.classList.contains("rimsort-mod-default")) {
      setBadgeVisuals(badge, BadgeState.RESOLVING);

      const title = getTitle();
      const tree = await getDepsFor(modId);

      const toggles: { id: string; title: string }[] = [
        { id: modId, title },
      ];
      window.__pxmodrim.checkedIds.add(modId);

      if (tree) {
        const allDeps = flattenDepTree(tree, new Set([modId]));
        for (const dep of allDeps) {
          window.__pxmodrim.checkedIds.add(dep.id);
          toggles.push(dep);
        }
      } else {
        warn(`Failed to resolve deps for badge ${modId}`);
      }

      batchToggleChecked(toggles, true);

      setBadgeVisuals(badge, BadgeState.CHECKED);
      refreshAllDepsBadges();
      updateAllModBadges();
    } else if (badge.classList.contains("rimsort-mod-checked")) {
      window.__pxmodrim.checkedIds.delete(modId);
      setBadgeVisuals(badge, BadgeState.DEFAULT);
      PxModRimAPI.toggleDownloadChecked(modId, "", false);
      refreshAllDepsBadges();
      updateAllModBadges();
    }
  };
}

export function refreshAllDepsBadges(): void {
  document.querySelectorAll(".rimsort-dep-badge").forEach((badge) => {
    const el = badge as HTMLElement;
    const modId = el.dataset.modid;
    if (!modId) return;
    setBadgeVisuals(el, getDepBadgeState(modId));
  });
}

export function scheduleBadgeUpdate(): void {
  if (_badgeRaf !== null) return;
  if (_activeRoute?.name !== "grid") return;
  _badgeRaf = requestAnimationFrame(() => {
    _badgeRaf = null;
    if (isBridgeDataReady()) updateAllModBadges();
  });
}

export function updateModBadge(modId: string, status: BadgeStatus): void {
  if (_activeRoute?.name !== "grid") return;
  log("updateModBadge modId=" + modId + " status=" + status);

  const link = document.querySelector<HTMLAnchorElement>(
    `a[href*="sharedfiles/filedetails/?id=${modId}"]`,
  );
  if (!link) return;

  const tile = findItemCard(link);
  if (!tile) return;

  let badge = tile.querySelector<HTMLElement>(".rimsort-modstatus-badge");
  if (!badge) {
    badge = document.createElement("div");
    badge.className = "rimsort-modstatus-badge";
    if (getComputedStyle(tile).position === "static") {
      (tile as HTMLElement).style.position = "relative";
    }
    tile.classList.add("rimsort-tile");
    tile.appendChild(badge);

    badge.addEventListener(
      "click",
      makeBadgeClickHandler(badge, modId, () => getModTitle(tile)),
    );
  }
  setBadgeVisuals(badge, status);
}

export function updateAllModBadges(): void {
  log("updateAllModBadges ENTER");
  const links = document.querySelectorAll<HTMLAnchorElement>(
    'a[href*="sharedfiles/filedetails/?id="]',
  );
  let badged = 0;
  links.forEach((link) => {
    const modId = getModId(link);
    if (!modId) return;

    if (window.__pxmodrim.installedIds.has(modId)) {
      updateModBadge(modId, BadgeState.INSTALLED);
      badged++;
    } else if (window.__pxmodrim.checkedIds.has(modId)) {
      updateModBadge(modId, BadgeState.CHECKED);
    } else {
      updateModBadge(modId, BadgeState.DEFAULT);
    }
  });
  log(
    `Badges updated: links=${links.length} installed=${badged}`,
  );
}
