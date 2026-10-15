import type { DepNode, DepItemsResult } from "./types";
import { Config } from "./types";
import { log, warn, parseHTML, scrapeDepsFromContainer } from "./utils";
import { PxModRimAPI } from "./api";

export const DepState: {
  cache: Map<string, DepNode>;
  fetching: Set<string>;
  aborters: Map<string, AbortController>;
} = {
  cache: new Map(),
  fetching: new Set(),
  aborters: new Map(),
};

const _resolveTreeLocks = new Set<string>();
const _fetchResolvers = new Map<string, Array<() => void>>();

function convertItemsToTree(
  items: Record<string, { id: string; title: string; deps?: string[] }>,
  rootId: string,
  maxDepth = Config.DEPTH_MAX,
): DepNode {
  function buildNode(
    id: string,
    depth: number,
    seen: Set<string>,
  ): DepNode {
    if (seen.has(id)) {
      return { id, title: items[id]?.title || `Mod ${id}`, deps: [] };
    }
    if (depth >= maxDepth) {
      const item = items[id];
      if (!item) return { id, title: `Mod ${id}`, deps: [] };
      return { id: item.id, title: item.title || `Mod ${id}`, deps: [] };
    }
    seen.add(id);
    const item = items[id];
    if (!item) return { id, title: `Mod ${id}`, deps: [] };
    return {
      id: item.id,
      title: item.title || `Mod ${id}`,
      deps: (item.deps || []).map((depId) =>
        buildNode(depId, depth + 1, new Set(seen)),
      ),
    };
  }
  return buildNode(rootId, 0, new Set());
}

const apiStrategy = {
  name: "api",
  async fetch(modId: string): Promise<DepNode | null> {
    try {
      const result = await PxModRimAPI.fetchModDeps(modId);
      if (!result || !result.items || !result.rootId) {
        warn(`apiStrategy: invalid response for ${modId}`, result);
        return null;
      }
      if (!result.isComplete) {
        warn(`apiStrategy: incomplete for ${modId}`);
        return null;
      }
      log(`apiStrategy: complete for ${modId}`);
      return convertItemsToTree(result.items, result.rootId);
    } catch (e) {
      warn(`apiStrategy: failed for ${modId}:`, e);
      return null;
    }
  },
};

const domStrategy = {
  name: "dom",
  async fetch(modId: string): Promise<DepNode | null> {
    log(`domStrategy: building tree for ${modId}`);
    return await buildDomDepTree(modId, 0, new Set());
  },
};

async function buildDomDepTree(
  modId: string,
  depth: number,
  seen: Set<string>,
): Promise<DepNode | null> {
  if (depth >= Config.DEPTH_MAX) {
    log(`buildDomDepTree depth limit for ${modId}`);
    return null;
  }
  if (seen.has(modId)) {
    log(`buildDomDepTree circular for ${modId}`);
    return null;
  }
  seen.add(modId);

  let tree = DepState.cache.get(modId);
  if (tree) {
    log(`buildDomDepTree cache hit for ${modId}`);
    return tree;
  }

  if (DepState.fetching.has(modId)) {
    log(`buildDomDepTree waiting for ${modId}`);
    await new Promise<void>((resolve) => {
      const resolvers = _fetchResolvers.get(modId) || [];
      resolvers.push(resolve);
      _fetchResolvers.set(modId, resolvers);
    });
    tree = DepState.cache.get(modId);
    if (tree) return tree;
  }

  DepState.fetching.add(modId);
  const controller = new AbortController();
  DepState.aborters.set(modId, controller);
  try {
    const resp = await fetch(
      `https://steamcommunity.com/sharedfiles/filedetails/?id=${modId}`,
      { signal: controller.signal },
    );
    if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
    const html = await resp.text();
    const doc = parseHTML(html);

    const titleEl = doc.querySelector(".workshopItemTitle");
    const title = titleEl
      ? titleEl.textContent?.trim() ?? `Mod ${modId}`
      : `Mod ${modId}`;

    const container = doc.getElementById("RequiredItems");
    const deps = container ? scrapeDepsFromContainer(container) : [];

    tree = { id: modId, title, deps: [] };
    for (const dep of deps) {
      const child = await buildDomDepTree(dep.id, depth + 1, new Set(seen));
      if (child) tree.deps.push(child);
      else tree.deps.push({ id: dep.id, title: dep.title, deps: [] });
    }

    DepState.cache.set(modId, tree);
    return tree;
  } catch (e) {
    if ((e as Error).name === "AbortError") return null;
    warn(`DOM dep fetch failed for ${modId}:`, e);
    return null;
  } finally {
    DepState.fetching.delete(modId);
    DepState.aborters.delete(modId);
    const resolvers = _fetchResolvers.get(modId);
    if (resolvers) {
      _fetchResolvers.delete(modId);
      resolvers.forEach((r) => r());
    }
  }
}

export async function getDepsFor(modId: string): Promise<DepNode | null> {
  const cached = DepState.cache.get(modId);
  if (cached) {
    log(`getDepsFor cache hit for ${modId}`);
    return cached;
  }

  if (_resolveTreeLocks.has(modId)) {
    log(`getDepsFor waiting for concurrent fetch of ${modId}`);
    while (_resolveTreeLocks.has(modId)) {
      await new Promise((r) => setTimeout(r, 50));
    }
    return DepState.cache.get(modId) || null;
  }

  _resolveTreeLocks.add(modId);
  try {
    for (const strategy of [apiStrategy, domStrategy]) {
      try {
        log(`getDepsFor trying ${strategy.name} for ${modId}`);
        const tree = await strategy.fetch(modId);
        if (tree) {
          log(`getDepsFor ${strategy.name} succeeded for ${modId}`);
          DepState.cache.set(modId, tree);
          return tree;
        }
        log(`getDepsFor ${strategy.name} returned null for ${modId}`);
      } catch (e) {
        warn(`${strategy.name} strategy failed for ${modId}:`, e);
      }
    }
    warn(`getDepsFor all strategies failed for ${modId}`);
    return null;
  } finally {
    _resolveTreeLocks.delete(modId);
  }
}

export function flattenDepTree(
  node: DepNode,
  seen: Set<string>,
): { id: string; title: string }[] {
  const result: { id: string; title: string }[] = [];
  if (!node.deps) return result;
  for (const dep of node.deps) {
    if (seen.has(dep.id)) continue;
    if (window.__pxmodrim.installedIds.has(dep.id)) continue;
    if (window.__pxmodrim.checkedIds.has(dep.id)) continue;
    seen.add(dep.id);
    result.push({ id: dep.id, title: dep.title });
    result.push(...flattenDepTree(dep, new Set(seen)));
  }
  return result;
}

export function cancelPendingDepFetches(): void {
  DepState.aborters.forEach((controller) => controller.abort());
  DepState.aborters.clear();
  DepState.fetching.clear();
}
