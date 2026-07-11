from __future__ import annotations

import os
from typing import Any

import lxml.etree as ET
from loguru import logger


def etree_to_dict(t: Any) -> dict[str, Any]:
    d: dict[str, Any] = {str(t.tag): {}}
    children = list(t)
    if children:
        dd: dict[str, Any] = {}
        for dc in map(etree_to_dict, children):
            for k, v in dc.items():
                if k in dd:
                    if not isinstance(dd[k], list):
                        dd[k] = [dd[k]]
                    dd[k].append(v)
                else:
                    dd[k] = v
        d[str(t.tag)] = dd
    if t.attrib:
        d[str(t.tag)].update(("@" + str(k), str(v)) for k, v in t.attrib.items())
    if t.text:
        text = str(t.text or "").strip()
        if children or t.attrib:
            if text:
                d[str(t.tag)]["#text"] = text
        else:
            d[str(t.tag)] = text
    return d


def xml_path_to_json(path: str) -> dict[str, Any]:
    if not os.path.exists(path):
        logger.error(f"XML file does not exist at: {path}")
        return {}
    try:
        tree = ET.parse(path)
        root = tree.getroot()
        return etree_to_dict(root)
    except Exception as e:
        logger.error(f"Error parsing XML file {path}: {e}")
        return {}
