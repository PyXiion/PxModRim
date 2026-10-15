import { ModState, Config } from "./types";
import { log, warn } from "./utils";
import { getModState, applyModState, handleClick, performModAction } from "./controls";
import { getDepsFor } from "./deps";
import { refreshAllDepsBadges } from "./badges";
import {
  waitForBridgeData,
  isBridgeDataReady,
} from "./bridge";
import { buildDepSection } from "./rendering";

export const DetailState: {
  injectedUrl: string | null;
  depsInjected: boolean;
} = {
  injectedUrl: null,
  depsInjected: false,
};

let _depsLoading = false;

function updateSoloLinkVisibility(): void {
  const soloLink = document.getElementById(Config.DEP_SOLO_LINK_ID);
  const requiredContainer = document.getElementById("RequiredItems");
  const btn = document.getElementById("pxmodrim-subscribe-btn");
  if (!soloLink) return;
  const isInstalled = btn?.classList.contains("rimsort-mod-active") || btn?.classList.contains("rimsort-mod-inactive");
  const isChecked = btn?.classList.contains("rimsort-mod-checked");
  if (requiredContainer && !isInstalled && !isChecked) {
    soloLink.classList.remove("hidden");
  } else {
    soloLink.classList.add("hidden");
  }
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

  btn.dataset.rimsortVariant = "detail";
  btn.dataset.rimsortLabel = hasDeps ? "Add to Queue (with deps)" : "Add to Queue";
  btn.dataset.rimsortTitle = "Add to download queue";

  function updateButtonVisuals(): void {
    applyModState(btn, getModState(modId));
  }

  btn.addEventListener("click", function (e) {
    e.preventDefault();
    e.stopPropagation();

    (async () => {
      await handleClick(btn, modId, () => title);
      updateButtonVisuals();
      updateSoloLinkVisibility();
    })();
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
      (async () => {
        await performModAction(modId, () => title, { skipDeps: true });
        updateButtonVisuals();
        updateSoloLinkVisibility();
      })();
    });
    container.appendChild(soloLink);
    updateSoloLinkVisibility();
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
      if (modId) {
        applyModState(btn, getModState(modId));
      }
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


