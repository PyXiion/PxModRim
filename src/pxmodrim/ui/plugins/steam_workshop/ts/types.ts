export const BadgeState = {
  INSTALLED: "installed",
  CHECKED: "checked",
  DEFAULT: "default",
  RESOLVING: "resolving",
} as const;

export type BadgeStatus = (typeof BadgeState)[keyof typeof BadgeState];

export const Config = {
  DEPTH_MAX: 3,
  DEP_SECTION_ID: "pxmodrim-deps",
  DEP_SOLO_LINK_ID: "pxmodrim-solo-link",
} as const;

export interface ModToggle {
  id: string;
  title: string;
}

export interface DepNode {
  id: string;
  title: string;
  deps: DepNode[];
}

export interface DepItemsResult {
  items: Record<string, { id: string; title: string; deps?: string[] }>;
  rootId: string;
  isComplete: boolean;
}
