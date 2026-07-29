declare const CSS_STYLES: string;
declare const qt: { webChannelTransport: any };
declare class QWebChannel {
  constructor(transport: any, initCallback: (channel: any) => void);
}

interface PxmodrimState {
  installedIds: Set<string>;
  checkedIds: Set<string>;
  onStateChange: (() => void) | null;
}

interface Window {
  __pxmodrim: PxmodrimState;
  __pxmodrimInited?: boolean;
  QWebChannel: typeof QWebChannel;
  updateModBadge(modId: string, status: string): void;
  updateAllModBadges(): void;
  __pxmSetInstalled(modIds: string[]): void;
  __pxmUncheckMod(modId: string): void;
  __pxmClearChecked(): void;
}
