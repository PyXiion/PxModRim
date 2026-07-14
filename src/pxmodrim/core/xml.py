from __future__ import annotations

import os
from typing import Any

import lxml.etree as ET
from loguru import logger


def etree_to_dict(t: Any) -> dict[str, Any]:
    """Recursively convert an lxml Element tree into a plain nested dictionary."""
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
    """Parse an XML file at *path* and return its contents as a nested dictionary."""
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


def dict_to_etree(data: dict[str, Any]) -> ET._Element:
    """Convert a dictionary to an lxml.etree Element recursively."""
    if len(data) != 1:
        raise ValueError("Dictionary must have exactly one root key")
    root_key, root_val = next(iter(data.items()))
    root = ET.Element(root_key)
    _build_etree(root, root_val)
    return root


def _build_etree(parent: ET._Element, value: Any) -> None:
    """Recursively build XML elements from dict/list/str values."""
    if isinstance(value, dict):
        for k, v in value.items():
            if k.startswith("@"):
                parent.set(k[1:], str(v))
            elif k == "#text":
                parent.text = str(v)
            elif isinstance(v, list):
                for item in v:
                    child = ET.SubElement(parent, k)
                    _build_etree(child, item)
            else:
                child = ET.SubElement(parent, k)
                _build_etree(child, v)
    elif isinstance(value, list):
        for item in value:
            # Anonymous list items - shouldn't happen with well-formed data
            child = ET.SubElement(parent, "li")
            _build_etree(child, item)
    else:
        parent.text = str(value)


def json_to_xml_write(
    data: dict[str, Any], path: str, raise_errs: bool = False
) -> None:
    """Write dictionary data to an XML file with pretty printing."""
    try:
        root = dict_to_etree(data)
        formatted = ET.tostring(
            root, encoding="utf-8", xml_declaration=True, pretty_print=True
        )
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            f.write(formatted.decode("utf-8"))
        logger.debug(f"XML written to {path}")
    except Exception as e:
        logger.error(f"Error writing XML file: {e}")
        if raise_errs:
            raise
