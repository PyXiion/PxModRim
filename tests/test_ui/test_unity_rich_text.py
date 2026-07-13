from __future__ import annotations

from pxmodrim.ui.components.unity_rich_text import unity_rich_text_to_html


class TestUnityRichTextConverter:
    def test_basic_text(self) -> None:
        assert unity_rich_text_to_html("Hello world") == "Hello world"

    def test_bold(self) -> None:
        assert unity_rich_text_to_html("<b>bold</b>") == "<b>bold</b>"
        assert unity_rich_text_to_html("<bold>bold</bold>") == "<b>bold</b>"

    def test_italic(self) -> None:
        assert unity_rich_text_to_html("<i>italic</i>") == "<i>italic</i>"
        assert unity_rich_text_to_html("<italic>italic</italic>") == "<i>italic</i>"

    def test_underline(self) -> None:
        assert (
            unity_rich_text_to_html("<u>underline</u>")
            == '<span style="text-decoration: underline;">underline</span>'
        )

    def test_strikethrough(self) -> None:
        assert unity_rich_text_to_html("<s>strike</s>") == "<s>strike</s>"

    def test_color_named(self) -> None:
        assert (
            unity_rich_text_to_html("<color=red>red</color>")
            == '<font color="#ff0000">red</font>'
        )

    def test_color_hex(self) -> None:
        assert (
            unity_rich_text_to_html("<color=#ff0000>red</color>")
            == '<font color="#ff0000">red</font>'
        )

    def test_size_px(self) -> None:
        assert (
            unity_rich_text_to_html("<size=24>big</size>")
            == '<span style="font-size: 24pt;">big</span>'
        )

    def test_size_percent(self) -> None:
        assert (
            unity_rich_text_to_html("<size=200%>big</size>")
            == '<span style="font-size: 24pt;">big</span>'
        )

    def test_size_em(self) -> None:
        assert (
            unity_rich_text_to_html("<size=1.5em>big</size>")
            == '<span style="font-size: 18pt;">big</span>'
        )

    def test_size_relative(self) -> None:
        assert (
            unity_rich_text_to_html("<size=+5>relative</size>")
            == '<span style="font-size: 17pt;">relative</span>'
        )

    def test_size_negative(self) -> None:
        assert (
            unity_rich_text_to_html("<size=-2>small</size>")
            == '<span style="font-size: 10pt;">small</span>'
        )

    def test_align(self) -> None:
        assert (
            unity_rich_text_to_html('<align="center">center</align>')
            == '<div style="text-align: center;">center</div>'
        )

    def test_indent(self) -> None:
        assert (
            unity_rich_text_to_html("<indent=10%>indented</indent>")
            == '<div style="margin-left: 40px;">indented</div>'
        )

    def test_mark(self) -> None:
        assert (
            unity_rich_text_to_html("<mark=#ffff00>marked</mark>")
            == '<mark style="background-color: #ffff00;">marked</mark>'
        )

    def test_sub_sup(self) -> None:
        assert unity_rich_text_to_html("<sub>sub</sub>") == "<sub>sub</sub>"
        assert unity_rich_text_to_html("<sup>sup</sup>") == "<sup>sup</sup>"

    def test_link(self) -> None:
        assert (
            unity_rich_text_to_html('<link="http://example.com">link</link>')
            == '<a href="http://example.com">link</a>'
        )

    def test_nested(self) -> None:
        assert (
            unity_rich_text_to_html("<b><i>bold italic</i></b>")
            == "<b><i>bold italic</i></b>"
        )

    def test_unknown_tag_ignored(self) -> None:
        # Unknown tags should be stripped
        assert unity_rich_text_to_html("<foo>bar</foo>") == "bar"

    def test_mixed_formatting(self) -> None:
        assert (
            unity_rich_text_to_html("<b><color=#00ff00>green</color></b>")
            == '<b><font color="#00ff00">green</font></b>'
        )
