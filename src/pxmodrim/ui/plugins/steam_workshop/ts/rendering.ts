import { BadgeState, Config } from "./types";
import type { DepNode } from "./types";
import { scrapeDepsFromContainer } from "./utils";
import { setBadgeVisuals, getDepBadgeState } from "./visuals";
import { makeBadgeClickHandler } from "./badges";

export function buildDepSection(
  tree: DepNode | null,
): HTMLDivElement | null {
  const section = document.createElement("div");
  section.id = Config.DEP_SECTION_ID;
  section.className = "rimsort-deps-section";

  if (tree && tree.deps?.length) {
    const header = document.createElement("div");
    header.className = "rimsort-deps-header";
    header.textContent = `Dependencies (${tree.deps.length}):`;
    section.appendChild(header);
    const list = document.createElement("div");
    list.className = "rimsort-dep-list";
    renderDepTree(list, tree.deps, 0, new Set());
    section.appendChild(list);
    return section;
  }

  if (tree) {
    const header = document.createElement("div");
    header.className = "rimsort-deps-header";
    header.textContent = "No dependencies found";
    section.appendChild(header);
    const list = document.createElement("div");
    list.className = "rimsort-dep-list";
    const empty = document.createElement("div");
    empty.className = "rimsort-dep-empty";
    empty.textContent = "This mod has no required items";
    empty.style.padding = "8px";
    empty.style.color = "#888";
    list.appendChild(empty);
    section.appendChild(list);
    return section;
  }

  const required = document.getElementById("RequiredItems");
  if (!required) return null;
  const deps = scrapeDepsFromContainer(required);
  const header = document.createElement("div");
  header.className = "rimsort-deps-header";
  header.textContent = deps.length
    ? `Dependencies (${deps.length}):`
    : "No dependencies found";
  section.appendChild(header);
  const list = document.createElement("div");
  list.className = "rimsort-dep-list";
  if (deps.length) {
    deps.forEach((d) => {
      const node = createDepNode({ id: d.id, title: d.title, deps: [] }, 0);
      list.appendChild(node);
    });
  } else {
    const empty = document.createElement("div");
    empty.className = "rimsort-dep-empty";
    empty.textContent = "This mod has no required items";
    empty.style.padding = "8px";
    empty.style.color = "#888";
    list.appendChild(empty);
  }
  section.appendChild(list);
  return section;
}

function createDepNode(dep: DepNode, depth: number): HTMLDivElement {
  const node = document.createElement("div");
  node.className = "rimsort-dep-node";
  node.style.paddingLeft = `${depth * 16}px`;

  const hasChildren =
    dep.deps && dep.deps.length > 0 && depth + 1 < Config.DEPTH_MAX;

  const expand = document.createElement("span");
  expand.className = "rimsort-dep-expand";
  expand.textContent = hasChildren ? "\u25b8" : "";
  if (!hasChildren) expand.classList.add("empty");

  expand.addEventListener("click", (e) => {
    e.stopPropagation();
    if (expand.classList.contains("empty")) return;
    expand.classList.toggle("expanded");
    const children = node.nextElementSibling;
    if (
      children &&
      children.classList.contains("rimsort-dep-children")
    ) {
      children.style.display = expand.classList.contains("expanded")
        ? "block"
        : "none";
    }
  });

  const badge = document.createElement("span");
  badge.className = "rimsort-dep-badge";
  badge.dataset.modid = dep.id;
  badge.title = dep.id;
  setBadgeVisuals(badge, getDepBadgeState(dep.id));

  badge.addEventListener(
    "click",
    makeBadgeClickHandler(badge, dep.id, () => dep.title),
  );

  const titleLink = document.createElement("a");
  titleLink.className = "rimsort-dep-title";
  titleLink.href = `https://steamcommunity.com/sharedfiles/filedetails/?id=${dep.id}`;
  titleLink.target = "_blank";
  titleLink.rel = "noopener noreferrer";
  titleLink.textContent = dep.title;
  titleLink.title = dep.title;

  node.appendChild(expand);
  node.appendChild(badge);
  node.appendChild(titleLink);
  return node;
}

function renderDepTree(
  container: HTMLElement,
  deps: DepNode[],
  depth: number,
  seenIds: Set<string>,
): void {
  deps.forEach((dep) => {
    if (seenIds.has(dep.id)) {
      const circ = document.createElement("div");
      circ.className = "rimsort-dep-circular";
      circ.textContent = `\u21bb ${dep.title} (circular)`;
      circ.style.paddingLeft = `${depth * 16}px`;
      container.appendChild(circ);
      return;
    }
    seenIds.add(dep.id);

    const node = createDepNode(dep, depth);
    container.appendChild(node);

    if (dep.deps && dep.deps.length) {
      if (depth + 1 >= Config.DEPTH_MAX) {
        const maxDepth = document.createElement("div");
        maxDepth.className = "rimsort-dep-maxdepth";
        maxDepth.style.paddingLeft = `${(depth + 1) * 16}px`;
        maxDepth.textContent = `${dep.deps.length} required item(s) (max depth ${Config.DEPTH_MAX})`;
        container.appendChild(maxDepth);
      } else {
        const childrenContainer = document.createElement("div");
        childrenContainer.className = "rimsort-dep-children";
        childrenContainer.style.display = "none";
        renderDepTree(
          childrenContainer,
          dep.deps,
          depth + 1,
          new Set(seenIds),
        );
        container.appendChild(childrenContainer);
      }
    }
  });
}
