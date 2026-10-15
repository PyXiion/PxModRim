import { startScript, waitForRoot } from "./lifecycle";
import {
  __pxmSetInstalled,
  __pxmSetActive,
  __pxmUncheckMod,
  __pxmClearChecked,
  initRPC,
} from "./bridge";
import { initAPI } from "./api";
import { updateAllModBadges, updateModBadge } from "./badges";

if (!window.__pxmodrimInited) {
  window.__pxmodrimInited = true;

  window.__pxmSetInstalled = __pxmSetInstalled;
  window.__pxmSetActive = __pxmSetActive;
  window.__pxmUncheckMod = __pxmUncheckMod;
  window.__pxmClearChecked = __pxmClearChecked;
  window.updateAllModBadges = updateAllModBadges;
  window.updateModBadge = updateModBadge;

  (async () => {
    try {
      const rpc = await initRPC();
      initAPI(rpc);
    } catch (e) {
      console.error("[pxmodrim] QWebChannel init failed:", e);
      return;
    }

    if (document.documentElement) {
      startScript();
    } else {
      waitForRoot();
    }
  })();
}
