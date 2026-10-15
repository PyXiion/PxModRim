import { ModState, type ModStatus } from "./types";
import { log, warn } from "./utils";
import { getDepsFor, flattenDepTree } from "./deps";
import { PxModRimAPI } from "./api";

export function getModState(modId: string): ModStatus {
  if (window.__pxmodrim.activeIds.has(modId)) return ModState.ACTIVE;
  if (window.__pxmodrim.installedIds.has(modId)) return ModState.INACTIVE;
  if (window.__pxmodrim.checkedIds.has(modId)) return ModState.CHECKED;
  return ModState.DEFAULT;
}

export function applyModState(el: HTMLElement, state: ModStatus): void {
  if (el.dataset.state === state && state !== ModState.RESOLVING) return;
  el.className = el.className
    .split(" ")
    .filter((c) => !c.startsWith("rimsort-mod-"))
    .join(" ");
  el.classList.add(`rimsort-mod-${state}`);
  el.dataset.state = state;
  el.style.pointerEvents = "";

  if (state === ModState.RESOLVING) {
    el.title = "Resolving dependencies\u2026";
    el.innerHTML =
      '<div class="rimsort-dep-spinner" style="width:14px;height:14px;border-width:2px;"></div>';
    el.style.pointerEvents = "none";
    return;
  }

  if (el.dataset.rimsortVariant === "detail") {
    switch (state) {
      case ModState.ACTIVE:
        el.title = "Active \u2014 click to deactivate";
        el.textContent = "\u2713 Active";
        break;
      case ModState.INACTIVE:
        el.title = "Inactive \u2014 click to activate";
        el.textContent = "\u2298 Inactive";
        break;
      case ModState.CHECKED:
        el.title = "In download queue \u2014 click to remove";
        el.textContent = "\u2713 In Queue";
        break;
      default:
        el.title = el.dataset.rimsortTitle ?? "Add to download queue";
        el.textContent = el.dataset.rimsortLabel || "Add to Queue";
        break;
    }
  } else {
    switch (state) {
      case ModState.ACTIVE:
        el.title = "Active \u2014 click to deactivate";
        el.textContent = "\u2713";
        break;
      case ModState.INACTIVE:
        el.title = "Inactive \u2014 click to activate";
        el.textContent = "\u2298";
        break;
      case ModState.CHECKED:
        el.title = "In download queue \u2014 click to remove";
        el.textContent = "\u2212";
        break;
      default:
        el.title = "Add to download queue";
        el.textContent = "+";
        break;
    }
  }
}

export async function performModAction(
  modId: string,
  getTitle: () => string,
  opts?: { skipDeps?: boolean },
): Promise<void> {
  const state = getModState(modId);

  switch (state) {
    case ModState.ACTIVE:
      window.__pxmodrim.activeIds.delete(modId);
      try {
        await PxModRimAPI.toggleActive(modId, false);
      } catch (e) {
        window.__pxmodrim.activeIds.add(modId);
        throw e;
      }
      break;
    case ModState.INACTIVE:
      window.__pxmodrim.activeIds.add(modId);
      try {
        await PxModRimAPI.toggleActive(modId, true);
      } catch (e) {
        window.__pxmodrim.activeIds.delete(modId);
        throw e;
      }
      break;
    case ModState.CHECKED:
      window.__pxmodrim.checkedIds.delete(modId);
      try {
        await PxModRimAPI.toggleDownloadChecked(modId, "", false);
      } catch (e) {
        window.__pxmodrim.checkedIds.add(modId);
        throw e;
      }
      break;
    case ModState.DEFAULT: {
      const title = getTitle();
      const addedChecked = new Set<string>();
      addedChecked.add(modId);
      window.__pxmodrim.checkedIds.add(modId);
      try {
        if (opts?.skipDeps) {
          await PxModRimAPI.toggleDownloadChecked(modId, title, true);
        } else {
          const toggles: { id: string; title: string }[] = [{ id: modId, title }];
          const tree = await getDepsFor(modId);
          if (tree) {
            for (const dep of flattenDepTree(tree, new Set([modId]))) {
              addedChecked.add(dep.id);
              window.__pxmodrim.checkedIds.add(dep.id);
              toggles.push(dep);
            }
          } else {
            warn(`Failed to resolve deps for ${modId}`);
          }
          await PxModRimAPI.batchToggleDownloadChecked(
            toggles.map((m) => m.id),
            toggles.map((m) => m.title),
            true,
          );
        }
      } catch (e) {
        for (const id of addedChecked) window.__pxmodrim.checkedIds.delete(id);
        throw e;
      }
      break;
    }
    case ModState.RESOLVING:
      break;
  }
}

export async function handleClick(
  el: HTMLElement,
  modId: string,
  getTitle: () => string,
  opts?: { skipDeps?: boolean },
): Promise<void> {
  const state = getModState(modId);
  if (state === ModState.RESOLVING) return;

  el.classList.add("pressed");
  requestAnimationFrame(() => el.classList.remove("pressed"));

  if (state === ModState.DEFAULT) {
    applyModState(el, ModState.RESOLVING);
  }

  try {
    await performModAction(modId, getTitle, opts);
  } finally {
    applyModState(el, getModState(modId));
  }
}
