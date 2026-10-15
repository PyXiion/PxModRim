import "./qwebchannel.js";
import { getModState, applyModState } from "./controls";
import { refreshAllDepsBadges, updateAllModBadges } from "./badges";
import { log } from "./utils";

// Ensure __pxmodrim exists (modules may initialize before main.ts guard)
if (!window.__pxmodrim) {
  window.__pxmodrim = {
    installedIds: new Set(),
    checkedIds: new Set(),
    activeIds: new Set(),
    onStateChange: null,
  };
}

let _bridgeDataReady = false;
let _bridgeDataResolve: (() => void) | null = null;
let _bridgeDataReject: ((reason: Error) => void) | null = null;
let _bridgeDataPromise: Promise<void> | null = null;

export function isBridgeDataReady(): boolean {
  return _bridgeDataReady;
}

export function setBridgeDataReady(): void {
  _bridgeDataReady = true;
  _bridgeDataResolve?.();
}

export function waitForBridgeData(timeoutMs = 15000): Promise<void> {
  if (!_bridgeDataPromise) {
    _bridgeDataPromise = new Promise<void>((resolve, reject) => {
      _bridgeDataResolve = resolve;
      _bridgeDataReject = reject;
      setTimeout(() => {
        _bridgeDataPromise = null;
        _bridgeDataResolve = null;
        _bridgeDataReject = null;
        reject(new Error("Bridge data timeout"));
      }, timeoutMs);
    });
  }
  if (_bridgeDataReady) {
    _bridgeDataResolve?.();
  }
  return _bridgeDataPromise;
}

// ── QWebChannel RPC ────────────────────────────────────

export class PxModRimRPC {
  private bridge: any;
  private pending = new Map<string, (r: Record<string, any>) => void>();

  constructor(channel: any) {
    this.bridge = channel.objects.bridge;
    this.bridge.result_ready.connect((id: string, json: string) => {
      const resolve = this.pending.get(id);
      if (resolve) {
        this.pending.delete(id);
        resolve(JSON.parse(json));
      }
    });
  }

  call(
    method: string,
    payload: Record<string, any> = {},
  ): Promise<Record<string, any>> {
    return new Promise(resolve => {
      const id = crypto.randomUUID();
      this.pending.set(id, resolve);
      this.bridge.call(id, method, JSON.stringify(payload));
    });
  }
}

export function initRPC(): Promise<PxModRimRPC> {
  return new Promise(resolve => {
    const qt = (window as any).qt;
    if (qt && qt.webChannelTransport) {
      new (window as any).QWebChannel(
        qt.webChannelTransport,
        (channel: any) => {
          resolve(new PxModRimRPC(channel));
        },
      );
    } else {
      setTimeout(() => initRPC().then(resolve), 100);
    }
  });
}

// ── Python integration points ─────────────────────────────────────

export function __pxmSetInstalled(modIds: string[]): void {
  window.__pxmodrim.installedIds.clear();
  (modIds || []).forEach((id) => window.__pxmodrim.installedIds.add(id));
  if (_bridgeDataReady) {
    updateAllModBadges();
    refreshAllDepsBadges();
    window.__pxmodrim.onStateChange?.();
  }
}

export function __pxmUncheckMod(modId: string): void {
  window.__pxmodrim.checkedIds.delete(modId);
  window.updateModBadge(modId);
  refreshAllDepsBadges();
  window.__pxmodrim.onStateChange?.();
}

export function __pxmClearChecked(): void {
  window.__pxmodrim.checkedIds.clear();
  updateAllModBadges();
  refreshAllDepsBadges();
  window.__pxmodrim.onStateChange?.();
}

export function __pxmSetActive(modIds: string[]): void {
  window.__pxmodrim.activeIds.clear();
  (modIds || []).forEach((id) => window.__pxmodrim.activeIds.add(id));
  if (_bridgeDataReady) {
    updateAllModBadges();
    refreshAllDepsBadges();
    window.__pxmodrim.onStateChange?.();
  }
}

// Render detail page button on state changes from Python
window.__pxmodrim.onStateChange = function () {
  const modId = (window.location.href.match(/[?&]id=(\d+)/) || [])[1];
  const btn = document.getElementById("pxmodrim-subscribe-btn");
  if (btn && modId) {
    applyModState(btn, getModState(modId));
  }
  refreshAllDepsBadges();
};
