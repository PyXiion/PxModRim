import { BadgeState, Config } from "./types";
import { log } from "./utils";
import {
  refreshAllDepsBadges,
  scheduleBadgeUpdate,
  updateAllModBadges,
} from "./badges";
import { createDetailButton, DetailState } from "./detail";
import { setBridgeDataReady } from "./bridge";
import { PxModRimAPI } from "./api";

export let _activeRoute: {
  name: string;
  match: (url: string) => boolean;
  init: () => void;
  onMutation: (mutations: MutationRecord[]) => void;
} | null = null;

const ROUTES = [
  {
    name: "details",
    match: (url: string) =>
      url.startsWith(
        "https://steamcommunity.com/sharedfiles/filedetails/?id=",
      ),
    init() {
      createDetailButton();
    },
    onMutation(mutations: MutationRecord[]) {
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
        if (
          m.type === "attributes" &&
          RELEVANT_IDS.has((m.target as Element).id)
        )
          return true;
        return false;
      });
      if (!hasRelevant) return;

      const currentUrl = window.location.href;
      if (
        document.getElementById("SubscribeItemBtn") &&
        !document.getElementById("pxmodrim-subscribe-btn")
      ) {
        createDetailButton();
        return;
      }
      if (DetailState.injectedUrl !== currentUrl) {
        return;
      }
      if (
        document.getElementById("RequiredItems") &&
        !document.getElementById(Config.DEP_SECTION_ID) &&
        !DetailState.depsInjected
      ) {
        createDetailButton();
      }
    },
  },
  {
    name: "grid",
    match: () => true,
    init() {
      updateAllModBadges();
    },
    onMutation(mutations: MutationRecord[]) {
      for (const m of mutations) {
        for (const node of m.addedNodes) {
          if (node.nodeType !== Node.ELEMENT_NODE) continue;
          const el = node as Element;
          if (
            el.querySelector('a[href*="sharedfiles/filedetails/?id="]') &&
            el.querySelector("img")
          ) {
            scheduleBadgeUpdate();
            return;
          }
        }
      }
    },
  },
];

function getActiveRoute() {
  return ROUTES.find((r) => r.match(window.location.href)) ?? null;
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
  _activeRoute = getActiveRoute();
  log(
    "Active route:",
    _activeRoute?.name,
    _activeRoute?.match(window.location.href),
  );

  const mainObserver = new MutationObserver((mutations) => {
    _activeRoute?.onMutation(mutations);
    runDomCleanup();
  });

  mainObserver.observe(document.documentElement, {
    childList: true,
    subtree: true,
  });
  log("initObservers OBSERVER ATTACHED");

  runDomCleanup();
  log("initObservers CALLING _activeRoute.init()");
  _activeRoute?.init();
  log("initObservers DONE");
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
  _activeRoute?.init();
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

export { startScript, waitForRoot };
