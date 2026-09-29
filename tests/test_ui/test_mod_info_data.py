from __future__ import annotations

from pxmodrim.ui.panels.mod_info_data import (
    IMG_WIDTH_TOKEN,
    autolink,
    bbcode_images,
    bbcode_markup,
)


class TestAutolink:
    def test_bare_url_becomes_anchor(self) -> None:
        assert autolink("see https://a.com/x?y=1 now") == (
            'see <a href="https://a.com/x?y=1">https://a.com/x?y=1</a> now'
        )

    def test_trailing_punctuation_stays_outside(self) -> None:
        assert autolink("go to https://a.com.") == (
            'go to <a href="https://a.com">https://a.com</a>.'
        )

    def test_existing_anchor_is_untouched(self) -> None:
        html = '<a href="https://b.com">https://b.com</a>'
        assert autolink(html) == html


class TestBbcodeImages:
    def test_img_tag_becomes_width_constrained_image(self) -> None:
        assert bbcode_images("[img]https://i.imgur.com/a.png[/img]\nhi") == (
            f'<img src="https://i.imgur.com/a.png" width="{IMG_WIDTH_TOKEN}">\nhi'
        )

    def test_image_url_is_not_autolinked_afterwards(self) -> None:
        html = autolink(bbcode_images("[img]https://i.imgur.com/a.png[/img]"))
        assert "<a " not in html


class TestBbcodeMarkup:
    def test_named_url_wraps_its_content(self) -> None:
        assert bbcode_markup("[url=https://a.com/w]the wiki[/url]") == (
            '<a href="https://a.com/w">the wiki</a>'
        )

    def test_image_inside_url_stays_inside_anchor(self) -> None:
        html = bbcode_markup(
            bbcode_images(
                "[url=https://discord.gg/x][img]https://i.imgur.com/a.png[/img][/url]"
            )
        )
        assert html.startswith('<a href="https://discord.gg/x"><img src=')
        assert html.endswith("></a>")

    def test_non_http_url_is_left_alone(self) -> None:
        text = "[url=javascript:alert(1)]x[/url]"
        assert bbcode_markup(text) == text

    def test_bold_and_italic(self) -> None:
        assert bbcode_markup("[b]a[/b] [i]b[/i]") == "<b>a</b> <i>b</i>"
