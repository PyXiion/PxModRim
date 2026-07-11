from __future__ import annotations

from typing import Any

from PySide6.QtCore import QEvent, QModelIndex, QRect, QSize, Qt
from PySide6.QtGui import (
    QColor,
    QFontMetrics,
    QHelpEvent,
    QIcon,
    QPainter,
)
from PySide6.QtWidgets import (
    QAbstractItemView,
    QApplication,
    QStyle,
    QStyledItemDelegate,
    QStyleOptionViewItem,
    QToolTip,
)

from app.models.divider import is_divider_uuid
from app.models.metadata.metadata_structure import ModType
from app.views.mod_list_icons import ModListIcons

_ITEM_HEIGHT = 24
_ICON_SIZE = 16
_MARGIN = 4
_ICON_SPACING = 2


class ModItemDelegate(QStyledItemDelegate):
    """
    Renders mod items for a QListWidget (or QListView).

    Reads the UUID from the model's UserRole+1 data, then looks up
    the mod via MetadataController for display.  State-dependent
    rendering (errors, warnings, save-comparison indicators, colours)
    is read from the owning ModListWidget._mod_state dict.
    """

    def __init__(
        self,
        metadata_controller: Any,
        settings: Any,
        list_widget: Any = None,
        parent: Any = None,
    ) -> None:
        super().__init__(parent)
        self.metadata_controller = metadata_controller
        self.settings = settings
        self._list_widget = list_widget

    # ------------------------------------------------------------------
    # Size
    # ------------------------------------------------------------------

    def sizeHint(self, option: QStyleOptionViewItem, index: QModelIndex) -> QSize:
        uuid = index.data(Qt.ItemDataRole.UserRole + 1)
        if uuid and is_divider_uuid(uuid):
            return QSize(0, 28)
        return QSize(0, _ITEM_HEIGHT)

    # ------------------------------------------------------------------
    # Paint dispatch
    # ------------------------------------------------------------------

    def paint(
        self,
        painter: QPainter,
        option: QStyleOptionViewItem,
        index: QModelIndex,
    ) -> None:
        uuid = index.data(Qt.ItemDataRole.UserRole + 1)
        if uuid and is_divider_uuid(uuid):
            self._paint_divider(painter, option, index)
        else:
            self._paint_mod(painter, option, index, uuid)

    # ------------------------------------------------------------------
    # Divider rendering
    # ------------------------------------------------------------------

    def _paint_divider(
        self,
        painter: QPainter,
        option: QStyleOptionViewItem,
        index: QModelIndex,
    ) -> None:
        painter.save()
        rect = option.rect
        text = index.data(Qt.ItemDataRole.DisplayRole) or ""

        painter.fillRect(rect, QColor("#3a3a4a"))
        painter.setPen(QColor("#cccccc"))
        font = painter.font()
        font.setBold(True)
        painter.setFont(font)
        painter.drawText(
            rect.adjusted(6, 0, -4, 0), Qt.AlignmentFlag.AlignVCenter, text
        )

        painter.restore()

    # ------------------------------------------------------------------
    # Mod item rendering
    # ------------------------------------------------------------------

    def _paint_mod(
        self,
        painter: QPainter,
        option: QStyleOptionViewItem,
        index: QModelIndex,
        uuid: str | None,
    ) -> None:
        painter.save()

        # Populate option with QSS-aware state (palette, flags, font, text, etc.)
        self.initStyleOption(option, index)

        mod = self.metadata_controller.get_mod(uuid) if uuid else None
        state = self._state_for(uuid)

        rect = option.rect
        selected = option.state & QStyle.StateFlag.State_Selected
        hovered = option.state & QStyle.StateFlag.State_MouseOver

        mod_color: QColor | None = state.get("mod_color")
        color_bg: bool = self.settings.color_background_instead_of_text_toggle

        # ---- 1. Native QSS-styled background (selection, hover, border-radius) ----
        style = option.widget.style() if option.widget else QApplication.style()
        style.drawPrimitive(
            QStyle.PrimitiveElement.PE_PanelItemViewItem, option, painter, option.widget
        )

        # ---- 2. Overlay mod background colour (if enabled) ----
        if mod_color and color_bg and not selected:
            painter.fillRect(rect, mod_color)
            if hovered:
                painter.fillRect(rect, QColor(255, 255, 255, 30))

        # ---- 3. Text colour (QSS-respecting via palette or mod colour override) ----
        if selected:
            text_color = option.palette.highlightedText().color()
        elif mod_color and not color_bg:
            text_color = mod_color
        else:
            text_color = option.palette.text().color()

        # ---- 4. Right-side icons (right-to-left) ----
        icon_x = rect.right() - _MARGIN
        icons_right = self._collect_right_icons(uuid, state)

        # Append translation indicator if enabled
        if (
            uuid
            and self._list_widget
            and self._list_widget.show_translation_status
            and mod
        ):
            pkg_id = (
                str(mod.package_id)
                if hasattr(mod, "package_id") and mod.package_id
                else ""
            )
            if pkg_id and pkg_id in self._list_widget.translation_lookup:
                icons_right.insert(
                    0, (self._icon("translation"), "Translation mod")
                )

        # Append tags text indicator if enabled
        if (
            uuid
            and self._list_widget
            and self._list_widget.show_tags
        ):
            tag_count = len(state.get("mod_tags", []))
            if tag_count > 0:
                icons_right.insert(
                    0, (self._icon("tag"), f"Tags: {tag_count}")
                )

        for icon, _ in icons_right:
            icon_x -= _ICON_SIZE
            icon_rect = QRect(
                icon_x,
                rect.top() + (rect.height() - _ICON_SIZE) // 2,
                _ICON_SIZE,
                _ICON_SIZE,
            )
            painter.drawPixmap(icon_rect, icon.pixmap(_ICON_SIZE, _ICON_SIZE))
            icon_x -= _ICON_SPACING

        # ---- 5. Left-side icons ----
        name_left = rect.left() + _MARGIN

        source_icon = self._source_icon(mod)
        if source_icon:
            icon_rect = QRect(
                name_left,
                rect.top() + (rect.height() - _ICON_SIZE) // 2,
                _ICON_SIZE,
                _ICON_SIZE,
            )
            painter.drawPixmap(icon_rect, source_icon.pixmap(_ICON_SIZE, _ICON_SIZE))
            name_left += _ICON_SIZE + _ICON_SPACING

        type_icon = self._type_icon(mod)
        if type_icon:
            icon_rect = QRect(
                name_left,
                rect.top() + (rect.height() - _ICON_SIZE) // 2,
                _ICON_SIZE,
                _ICON_SIZE,
            )
            painter.drawPixmap(icon_rect, type_icon.pixmap(_ICON_SIZE, _ICON_SIZE))
            name_left += _ICON_SIZE + _ICON_SPACING

        # ---- 6. Name text ----
        name = mod.name if mod else "Unknown"
        available_width = max(10, icon_x - name_left - _MARGIN)
        fm = QFontMetrics(painter.font())
        elided_name = fm.elidedText(
            str(name), Qt.TextElideMode.ElideRight, available_width
        )
        painter.setPen(text_color)
        painter.drawText(
            QRect(name_left, rect.top(), available_width, rect.height()),
            Qt.AlignmentFlag.AlignVCenter,
            elided_name,
        )

        painter.restore()

    # ------------------------------------------------------------------
    # State helpers
    # ------------------------------------------------------------------

    def _state_for(self, uuid: str | None) -> dict[str, Any]:
        if uuid is not None and self._list_widget is not None:
            return self._list_widget._mod_state.get(uuid, {})
        return {}

    # ------------------------------------------------------------------
    # Right-side icon collection
    # ------------------------------------------------------------------

    def _collect_right_icons(
        self, uuid: str | None, state: dict[str, Any]
    ) -> list[tuple[QIcon, str]]:
        icons: list[tuple[QIcon, str]] = []

        errors = state.get("errors", "")
        warnings = state.get("warnings", "")

        if errors:
            icons.append((self._icon("error"), errors))
        if warnings:
            icons.append((self._icon("warning"), warnings))

        list_type = self._list_widget.list_type if self._list_widget else None
        if list_type == "Active" and state.get("is_new"):
            icons.append((self._icon("new"), "Not in latest save"))
        elif list_type == "Inactive" and state.get("in_save"):
            icons.append((self._icon("in_save"), "In latest save"))

        if state.get("is_recently_updated"):
            icons.append((self._icon("updated"), "Recently updated"))

        return icons

    # ------------------------------------------------------------------
    # Left-side icons
    # ------------------------------------------------------------------

    def _source_icon(self, mod: Any) -> QIcon | None:
        if mod is None:
            return None
        mod_type = getattr(mod, "mod_type", None)
        if mod_type == ModType.LUDEON:
            return self._icon("ludeon")
        elif mod_type == ModType.STEAM_WORKSHOP:
            return self._icon("steam")
        elif mod_type == ModType.GIT:
            return self._icon("git")
        elif mod_type == ModType.STEAM_CMD:
            return self._icon("steamcmd")
        elif mod_type == ModType.LOCAL:
            return self._icon("local")
        return self._icon("local")

    def _source_tooltip(self, mod: Any) -> str:
        if mod is None:
            return ""
        mod_type = getattr(mod, "mod_type", None)
        if mod_type == ModType.LUDEON:
            return "Expansion"
        elif mod_type == ModType.STEAM_WORKSHOP:
            return "Subscribed via Steam Workshop"
        elif mod_type == ModType.GIT:
            return "Git repository"
        elif mod_type == ModType.STEAM_CMD:
            return "SteamCMD"
        elif mod_type == ModType.LOCAL:
            return "Installed locally"
        return "Installed locally"

    def _type_icon(self, mod: Any) -> QIcon | None:
        if mod is None:
            return None
        if not self.settings.mod_type_filter:
            return None
        if getattr(mod, "c_sharp_mod", False):
            return self._icon("csharp")
        return self._icon("xml")

    # ------------------------------------------------------------------
    # Icon cache
    # ------------------------------------------------------------------

    _icon_cache: dict[str, QIcon] = {}

    def _icon(self, name: str) -> QIcon:
        if name not in self._icon_cache:
            _map = {
                "error": ModListIcons.error_icon,
                "warning": ModListIcons.warning_icon,
                "new": ModListIcons.new_icon,
                "in_save": ModListIcons.clear_icon,
                "updated": ModListIcons.updated_icon,
                "ludeon": ModListIcons.ludeon_icon,
                "local": ModListIcons.local_icon,
                "steam": ModListIcons.steam_icon,
                "git": ModListIcons.git_icon,
                "steamcmd": ModListIcons.steamcmd_icon,
                "csharp": ModListIcons.csharp_icon,
                "xml": ModListIcons.xml_icon,
                "translation": ModListIcons.clear_icon,
                "tag": ModListIcons.clear_icon,
            }
            factory = _map.get(name)
            self._icon_cache[name] = factory() if factory is not None else QIcon()
        return self._icon_cache[name]

    # ------------------------------------------------------------------
    # Tooltip handling
    # ------------------------------------------------------------------

    def helpEvent(
        self,
        event: QHelpEvent,
        view: QAbstractItemView,
        option: QStyleOptionViewItem,
        index: QModelIndex,
    ) -> bool:
        if event.type() != QEvent.Type.ToolTip:
            return super().helpEvent(event, view, option, index)

        uuid = index.data(Qt.ItemDataRole.UserRole + 1)
        if uuid and is_divider_uuid(uuid):
            return False

        rect = option.rect
        pos = event.pos()
        state = self._state_for(uuid)

        # Check right-side icons (painted right-to-left)
        icons_right = self._collect_right_icons(uuid, state)
        icon_x = rect.right() - _MARGIN
        for icon, tooltip in icons_right:
            icon_x -= _ICON_SIZE
            icon_rect = QRect(
                icon_x,
                rect.top() + (rect.height() - _ICON_SIZE) // 2,
                _ICON_SIZE,
                _ICON_SIZE,
            )
            if icon_rect.contains(pos):
                QToolTip.showText(event.globalPos(), tooltip, view)
                return True
            icon_x -= _ICON_SPACING

        # Check left-side icons (source, type)
        name_left = rect.left() + _MARGIN
        mod = self.metadata_controller.get_mod(uuid) if uuid else None

        source_icon = self._source_icon(mod)
        if source_icon:
            icon_rect = QRect(
                name_left,
                rect.top() + (rect.height() - _ICON_SIZE) // 2,
                _ICON_SIZE,
                _ICON_SIZE,
            )
            if icon_rect.contains(pos):
                tooltip = self._source_tooltip(mod)
                QToolTip.showText(event.globalPos(), tooltip, view)
                return True
            name_left += _ICON_SIZE + _ICON_SPACING

        type_icon = self._type_icon(mod)
        if type_icon:
            icon_rect = QRect(
                name_left,
                rect.top() + (rect.height() - _ICON_SIZE) // 2,
                _ICON_SIZE,
                _ICON_SIZE,
            )
            if icon_rect.contains(pos):
                tooltip = "C# mod" if mod and mod.c_sharp_mod else "XML mod"
                QToolTip.showText(event.globalPos(), tooltip, view)
                return True

        # Default: general mod tooltip
        if mod:
            name = mod.name or "Unknown"
            path = str(mod.mod_path) if mod.mod_path else ""
            tooltip = f"Mod: {name}\nPath: {path}"
        else:
            tooltip = ""
        QToolTip.showText(event.globalPos(), tooltip, view)
        return True

    # ------------------------------------------------------------------
    # Events (handled by ModListWidget.eventFilter)
    # ------------------------------------------------------------------

    def editorEvent(
        self,
        event: Any,
        model: Any,
        option: QStyleOptionViewItem,
        index: QModelIndex,
    ) -> bool:
        return False
