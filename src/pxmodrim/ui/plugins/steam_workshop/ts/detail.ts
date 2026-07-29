import { BadgeState, Config } from "./types";
import { log, warn } from "./utils";
import {
  setDetailBtnVisuals,
  getDetailButtonState,
} from "./visuals";
import { getDepsFor, flattenDepTree } from "./deps";
import { refreshAllDepsBadges, updateAllModBadges } from "./badges";
import { PxModRimAPI } from "./api";
import {
  waitForBridgeData,
  isBridgeDataReady,
} from "./bridge";
import { buildDepSection } from "./rendering";

export const DetailState: {
  injectedUrl: string | null;
  resolvingDeps: boolean;
  depsInjected: boolean;
} = {
  injectedUrl: null,
  resolvingDeps: false,
  depsInjected: false,
};

let _depsLoading = false;

export async function createDetailButton(): Promise<void> {
  if ((createDetailButton as Record<string, unknown>)._running) return;
  (createDetailButton as Record<string, unknown>)._running = true;
  try {
    log("createDetailButton ENTER url=" + window.location.href);
    const currentUrl = window.location.href;
    if (DetailState.injectedUrl !== currentUrl) {
      DetailState.injectedUrl = currentUrl;
      DetailState.depsInjected = false;
      document.getElementById("pxmodrim-subscribe-btn")?.remove();
      document.getElementById(Config.DEP_SECTION_ID)?.remove();
      document.getElementById("pxmodrim-loading-container")?.remove();
    }

    let btn = document.getElementById(
      "pxmodrim-subscribe-btn",
    ) as HTMLAnchorElement | null;
    let container: HTMLElement | null = null;

    if (btn) {
      const modId = (
        window.location.href.match(/[?&]id=(\d+)/) || []
      )[1];
      if (modId) setDetailBtnVisuals(btn, getDetailButtonState(modId));
      updateSoloLinkVisibility();
      container =
        btn.closest("div[style*='flex']") || btn.parentElement;
    } else if (document.getElementById("pxmodrim-loading-container")) {
      return;
    } else {
      const steamBtn = document.getElementById("SubscribeItemBtn");
      if (!steamBtn) return;

      const modId = (
        window.location.href.match(/[?&]id=(\d+)/) || []
      )[1];
      if (!modId) return;

      const h1 = document.querySelector(
        ".game_area_purchase_game h1",
      );
      const title = h1
        ? h1.textContent
            ?.replace(/Subscribe to download\s*/i, "")
            .trim() || ""
        : "";
      const hasDeps = !!document.getElementById("RequiredItems");

      if (isBridgeDataReady()) {
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
          (loadingEl: HTMLElement, ctr: HTMLElement) => {
            finalizeButton(loadingEl, ctr, modId, title, hasDeps);
            createDetailButton();
          },
          () => waitForBridgeData(10000),
        );
        steamBtn.replaceWith(container);
        return;
      }
    }

    if (!document.getElementById("RequiredItems")) return;

    const modId = (
      window.location.href.match(/[?&]id=(\d+)/) || []
    )[1];
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
    loading.innerHTML =
      '<div class="rimsort-dep-spinner"></div><span>Resolving dependencies...</span>';
    container?.insertAdjacentElement("afterend", loading);

    try {
      const tree = await getDepsFor(modId);

      loading.remove();
      const section = buildDepSection(tree);
      if (section && container) {
        container.insertAdjacentElement("afterend", section);
        DetailState.depsInjected = true;
      }
      updateSoloLinkVisibility();
    } finally {
      _depsLoading = false;
    }
  } finally {
    (createDetailButton as Record<string, unknown>)._running = false;
  }
}

async function queueModWithDeps(
  modId: string,
  title: string,
): Promise<void> {
  if (DetailState.resolvingDeps) return;
  DetailState.resolvingDeps = true;

  const btn = document.getElementById(
    "pxmodrim-subscribe-btn",
  ) as HTMLElement | null;
  if (btn) {
    btn.classList.add("rimsort-deps-resolving");
    btn.textContent = "Resolving deps...";
  }

  window.__pxmodrim.checkedIds.add(modId);

  const toggles: { id: string; title: string }[] = [
    { id: modId, title },
  ];
  const tree = await getDepsFor(modId);
  if (tree) {
    const allDeps = flattenDepTree(tree, new Set([modId]));
    for (const dep of allDeps) {
      window.__pxmodrim.checkedIds.add(dep.id);
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

function queueModSolo(modId: string, title: string): void {
  window.__pxmodrim.checkedIds.add(modId);
  PxModRimAPI.toggleDownloadChecked(modId, title, true);
  const btn = document.getElementById(
    "pxmodrim-subscribe-btn",
  ) as HTMLElement | null;
  if (btn) setDetailBtnVisuals(btn, getDetailButtonState(modId));
  refreshAllDepsBadges();
  updateSoloLinkVisibility();
}

export function batchToggleChecked(
  mods: { id: string; title: string }[],
  checked: boolean,
): void {
  if (!mods.length) return;
  PxModRimAPI.batchToggleDownloadChecked(
    mods.map((m) => m.id),
    mods.map((m) => m.title),
    checked,
  );
}

function updateSoloLinkVisibility(): void {
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

function createLoadingPlaceholder(
  loadingFactory: () => HTMLElement,
  afterLoadedFactory: (
    loadingEl: HTMLElement,
    container: HTMLElement,
  ) => void,
  event: () => Promise<void>,
): HTMLElement {
  const loadingEl = loadingFactory();
  const container = document.createElement("div");
  container.id = "pxmodrim-loading-container";
  container.style.display = "flex";
  container.style.flexDirection = "column";
  container.style.gap = "8px";
  container.appendChild(loadingEl);

  event().then(
    () => {
      if (!document.contains(container)) return;
      afterLoadedFactory(loadingEl, container);
    },
    () => {
      if (!document.contains(container)) return;
      warn("Bridge data timeout \u2014 showing error state");
      loadingEl.className = "rimsort-error-btn";
      loadingEl.textContent = "ERROR";
    },
  );

  return container;
}

function finalizeButton(
  btn: HTMLElement,
  container: HTMLElement,
  modId: string,
  title: string,
  hasDeps: boolean,
): void {
  btn.id = "pxmodrim-subscribe-btn";
  btn.className = "rimsort-detail-btn";
  btn.style.textAlign = "center";

  function updateButtonVisuals(): void {
    setDetailBtnVisuals(
      btn,
      getDetailButtonState(modId),
      hasDeps ? "Add to Queue (with deps)" : "Add to Queue",
    );
  }

  btn.addEventListener("click", function (e) {
    e.preventDefault();
    e.stopPropagation();
    if (btn.classList.contains("rimsort-mod-installed")) return;
    if (DetailState.resolvingDeps) return;

    btn.classList.add("pressed");
    requestAnimationFrame(() => btn.classList.remove("pressed"));

    const state = getDetailButtonState(modId);
    if (state === "checked") {
      window.__pxmodrim.checkedIds.delete(modId);
      updateButtonVisuals();
      PxModRimAPI.toggleDownloadChecked(modId, "", false);
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
      if (btn.classList.contains("rimsort-mod-installed")) return;
      if (DetailState.resolvingDeps) return;
      queueModSolo(modId, title);
    });
    container.appendChild(soloLink);
    updateSoloLinkVisibility();
  }
}
