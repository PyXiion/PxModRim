"""Check layer imports and circular dependencies using pydeps.

Usage: uv run python scripts/check-deps.py

Layer rules (bottom→top):
  core/  — domain logic; never imports ui/
  ui/    — presentation layer; may import core/

Sub-layer rules enforce dependency ordering within each top-level package.
"""

from __future__ import annotations

import sys

from pydeps import target, py2depgraph
from pydeps.configs import Config

SRC = "src/pxmodrim"

ENTRYPOINTS = {"__main__", "_app"}

GROUPS: dict[str, set[str]] = {
    "core.foundation": {"core.constants", "core.events", "core.structures", "core.utils", "core.xml", "core.loading",
                        "core.profiler"},
    "core.models": {"core.models"},
    "core.msgspec_hooks": {"core.msgspec_hooks"},
    "core.sort": {"core.sort"},
    "core.checker": {"core.checker"},
    "core.config": {"core.config"},
    "core.context": {"core.context"},
    "core.mods_config": {"core.mods_config"},
    "core.providers": {"core.providers"},
    "core.services": {"core.services"},
    "core.mod_service": {"core.mod_service"},
    "ui.progress": {"ui.progress"},
    "ui.theme": {"ui.theme"},
    "ui.components": {"ui.components"},
    "ui.models": {"ui.models"},
    "ui.panels": {"ui.panels"},
    "ui.views": {"ui.views"},
    "ui.window": {"ui.window"},
}

ALLOWED: dict[str, set[str]] = {
    "core": set(),
    "ui": {"core"},
    "core.foundation": set(),
    "core.models": {"core.foundation"},
    "core.msgspec_hooks": {"core.foundation", "core.models"},
    "core.sort": {"core.foundation", "core.models", "core.msgspec_hooks"},
    "core.checker": {"core.foundation", "core.models", "core.sort"},
    "core.config": {"core.foundation", "core.msgspec_hooks", "core.sort"},
    "core.context": {"core"},
    "core.mods_config": {"core.foundation", "core.models"},
    "core.providers": {"core.foundation", "core.models", "core.config", "core.services"},
    "core.services": {"core.foundation", "core.models", "core.checker", "core.sort", "core.context", "core.config", "core.mods_config"},
    "core.mod_service": {"core.foundation", "core.models", "core.context", "core.providers", "core.services", "core.mods_config"},
    "ui.progress": set(),
    "ui.theme": {"ui.components"},
    "ui.components": {"ui.theme"},
    "ui.models": {"ui.theme"},
    "ui.panels": {"ui.theme", "ui.components", "ui.models"},
    "ui.views": {"ui.theme", "ui.components", "ui.models", "ui.panels"},
    "ui.window": {"ui.theme", "ui.components", "ui.models", "ui.panels", "ui.views"},
}

_PREFIX_TO_GROUP: dict[str, str] = {}
for _group, _members in GROUPS.items():
    for _member in _members:
        _PREFIX_TO_GROUP[f"pxmodrim.{_member}"] = _group


def _layer(modname: str) -> str | None:
    if not modname.startswith("pxmodrim."):
        return None
    parts = modname.split(".")
    if len(parts) < 3:
        return None
    top = parts[1]
    if top in ENTRYPOINTS:
        return None
    if parts[2] == "__init__":
        return None
    prefix = f"pxmodrim.{top}.{parts[2]}"
    return _PREFIX_TO_GROUP.get(prefix, top)


def _is_allowed(src_group: str, tgt_group: str) -> bool:
    own = ALLOWED.get(src_group, set())
    parent = ALLOWED.get(src_group.split(".")[0], set())
    effective = own | parent
    if tgt_group in effective:
        return True
    return tgt_group.split(".")[0] in effective


def main() -> int:
    cfg = Config()
    cfg.fname = SRC
    cfg.no_show = True
    cfg.no_output = True
    cfg.max_bacon = sys.maxsize
    cfg.no_dot = True
    cfg.max_module_depth = 3

    trgt = target.Target(cfg.fname)
    dg = py2depgraph.py2dep(trgt, **cfg.__dict__)

    has_error = False

    cycles = dg.find_import_cycles()
    if cycles:
        print(f"Found {len(cycles)} cyclic import(s):")
        for cycle in cycles:
            parts = [s.name for s in cycle]
            print("  ", " → ".join(parts))
        has_error = True

    violations: list[tuple[str, str, str]] = []
    for modname, src in dg.sources.items():
        src_group = _layer(modname)
        if src_group is None:
            continue
        for imp in src.imports:
            tgt_group = _layer(imp)
            if tgt_group is None or tgt_group == src_group:
                continue
            if _is_allowed(src_group, tgt_group):
                continue
            violations.append(
                (modname.removeprefix("pxmodrim."), imp.removeprefix("pxmodrim."), tgt_group)
            )

    if violations:
        print("\nLayer dependency violations:")
        for src_mod, tgt_mod, tgt_group in sorted(violations):
            print(f"  {src_mod}  →  {tgt_mod}  (forbidden: {tgt_group})")
        has_error = True

    if has_error:
        print(
            "\nFix: move imports to a higher layer, or update\n"
            "GROUPS / ALLOWED in scripts/check-deps.py"
        )
        return 1

    print("No circular imports")
    print("All layer dependencies OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
