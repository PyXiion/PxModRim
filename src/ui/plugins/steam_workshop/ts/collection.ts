import { ModState, type ModStatus } from "./types";
import { log, warn, getModId } from "./utils";
import { getModState, applyModState, handleClick } from "./controls";
import { PxModRimAPI } from "./api";

function scrapeChildIds(): string[] {
  const ids: string[] = [];
  document.querySelectorAll('[id^="sharedfile_"]').forEach((el) => {
    const id = el.id.replace("sharedfile_", "");
    if (id) ids.push(id);
  });
  return ids;
}

function getCollectionState(): ModStatus {
  const ids = scrapeChildIds();
  if (!ids.length) return ModState.DEFAULT;

  const installed = window.__pxmodrim.installedIds;
  const checked = window.__pxmodrim.checkedIds;
  const active = window.__pxmodrim.activeIds;

  if (ids.some((id) => checked.has(id))) return ModState.CHECKED;
  if (ids.some((id) => !installed.has(id) && !checked.has(id))) return ModState.DEFAULT;
  if (ids.every((id) => active.has(id))) return ModState.ACTIVE;
  return ModState.INACTIVE;
}

function addBatchBar(childrenEl: Element): void {
  const existing = document.querySelector(".rimsort-collection-bar");
  if (existing) return;

  const bar = document.createElement("div");
  bar.className = "rimsort-collection-bar";

  let resolving = false;

  const btn = document.createElement("button");
  btn.className = "rimsort-detail-btn";
  btn.dataset.rimsortVariant = "detail";

  btn.addEventListener("click", async () => {
    if (resolving) return;
    resolving = true;

    const state = getCollectionState();

    btn.classList.add("pressed");
    requestAnimationFrame(() => btn.classList.remove("pressed"));

    if (state === ModState.DEFAULT) {
      applyModState(btn, ModState.RESOLVING);
    }

    try {
      switch (state) {
        case ModState.DEFAULT: {
          const collectionId = new URL(window.location.href).searchParams.get("id");
          if (!collectionId) return;

          const result = await PxModRimAPI.fetchModDeps(collectionId);
          if (!result?.items) return;

          const idsToQueue = new Set<string>();
          const titles: Record<string, string> = {};
          const installed = window.__pxmodrim.installedIds;
          const checked = window.__pxmodrim.checkedIds;

          for (const item of Object.values(result.items)) {
            if (!installed.has(item.id) && !checked.has(item.id)) {
              idsToQueue.add(item.id);
              titles[item.id] = item.title || item.id;
            }
            for (const depId of item.deps || []) {
              if (!installed.has(depId) && !checked.has(depId)) {
                idsToQueue.add(depId);
                titles[depId] = result.items[depId]?.title || depId;
              }
            }
          }

          const queueList = [...idsToQueue];
          if (queueList.length) {
            for (const id of queueList) window.__pxmodrim.checkedIds.add(id);
            await PxModRimAPI.batchToggleDownloadChecked(
              queueList,
              queueList.map((id) => titles[id] || id),
              true,
            );
          }
          break;
        }
        case ModState.CHECKED: {
          const checkedIds = scrapeChildIds().filter((id) =>
            window.__pxmodrim.checkedIds.has(id),
          );
          if (checkedIds.length) {
            for (const id of checkedIds) window.__pxmodrim.checkedIds.delete(id);
            await PxModRimAPI.batchToggleDownloadChecked(
              checkedIds,
              checkedIds.map(() => ""),
              false,
            );
          }
          break;
        }
        case ModState.INACTIVE: {
          const toActivate = scrapeChildIds().filter(
            (id) =>
              window.__pxmodrim.installedIds.has(id) &&
              !window.__pxmodrim.activeIds.has(id),
          );
          if (toActivate.length) {
            for (const id of toActivate) window.__pxmodrim.activeIds.add(id);
            await PxModRimAPI.batchToggleActive(toActivate, true);
          }
          break;
        }
        case ModState.ACTIVE: {
          const toDeactivate = scrapeChildIds().filter((id) =>
            window.__pxmodrim.activeIds.has(id),
          );
          if (toDeactivate.length) {
            for (const id of toDeactivate) window.__pxmodrim.activeIds.delete(id);
            await PxModRimAPI.batchToggleActive(toDeactivate, false);
          }
          break;
        }
      }
    } catch (e) {
      warn("Collection batch action failed", e);
    } finally {
      resolving = false;
      updateCollectionUI();
    }
  });

  const onlyBtn = document.createElement("button");
  onlyBtn.className = "rimsort-only-btn";
  onlyBtn.textContent = "Activate only this pack";
  onlyBtn.title = "Activate this modpack + deps, deactivate everything else";

  onlyBtn.addEventListener("click", async () => {
    if (resolving) return;
    resolving = true;

    const hasInstalled = scrapeChildIds().some((id) =>
      window.__pxmodrim.installedIds.has(id),
    );
    if (!hasInstalled) {
      resolving = false;
      return;
    }

    onlyBtn.disabled = true;
    onlyBtn.textContent = "Resolving...";

    try {
      const collectionId = new URL(window.location.href).searchParams.get("id");
      if (!collectionId) return;

      const result = await PxModRimAPI.fetchModDeps(collectionId);
      if (!result?.items) return;

      const packIds = new Set<string>();
      for (const item of Object.values(result.items)) {
        packIds.add(item.id);
        for (const depId of item.deps || []) packIds.add(depId);
      }

      const installedPack = [...packIds].filter((id) =>
        window.__pxmodrim.installedIds.has(id),
      );

      const toDeactivate = [...window.__pxmodrim.activeIds].filter(
        (id) => !installedPack.includes(id),
      );

      onlyBtn.textContent = "Applying...";

      if (toDeactivate.length) {
        for (const id of toDeactivate) window.__pxmodrim.activeIds.delete(id);
        await PxModRimAPI.batchToggleActive(toDeactivate, false);
      }

      const needActivate = installedPack.filter(
        (id) => !window.__pxmodrim.activeIds.has(id),
      );
      if (needActivate.length) {
        for (const id of needActivate) window.__pxmodrim.activeIds.add(id);
        await PxModRimAPI.batchToggleActive(needActivate, true);
      }
    } catch (e) {
      warn("Activate-only action failed", e);
    } finally {
      resolving = false;
      updateCollectionUI();
      onlyBtn.disabled = false;
      onlyBtn.textContent = "Activate only this pack";
    }
  });

  bar.appendChild(btn);
  bar.appendChild(onlyBtn);
  updateCollectionUI();
  childrenEl.before(bar);
}

function updateCollectionButton(btn: HTMLElement, state: ModStatus): void {
  applyModState(btn, state);
  switch (state) {
    case ModState.ACTIVE:
      btn.textContent = "✓ Deactivate all";
      btn.title = "Deactivate all active mods in this collection";
      break;
    case ModState.INACTIVE:
      btn.textContent = "⊗ Activate all";
      btn.title = "Activate all inactive mods in this collection";
      break;
  }
}

function updateCollectionUI(): void {
  document.querySelectorAll(".collectionItem .rimsort-modstatus-badge").forEach((badge) => {
    const modId = (badge as HTMLElement).dataset.modid;
    if (modId) applyModState(badge as HTMLElement, getModState(modId));
  });
  const btn = document.querySelector<HTMLElement>(".rimsort-collection-bar .rimsort-detail-btn");
  if (btn) updateCollectionButton(btn, getCollectionState());
}

function addChildBadges(childrenEl: Element): void {
  const items = childrenEl.querySelectorAll(".collectionItem");
  items.forEach((item) => {
    const link = item.querySelector<HTMLAnchorElement>(
      'a[href*="sharedfiles/filedetails/?id="]',
    );
    if (!link) return;
    const modId = getModId(link);
    if (!modId) return;

    const state = getModState(modId);

    let badge = item.querySelector<HTMLElement>(".rimsort-modstatus-badge");
    if (!badge) {
      badge = document.createElement("div");
      badge.className = "rimsort-modstatus-badge";
      badge.dataset.modid = modId;
      const titleEl = item.querySelector(".workshopItemTitle");
      badge.addEventListener("click", async (e) => {
        e.stopPropagation();
        e.preventDefault();
        await handleClick(badge!, modId, () => titleEl?.textContent?.trim() || "");
      });
      item.appendChild(badge);
    }
    applyModState(badge, state);
  });
}

export function initCollectionPage(): void {
  log("initCollectionPage ENTER");
  const container = document.querySelector(".collectionChildren");
  if (!container) return;
  addBatchBar(container);
  addChildBadges(container);
  log("initCollectionPage DONE");
}

export function updateCollectionBadges(): void {
  const container = document.querySelector(".collectionChildren");
  if (!container) return;
  addChildBadges(container);
  updateCollectionUI();
}
