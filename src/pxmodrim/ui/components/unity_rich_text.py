from __future__ import annotations

import html
import re

UNITY_TAG_PATTERN = re.compile(r"</?([a-zA-Z]+)(?:=([^>\s]+))?\s*>")
UNITY_COLOR_PATTERN = re.compile(r"^(#?[0-9a-fA-F]{3,8}|[a-zA-Z]+)$")
UNITY_SIZE_PATTERN = re.compile(r"^([+-]?\d+(?:\.\d+)?)(px|em|%)?$")


class UnityRichTextConverter:
    """Convert Unity rich text tags to Qt-compatible HTML subset."""

    def __init__(self) -> None:
        self._tag_stack: list[str] = []

    def convert(self, text: str) -> str:
        self._tag_stack.clear()
        # Split text into segments: tags and text content
        # Process tags first, escape text content between tags
        result_parts = []
        last_end = 0

        for match in UNITY_TAG_PATTERN.finditer(text):
            # Text before this tag - escape it
            if match.start() > last_end:
                text_segment = text[last_end : match.start()]
                result_parts.append(html.escape(text_segment))

            # Process the tag
            full = match.group(0)
            tag_name = match.group(1).lower()
            attr = match.group(2)

            if full.startswith("</"):
                result_parts.append(self._handle_close_tag(tag_name))
            else:
                result_parts.append(self._handle_open_tag(tag_name, attr))

            last_end = match.end()

        # Remaining text after last tag
        if last_end < len(text):
            result_parts.append(html.escape(text[last_end:]))

        result = "".join(result_parts)
        result = result.replace("\n", "<br>")
        result = result.replace("\\n", "<br>")
        return self._close_all_tags(result)

    def _handle_open_tag(self, tag: str, attr: str | None) -> str:
        html_tag = self._unity_to_html_open(tag, attr)
        if html_tag:
            self._tag_stack.append(tag)
            return html_tag
        return ""

    def _handle_close_tag(self, tag: str) -> str:
        tag = tag.lower()
        if tag in self._tag_stack:
            while self._tag_stack and self._tag_stack[-1] != tag:
                self._tag_stack.pop()
            if self._tag_stack:
                self._tag_stack.pop()
            return self._unity_to_html_close(tag)
        return ""

    def _close_all_tags(self, text: str) -> str:
        for tag in reversed(self._tag_stack):
            text += self._unity_to_html_close(tag)
        self._tag_stack.clear()
        return text

    def _unity_to_html_open(self, tag: str, attr: str | None) -> str:
        match tag:
            case "b" | "bold":
                return "<b>"
            case "i" | "italic":
                return "<i>"
            case "u" | "underline":
                return '<span style="text-decoration: underline;">'
            case "s" | "strike" | "strikethrough":
                return "<s>"
            case "color":
                return self._color_tag(attr)
            case "size":
                return self._size_tag(attr)
            case "align":
                return self._align_tag(attr)
            case "indent":
                return self._indent_tag(attr)
            case "mark":
                return self._mark_tag(attr)
            case "br":
                return "<br>"
            case "sub":
                return "<sub>"
            case "sup":
                return "<sup>"
            case "a" | "link":
                return self._link_tag(attr)
            case (
                "noparse"
                | "alpha"
                | "font"
                | "font-weight"
                | "mspace"
                | "style"
                | "gradient"
                | "cspace"
                | "voffset"
                | "pos"
                | "width"
                | "line-height"
                | "line-indent"
                | "margin"
                | "margin-left"
                | "margin-right"
                | "uppercase"
                | "lowercase"
                | "smallcaps"
                | "rotate"
                | "sprite"
            ):
                # TODO: Implement these tags
                return ""
            case _:
                return ""

    def _unity_to_html_close(self, tag: str) -> str:
        match tag:
            case "b" | "bold":
                return "</b>"
            case "i" | "italic":
                return "</i>"
            case "u" | "underline":
                return "</span>"
            case "s" | "strike" | "strikethrough":
                return "</s>"
            case "color":
                return "</font>"
            case "size":
                return "</span>"
            case "align":
                return "</div>"
            case "indent":
                return "</div>"
            case "mark":
                return "</mark>"
            case "sub":
                return "</sub>"
            case "sup":
                return "</sup>"
            case "a" | "link":
                return "</a>"
            case _:
                return ""

    def _color_tag(self, attr: str | None) -> str:
        if not attr:
            return '<font color="#ffffff">'
        attr = attr.strip("\"'")
        if UNITY_COLOR_PATTERN.match(attr):
            return f'<font color="{self._normalize_color(attr)}">'
        return '<font color="#ffffff">'

    def _normalize_color(self, color: str) -> str:
        if color.startswith("#"):
            hex_part = color[1:]
            if len(hex_part) == 3:
                return "#" + "".join(c * 2 for c in hex_part)
            if len(hex_part) == 4:
                return "#" + "".join(c * 2 for c in hex_part)
            if len(hex_part) == 6:
                return "#" + hex_part
            if len(hex_part) == 8:
                return "#" + hex_part[:6]
        named_colors = {
            "red": "#ff0000",
            "green": "#00ff00",
            "blue": "#0000ff",
            "white": "#ffffff",
            "black": "#000000",
            "yellow": "#ffff00",
            "cyan": "#00ffff",
            "magenta": "#ff00ff",
            "gray": "#808080",
            "grey": "#808080",
            "orange": "#ffa500",
            "purple": "#800080",
        }
        return named_colors.get(color.lower(), "#ffffff")

    def _size_tag(self, attr: str | None) -> str:
        if not attr:
            return '<span style="font-size: 13pt;">'
        attr = attr.strip("\"'")
        m = UNITY_SIZE_PATTERN.match(attr)
        if not m:
            return '<span style="font-size: 13pt;">'
        raw_value = m.group(1)
        unit = m.group(2) or "px"
        is_relative = raw_value.startswith(("+", "-"))
        value = float(raw_value)
        base_pt = 12.0
        if unit == "%":
            pt = max(1, base_pt * value / 100)
        elif unit == "em":
            pt = max(1, base_pt * value)
        else:
            pt = max(1, base_pt + value if is_relative else value)
        pt_str = str(int(pt)) if pt == int(pt) else str(pt)
        return f'<span style="font-size: {pt_str}pt;">'

    def _align_tag(self, attr: str | None) -> str:
        if not attr:
            return '<div style="text-align: left;">'
        attr = attr.strip("\"'").lower()
        align_map = {
            "left": "left",
            "center": "center",
            "right": "right",
            "justified": "justify",
            "flush": "justify",
        }
        return f'<div style="text-align: {align_map.get(attr, "left")};">'

    def _indent_tag(self, attr: str | None) -> str:
        if not attr:
            return '<div style="margin-left: 20px;">'
        attr = attr.strip("\"'")
        m = UNITY_SIZE_PATTERN.match(attr)
        if not m:
            return '<div style="margin-left: 20px;">'
        value = float(m.group(1))
        unit = m.group(2) or "px"
        if unit == "%":
            px = int(400 * value / 100)
        elif unit == "em":
            px = int(16 * value)
        else:
            px = int(value)
        return f'<div style="margin-left: {px}px;">'

    def _mark_tag(self, attr: str | None) -> str:
        if not attr:
            return '<mark style="background-color: #ffff00;">'
        attr = attr.strip("\"'")
        color = self._normalize_color(attr)
        if len(attr) == 9 and attr.startswith("#"):
            alpha = int(attr[7:], 16) / 255
            return f'<mark style="background-color: {color}{alpha:.2f};">'
        return f'<mark style="background-color: {color};">'

    def _link_tag(self, attr: str | None) -> str:
        if not attr:
            return '<a href="#">'
        attr = attr.strip("\"'")
        return f'<a href="{html.escape(attr)}">'


def unity_rich_text_to_html(text: str) -> str:
    """Convert Unity rich text to Qt-compatible HTML."""
    return UnityRichTextConverter().convert(text)
