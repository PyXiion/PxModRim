import type { ModToggle } from "./types";

const DEBUG = false;

export const log = DEBUG
  ? console.log.bind(console, "[pxmodrim]")
  : () => {};
export const warn = console.warn.bind(console, "[pxmodrim]");

export function getModId(link: HTMLAnchorElement): string | null {
  const m = link.href.match(/[?&]id=(\d+)/);
  return m ? m[1] : null;
}

export function getModTitle(tile: Element): string {
  const links = tile.querySelectorAll<HTMLAnchorElement>(
    'a[href*="sharedfiles/filedetails/?id="]',
  );
  for (const link of links) {
    const text = link.textContent?.trim();
    if (text) return text;
  }
  return "";
}

export function findItemCard(link: HTMLAnchorElement): Element | null {
  let el = link.parentElement;
  while (el) {
    if (
      el.querySelector("img") &&
      el.querySelectorAll('a[href*="sharedfiles/filedetails/?id="]').length === 2
    ) {
      return el;
    }
    el = el.parentElement;
  }
  return null;
}

export function parseHTML(htmlText: string): Document {
  return new DOMParser().parseFromString(htmlText, "text/html");
}

export function scrapeDepsFromContainer(containerEl: Element): ModToggle[] {
  const deps: ModToggle[] = [];
  const links = containerEl.querySelectorAll<HTMLAnchorElement>(
    'a[href*="filedetails/?id="]',
  );
  links.forEach((link) => {
    const match = link.href.match(/[?&]id=(\d+)/);
    const modId = match ? match[1] : null;
    const titleEl = link.querySelector(".requiredItem");
    const title = titleEl
      ? titleEl.textContent?.trim()
      : link.textContent?.trim();
    if (modId) deps.push({ id: modId, title: title || `Mod ${modId}` });
  });
  return deps;
}
