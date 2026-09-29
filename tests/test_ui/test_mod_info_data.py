from __future__ import annotations

from types import SimpleNamespace
from typing import Any, cast

from pxmodrim.core.models.metadata.structures import (
    AboutXmlMod,
    BaseRules,
    CaseInsensitiveStr,
    DependencyMod,
    ListedMod,
)
from pxmodrim.core.models.view.diagnostics import ModIssueView
from pxmodrim.ui.components.unity_rich_text import unity_rich_text_to_html
from pxmodrim.ui.panels.mod_info_data import (
    IMG_WIDTH_TOKEN,
    _conflicts,
    _needed_by,
    _needs,
    _pid_index,
    autolink,
    bbcode_images,
    bbcode_markup,
    build_mod_info,
    restore_entities,
)


def render(text: str) -> str:
    return autolink(
        bbcode_markup(bbcode_images(restore_entities(unity_rich_text_to_html(text))))
    )


def make_mod(
    name: str,
    pid: str,
    deps: list[tuple[str, list[str]]] | None = None,
    incompatible: list[str] | None = None,
) -> AboutXmlMod:
    rules = BaseRules()
    for dep_pid, alts in deps or []:
        rules.dependencies[CaseInsensitiveStr(dep_pid)] = DependencyMod(
            name=dep_pid,
            package_id=CaseInsensitiveStr(dep_pid),
            alternative_package_ids={CaseInsensitiveStr(a) for a in alts},
        )
    for i in incompatible or []:
        rules.incompatible_with.add(CaseInsensitiveStr(i))
    return AboutXmlMod(
        name=name,
        package_id=CaseInsensitiveStr(pid),
        about_rules=rules,
        description="",
    )


def make_ctx(mods: list[ListedMod], active: list[str]) -> Any:
    return cast(
        Any,
        SimpleNamespace(
            all_mods={m.uuid: m for m in mods},
            active_uuids=active,
            target_version="1.5",
        ),
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


class TestSteamTags:
    def test_list_items_have_no_stray_line_breaks(self) -> None:
        html = bbcode_markup("[list]<br>[*]one<br>[*]two<br>[/list]<br>after")
        assert html == "<ul><li>one<li>two</ul>after"

    def test_heading_swallows_following_break(self) -> None:
        assert bbcode_markup("[h2]Title[/h2]<br>body") == "<h2>Title</h2>body"

    def test_quote_with_author(self) -> None:
        assert bbcode_markup("[quote=Bob]hi[/quote]") == (
            "<blockquote><i>Bob wrote:</i><br>hi</blockquote>"
        )

    def test_table_with_attributes(self) -> None:
        assert bbcode_markup("[table noborder=1][tr][td]x[/td][/tr][/table]") == (
            "<table><tr><td>x</td></tr></table>"
        )


class TestRestoreEntities:
    def test_numeric_entity_is_unescaped_once(self) -> None:
        assert restore_entities("&amp;#8226; item") == "&#8226; item"

    def test_plain_ampersand_stays_escaped(self) -> None:
        assert restore_entities("a &amp; b") == "a &amp; b"


class TestAutolinkEscaped:
    def test_quoted_url_excludes_entity(self) -> None:
        assert render('see "https://a.com/x" ok') == (
            'see &quot;<a href="https://a.com/x">https://a.com/x</a>&quot; ok'
        )

    def test_angle_bracketed_url_excludes_entity(self) -> None:
        assert render("<https://a.com/x>") == (
            '&lt;<a href="https://a.com/x">https://a.com/x</a>&gt;'
        )

    def test_ampersand_in_query_is_kept(self) -> None:
        assert autolink("https://a.com/?a=1&amp;b=2") == (
            '<a href="https://a.com/?a=1&amp;b=2">https://a.com/?a=1&amp;b=2</a>'
        )


class TestSecurity:
    def test_file_img_is_not_an_image(self) -> None:
        assert "<img" not in render("[img]file:///etc/passwd[/img]")

    def test_http_img_is_a_link_not_an_image(self) -> None:
        html = render("[img]http://a.com/i.png[/img]")
        assert "<img" not in html
        assert '<a href="http://a.com/i.png">' in html

    def test_javascript_url_tag_has_no_anchor(self) -> None:
        assert "<a " not in render("[url=javascript:alert(1)]x[/url]")

    def test_raw_html_is_escaped(self) -> None:
        html = render('<a href="evil">x</a> <b onclick="1">y</b>')
        assert "<a " not in html
        assert "<b " not in html
        assert "&lt;a href=" in html

    def test_quote_injection_in_url_attribute(self) -> None:
        html = render('[url=https://a.com/x" onclick="evil]t[/url]')
        assert 'onclick="' not in html
        assert '" onclick' not in html


class TestNeeds:
    def test_states_and_alternatives(self) -> None:
        dep_active = make_mod("Active", "a.active")
        dep_inactive = make_mod("Inactive", "a.inactive")
        alt = make_mod("Alt", "a.alt")
        main = make_mod(
            "Main",
            "a.main",
            deps=[
                ("a.active", []),
                ("A.Inactive", []),
                ("a.missing", ["a.alt"]),
                ("a.gone", []),
            ],
        )
        ctx = make_ctx([main, dep_active, dep_inactive, alt], [dep_active.uuid])
        result = _needs(main, _pid_index(ctx), {dep_active.uuid})
        assert {r["name"]: (r["state"], r["uuid"]) for r in result} == {
            "a.active": ("active", dep_active.uuid),
            "A.Inactive": ("inactive", dep_inactive.uuid),
            "a.missing": ("inactive", alt.uuid),
            "a.gone": ("missing", ""),
        }

    def test_conflicts_only_lists_active_sorted(self) -> None:
        b = make_mod("beta", "x.b")
        a = make_mod("Alpha", "x.a")
        off = make_mod("Off", "x.off")
        main = make_mod("Main", "x.main", incompatible=["X.B", "x.a", "x.off", "x.no"])
        ctx = make_ctx([main, a, b, off], [a.uuid, b.uuid])
        assert _conflicts(main, _pid_index(ctx), {a.uuid, b.uuid}) == ["Alpha", "beta"]

    def test_needed_by_matches_alternatives_and_sorts(self) -> None:
        target = make_mod("Target", "t.target")
        z = make_mod("zed", "t.z", deps=[("t.target", [])])
        a = make_mod("Apple", "t.a", deps=[("t.other", ["T.Target"])])
        unrelated = make_mod("Unrelated", "t.u", deps=[("t.other", [])])
        ctx = make_ctx([target, z, a, unrelated], [])
        assert _needed_by(target, ctx) == [
            {"name": "Apple", "uuid": a.uuid},
            {"name": "zed", "uuid": z.uuid},
        ]


class TestBuildModInfo:
    def test_active_mod_with_issue(self) -> None:
        dep = make_mod("Dep", "d.dep")
        main = make_mod("Main", "m.main", deps=[("d.dep", [])])
        main.description = "hello <b>x</b> https://a.com/x"
        main.authors = ["Ann"]
        other = make_mod("Other", "o.other")
        ctx = make_ctx([dep, main, other], [other.uuid, main.uuid])
        issue = ModIssueView(
            category="c", category_display_name="Cat", detail=None, is_error=False
        )
        info = build_mod_info(ctx, main, [issue])
        assert info["name"] == "Main"
        assert info["author"] == "Ann"
        assert info["status"]["level"] == "warning"
        assert info["status"]["title"] == "1 warning"
        assert info["status"]["active"] is True
        assert info["status"]["position"] == 2
        assert info["status"]["total"] == 2
        assert info["status"]["issues"] == [
            {"title": "Cat", "detail": "", "isError": False}
        ]
        assert info["needs"] == [
            {"name": "d.dep", "state": "inactive", "uuid": dep.uuid}
        ]
        assert '<a href="https://a.com/x">' in info["description"]
        assert info["version"] == {"known": False, "ok": False, "target": "1.5"}

    def test_inactive_mod_without_description(self) -> None:
        mod = make_mod("Solo", "s.solo")
        info = build_mod_info(make_ctx([mod], []), mod, [])
        assert info["status"]["level"] == "ok"
        assert info["status"]["active"] is False
        assert info["status"]["position"] == 0
        assert info["description"] == ""
        assert info["neededBy"] == []
