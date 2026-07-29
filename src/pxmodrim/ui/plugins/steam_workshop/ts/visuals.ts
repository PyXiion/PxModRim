import { BadgeState, type BadgeStatus } from "./types";

export function setBadgeVisuals(
  badge: HTMLElement,
  status: BadgeStatus,
): void {
  badge.classList.remove(
    "rimsort-mod-installed",
    "rimsort-mod-checked",
    "rimsort-mod-default",
    "rimsort-mod-resolving",
  );
  switch (status) {
    case BadgeState.INSTALLED:
      badge.title = "Already installed";
      badge.textContent = "\u2713";
      badge.classList.add("rimsort-mod-installed");
      break;
    case BadgeState.CHECKED:
      badge.title = "Preparing to download";
      badge.textContent = "\u2212";
      badge.classList.add("rimsort-mod-checked");
      break;
    case BadgeState.RESOLVING:
      badge.title = "Resolving dependencies...";
      badge.innerHTML =
        '<div class="rimsort-dep-spinner" style="width:14px;height:14px;border-width:2px;"></div>';
      badge.classList.add("rimsort-mod-resolving");
      break;
    default:
      badge.title = "Add to list";
      badge.innerHTML = "+";
      badge.classList.add("rimsort-mod-default");
      break;
  }
}

export function setDetailBtnVisuals(
  el: HTMLElement,
  state: BadgeStatus,
  defaultLabel?: string,
): void {
  el.className = "rimsort-detail-btn";
  if (state === BadgeState.INSTALLED) {
    el.classList.add("rimsort-mod-installed");
    el.textContent = "Installed";
    el.style.pointerEvents = "none";
  } else if (state === BadgeState.CHECKED) {
    el.classList.add("rimsort-mod-checked");
    el.textContent = "\u2713 In Queue";
    el.style.pointerEvents = "";
  } else {
    el.classList.add("rimsort-mod-default");
    el.textContent = defaultLabel || "Add to Queue";
    el.style.pointerEvents = "";
  }
}

export function getDetailButtonState(modId: string): BadgeStatus {
  if (window.__pxmodrim.installedIds.has(modId)) return BadgeState.INSTALLED;
  if (window.__pxmodrim.checkedIds.has(modId)) return BadgeState.CHECKED;
  return BadgeState.DEFAULT;
}

export function getDepBadgeState(modId: string): BadgeStatus {
  return getDetailButtonState(modId);
}
