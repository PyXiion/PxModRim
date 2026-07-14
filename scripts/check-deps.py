"""Check layer imports and circular dependencies using pydeps.

Usage: uv run python scripts/check-deps.py

Layer rules (bottom→top):
  ui/ is the top layer — nothing beneath it may import from ui/.
"""

from __future__ import annotations

import sys

from pydeps import target, py2depgraph
from pydeps.configs import Config

SRC = "src/pxmodrim"

ENTRYPOINTS = {"__main__", "_app"}

ALLOWED_IMPORTS: dict[str, set[str]] = {
    "core": {"models", "_compat"},
    "services": {"core", "checker", "sort", "models", "_compat"},
    "checker": {"models", "sort", "_compat"},
    "sort": {"core", "models", "_compat"},
    "models": {"_compat"},
    "_compat": {"models", "sort"},
    "ui": {"_compat", "core", "services", "checker", "sort", "models"},
}

LEGACY_EXCEPTIONS: dict[str, set[str]] = {
    "core.mod_service": {"services"},
    "core.providers": {"services"},
    "core.providers.core": {"services"},
    "core.providers.local": {"services"},
    "sort.community_service": {"core"},
}


def _layer(modname: str) -> str | None:
    if not modname.startswith("pxmodrim."):
        return None
    layer = modname.split(".")[1]
    return None if layer in ENTRYPOINTS else layer


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

    # ── Circular imports ──────────────────────────────────────
    cycles = dg.find_import_cycles()
    if cycles:
        print(f"Found {len(cycles)} cyclic import(s):")
        for cycle in cycles:
            parts = [s.name for s in cycle]
            print("  ", " → ".join(parts))
        has_error = True

    # ── Layer violations ──────────────────────────────────────
    violations: list[tuple[str, str, str]] = []
    for modname, src in dg.sources.items():
        src_layer = _layer(modname)
        if src_layer is None:
            continue
        allowed = ALLOWED_IMPORTS.get(src_layer, set())
        for imp in src.imports:
            tgt_layer = _layer(imp)
            if tgt_layer is None or tgt_layer == src_layer:
                continue
            if tgt_layer in allowed:
                continue

            mod_short = modname.removeprefix("pxmodrim.")
            legacy = LEGACY_EXCEPTIONS.get(mod_short, set())
            if tgt_layer in legacy:
                continue

            violations.append((mod_short, imp, tgt_layer))

    if violations:
        print("\nLayer dependency violations:")
        for src_mod, tgt_mod, tgt_layer in sorted(violations):
            print(f"  {src_mod}  imports  {tgt_mod}  (forbidden: {tgt_layer}/)")
        has_error = True

    if has_error:
        print(
            "\nFix: move imports to a higher layer, or (temporarily) add\n"
            "an exception in LEGACY_EXCEPTIONS in scripts/check-deps.py"
        )
        return 1

    print("No circular imports")
    print("All layer dependencies OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
