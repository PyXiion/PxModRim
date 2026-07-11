from __future__ import annotations

from typing import Any

from PySide6.QtCore import QModelIndex, QSortFilterProxyModel, Qt

from app.models.divider import is_divider_uuid
from app.models.mods_panel_sort_key import ModsPanelSortKey
from app.views.mod_list_model import UUID_ROLE


class ModListFilterProxyModel(QSortFilterProxyModel):
    """
    QSortFilterProxyModel that supports:
    - Search text filtering (by mod name / package ID / author)
    - Tag filtering
    - Column sorting (by ModsPanelSortKey)
    """

    def __init__(self, metadata_controller: Any, parent: Any = None) -> None:
        super().__init__(parent)
        self.metadata_controller = metadata_controller
        self._search_text: str = ""
        self._tag_filter: set[str] = set()
        self._sort_key: ModsPanelSortKey | None = None
        self._sort_descending: bool = False

    # --- Filter setters ---

    def set_search_text(self, text: str) -> None:
        self._search_text = text.lower()
        self.invalidateFilter()

    def set_tag_filter(self, tags: set[str]) -> None:
        self._tag_filter = tags
        self.invalidateFilter()

    def clear_filters(self) -> None:
        self._search_text = ""
        self._tag_filter = set()
        self.invalidateFilter()

    # --- Sort ---

    def set_sort(self, key: ModsPanelSortKey, descending: bool) -> None:
        self._sort_key = key
        self._sort_descending = descending
        self.sort(
            0,
            Qt.SortOrder.DescendingOrder if descending else Qt.SortOrder.AscendingOrder,
        )
        self.invalidate()

    # --- Filter logic ---

    def filterAcceptsRow(self, source_row: int, source_parent: QModelIndex) -> bool:
        source_model = self.sourceModel()
        if source_model is None:
            return True
        index = source_model.index(source_row, 0, source_parent)
        uuid = index.data(UUID_ROLE)

        if is_divider_uuid(uuid):
            return True

        mod = self.metadata_controller.get_mod(uuid) if uuid else None
        if mod is None:
            return True

        if self._search_text:
            name = (mod.name or "").lower()
            package_id = (
                str(mod.package_id).lower()
                if hasattr(mod, "package_id") and mod.package_id
                else ""
            )
            authors = ""
            if hasattr(mod, "authors") and mod.authors:
                authors = " ".join(mod.authors).lower()
            if (
                self._search_text not in name
                and self._search_text not in package_id
                and self._search_text not in authors
            ):
                return False

        if self._tag_filter:
            from app.utils.auxdb import auxdb_get_mod_tags

            mod_tags = set(auxdb_get_mod_tags(self.metadata_controller.settings, uuid))
            if not mod_tags & self._tag_filter:
                return False

        return True

    # --- Sort logic ---

    def lessThan(self, left: QModelIndex, right: QModelIndex) -> bool:
        source_model = self.sourceModel()
        if source_model is None:
            return super().lessThan(left, right)

        left_uuid = source_model.data(left, UUID_ROLE)
        right_uuid = source_model.data(right, UUID_ROLE)
        left_is_divider = is_divider_uuid(left_uuid)
        right_is_divider = is_divider_uuid(right_uuid)

        if left_is_divider and right_is_divider:
            return left.row() < right.row()
        if left_is_divider:
            return True
        if right_is_divider:
            return False

        if self._sort_key is None:
            return super().lessThan(left, right)

        mods_metadata = (
            self.metadata_controller.mods_metadata
            if hasattr(self.metadata_controller, "mods_metadata")
            else {}
        )
        left_meta = mods_metadata.get(left_uuid, {})
        right_meta = mods_metadata.get(right_uuid, {})

        def _sort_val(meta: dict[str, Any], key: ModsPanelSortKey) -> Any:
            mod = self.metadata_controller.get_mod(meta.get("path", ""))
            if key == ModsPanelSortKey.MODNAME:
                return (meta.get("name", "") or "").lower()
            elif key == ModsPanelSortKey.AUTHOR:
                if mod and hasattr(mod, "authors") and mod.authors:
                    return " ".join(mod.authors).lower()
                return ""
            elif key == ModsPanelSortKey.PACKAGEID:
                if mod and hasattr(mod, "package_id"):
                    return str(mod.package_id).lower()
                return meta.get("packageid", "").lower()
            elif key == ModsPanelSortKey.VERSION:
                if mod and hasattr(mod, "mod_version") and mod.mod_version:
                    return mod.mod_version
                return ""
            elif key == ModsPanelSortKey.FOLDER_SIZE:
                return meta.get("folder_size", 0) or 0
            elif key == ModsPanelSortKey.FILESYSTEM_MODIFIED_TIME:
                return meta.get("internal_time_touched", 0) or 0
            elif key == ModsPanelSortKey.MOD_UPDATED:
                return meta.get("external_time_updated", 0) or 0
            elif key == ModsPanelSortKey.MOD_TAGS:
                from app.utils.auxdb import auxdb_get_mod_tags

                tags = auxdb_get_mod_tags(self.metadata_controller.settings, left_uuid)
                return ", ".join(sorted(tags)).lower()
            return ""

        left_val = _sort_val(left_meta, self._sort_key)
        right_val = _sort_val(right_meta, self._sort_key)

        try:
            return left_val < right_val
        except TypeError:
            return str(left_val) < str(right_val)
