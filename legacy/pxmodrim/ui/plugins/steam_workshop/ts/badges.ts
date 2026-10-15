import { ModState } from "./types";
import { log, getModId, getModTitle, findItemCard } from "./utils";
import { getModState, applyModState, handleClick } from "./controls";
import { isBridgeDataReady } from "./bridge";
import { isPageKind } from "./lifecycle";

let _badgeTimer: ReturnType<typeof setTimeout> | null = null;
const BADGE_DEBOUNCE_MS = 300;

export function updateModBadge(modId: string): void {
  if (!isPageKind("grid")) return;
  const state = getModState(modId);

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

    badge.addEventListener("click", async (e) => {
      e.stopPropagation();
      e.preventDefault();
      await handleClick(badge!, modId, () => getModTitle(tile));
      updateAllModBadges();
    });
  }

  applyModState(badge, state);
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
    updateModBadge(modId);
    badged++;
  });
  log(`Badges updated: ${badged}`);
}

export function scheduleBadgeUpdate(): void {
  if (_badgeTimer !== null) return;
  if (!isPageKind("grid")) return;
  _badgeTimer = setTimeout(() => {
    _badgeTimer = null;
    if (isBridgeDataReady()) updateAllModBadges();
  }, BADGE_DEBOUNCE_MS);
}

export function refreshAllDepsBadges(): void {
  document.querySelectorAll(".rimsort-dep-badge").forEach((badge) => {
    const el = badge as HTMLElement;
    const modId = el.dataset.modid;
    if (!modId) return;
    applyModState(el, getModState(modId));
  });
}
