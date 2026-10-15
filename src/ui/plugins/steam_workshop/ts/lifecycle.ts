import { Config } from "./types";
import { log, findItemCard } from "./utils";
import {
  refreshAllDepsBadges,
  scheduleBadgeUpdate,
  updateAllModBadges,
} from "./badges";
import { createDetailButton, DetailState } from "./detail";
import { initCollectionPage, updateCollectionBadges } from "./collection";
import { setBridgeDataReady } from "./bridge";
import { PxModRimAPI } from "./api";

type PageKindKey = "collection" | "mod-details" | "grid";

interface PageKind {
  init(): void;
  onMutation(mutations: MutationRecord[]): void;
}

let _pageKind: PageKindKey | null = null;

function handleGridMutations(mutations: MutationRecord[]): void {
  for (const m of mutations) {
    for (const node of m.addedNodes) {
      if (node.nodeType !== Node.ELEMENT_NODE) continue;
      const el = node as Element;
      const link = el.querySelector<HTMLAnchorElement>('a[href*="sharedfiles/filedetails/?id="]');
      if (!link || !el.querySelector("img")) continue;
      if (!findItemCard(link)) continue;
      scheduleBadgeUpdate();
      return;
    }
  }
}

function handleCollectionMutations(mutations: MutationRecord[]): void {
  for (const m of mutations) {
    for (const node of m.addedNodes) {
      if (node.nodeType !== Node.ELEMENT_NODE) continue;
      const el = node as Element;
      if (el.matches?.(".collectionItem") || el.querySelector?.(".collectionItem")) {
        updateCollectionBadges();
        return;
      }
    }
  }
}

function handleModDetailMutations(mutations: MutationRecord[]): void {
  const RELEVANT_IDS = new Set([
    "SubscribeItemBtn",
    "RequiredItems",
    "pxmodrim-subscribe-btn",
    Config.DEP_SECTION_ID,
    "pxmodrim-loading-container",
    "pxmodrim-solo-link",
  ]);
  const hasRelevant = mutations.some((m) => {
    if (m.type === "childList") {
      for (const list of [m.addedNodes, m.removedNodes]) {
        for (const node of list) {
          if (node.nodeType !== Node.ELEMENT_NODE) continue;
          const el = node as Element;
          if (RELEVANT_IDS.has(el.id)) return true;
          if (el.querySelector?.("[id]")) {
            const nested = el.querySelector(
              "#SubscribeItemBtn,#RequiredItems,#pxmodrim-subscribe-btn,#" +
                CSS.escape(Config.DEP_SECTION_ID),
            );
            if (nested) return true;
          }
        }
      }
    }
    if (m.type === "attributes" && RELEVANT_IDS.has((m.target as Element).id)) return true;
    return false;
  });
  if (!hasRelevant) return;

  const currentUrl = window.location.href;
  if (document.getElementById("SubscribeItemBtn") && !document.getElementById("pxmodrim-subscribe-btn")) {
    createDetailButton();
    return;
  }
  if (DetailState.injectedUrl !== currentUrl) return;
  if (
    document.getElementById("RequiredItems") &&
    !document.getElementById(Config.DEP_SECTION_ID) &&
    !DetailState.depsInjected
  ) {
    createDetailButton();
  }
}

const PAGE_KINDS: Record<PageKindKey, PageKind> = {
  collection: {
    init: initCollectionPage,
    onMutation: handleCollectionMutations,
  },
  "mod-details": {
    init: createDetailButton,
    onMutation: handleModDetailMutations,
  },
  grid: {
    init: updateAllModBadges,
    onMutation: handleGridMutations,
  },
};

function resolveKind(mutations: MutationRecord[]): PageKindKey | null {
  for (const m of mutations) {
    for (const node of m.addedNodes) {
      if (node.nodeType !== Node.ELEMENT_NODE) continue;
      const el = node as Element;
      if (el.matches?.(".collectionChildren") || el.querySelector?.(".collectionChildren")) {
        return "collection";
      }
      if (el.matches?.("#SubscribeItemBtn") || el.querySelector?.("#SubscribeItemBtn")) {
        return "mod-details";
      }
    }
  }
  return null;
}

function activateKind(kind: PageKindKey): void {
  _pageKind = kind;
  PAGE_KINDS[kind].init();
}

function runDomCleanup() {
  const footer = document.getElementById("footer");
  footer
    ?.querySelectorAll<HTMLElement>("[style*='--grid-area']")
    .forEach((el) => {
      const area = el.style.getPropertyValue("--grid-area");
      if (area && area !== "main") el.remove();
    });
}

function injectStyles() {
  const style = document.createElement("style");
  style.id = "pxmodrim-styles";
  style.textContent = CSS_STYLES;
  document.documentElement.appendChild(style);
}

function initObservers() {
  log("initObservers START");
  const isSharedFile = window.location.href.startsWith(
    "https://steamcommunity.com/sharedfiles/filedetails/?id=",
  );
  if (!isSharedFile) {
    activateKind("grid");
    log("initObservers grid page");
  }

  const mainObserver = new MutationObserver((mutations) => {
    if (!_pageKind) {
      const kind = resolveKind(mutations);
      if (kind) {
        activateKind(kind);
        log("initObservers resolved pageKind=%s", kind);
      }
    }
    if (_pageKind) PAGE_KINDS[_pageKind].onMutation(mutations);
    runDomCleanup();
  });

  mainObserver.observe(document.documentElement, {
    childList: true,
    subtree: true,
  });
  log("initObservers OBSERVER ATTACHED");

  runDomCleanup();

  if (!_pageKind) {
    if (document.querySelector(".collectionChildren")) {
      activateKind("collection");
      log("initObservers immediate: collection");
    } else if (document.querySelector("#SubscribeItemBtn")) {
      activateKind("mod-details");
      log("initObservers immediate: mod-details");
    }
  }

  log("initObservers DONE pageKind=%s", _pageKind);
}

function initState() {
  log("initState ENTER");
  log(
    "initState installed=" +
      window.__pxmodrim.installedIds.size +
      " checked=" +
      window.__pxmodrim.checkedIds.size,
  );

  setBridgeDataReady();
  if (_pageKind) PAGE_KINDS[_pageKind].init();
  refreshAllDepsBadges();
  PxModRimAPI.initReady();
  log("initState DONE");
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
  log("startScript ENTER");
  injectStyles();
  log("Base styles injected at Document Creation. Starting lifecycle...");
  initObservers();
  initState();
  log("startScript DONE");
}

export function isPageKind(kind: PageKindKey): boolean {
  return _pageKind === kind;
}

export { startScript, waitForRoot };
