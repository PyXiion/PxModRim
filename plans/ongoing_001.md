# Ongoing 001: Architecture — Core, Providers, Clean DI

## Цель
Разделить бизнес-логику и UI. MainWindow — тонкий презентер.
Ввести `core/` с контекстом, провайдерами и сервисом. Чистый DI без синглтонов.

## Архитектура

```
src/pxmodrim/
  core/
    ├── __init__.py
    ├── context.py           ← CoreContext (состояние: mods + active_uuids + cfg)
    ├── mod_service.py       ← ModService (I/O: discover + save)
    ├── structures.py        ← CollectionStats (msgspec.Struct, чистый data)
    └── providers/
        ├── __init__.py      ← create_providers(cfg) → list[BaseModProvider]
        ├── base.py          ← BaseModProvider (ABC)
        ├── core.py          ← CoreModProvider   (game/Mods)
        ├── local.py         ← LocalModProvider  (local_path)
        └── workshop.py      ← WorkshopModProvider (workshop_path)
  _compat/
    ├── config.py            ← config_folder + детект (уже сделано)
    ├── mods_config.py       ← ModsConfig.xml парсинг/запись (уже сделано)
    └── xml.py               ← lxml утилиты (уже сделано)
  services/
    └── mod_discovery.py     ← scan_mod_directory(), resolve_active_uuids()
  ui/
    ├── main_window.py       ← только UI, держит CoreContext + ModService через DI
    ├── mod_list_panel.py    ← кастомный delegate
    ├── mod_info_panel.py    ← preview + meta
    ├── sidebar_panel.py     ← фильтры
    ├── menu_bar.py          ← редко используемые действия
    └── settings_panel.py    ← настройки
  _app.py                    ← сборка DI

## Компоненты

### `core/context.py`

```python
class CoreContext:
    _mods: dict[str, ListedMod] = {}
    _active_uuids: set[str] = set()
    _cfg: AppConfig

    def load(self, mods: dict, active: set[str]) -> None
    def update_config(self, cfg: AppConfig) -> None

    @property
    def all_mods(self) -> dict[str, ListedMod]
    @property
    def active_uuids(self) -> set[str]
    @property
    def config(self) -> AppConfig

    def compute_stats(self, latest_active_ids: set[str]) -> CollectionStats:
        """pure — ни одного I/O"""
```

### `core/structures.py`

```python
class CollectionStats(msgspec.Struct):
    total: int = 0
    active: int = 0
    inactive: int = 0
    local: int = 0
    steam: int = 0
    errors: int = 0
```

### `core/providers/base.py`

```python
class BaseModProvider(ABC):
    provider_id: str  # "core" | "local" | "workshop"

    @abstractmethod
    async def discover(self, target_version: str) -> dict[str, ListedMod]:
        ...

    async def delete(self, mod_id: str) -> bool:
        return False

    async def update(self, mod_id: str) -> bool:
        return False
```

### `core/providers/__init__.py`

```python
def create_providers(cfg: PathConfig) -> list[BaseModProvider]:
    providers: list[BaseModProvider] = []
    if cfg.game:
        providers.append(CoreModProvider(Path(cfg.game)))
    if cfg.local:
        providers.append(LocalModProvider(Path(cfg.local)))
    if cfg.workshop:
        providers.append(WorkshopModProvider(Path(cfg.workshop)))
    return providers
```

Каждый провайдер внутри вызывает `scan_mod_directory()` + `create_listed_mod_from_path()`.
Каждый сам выставляет `ModType` под себя.

### `core/mod_service.py`

```python
class ModService:
    def __init__(self, ctx: CoreContext, providers: list[BaseModProvider]) -> None:
        self._ctx = ctx
        self._providers = providers

    async def discover(self) -> None:
        """I/O: все провайдеры → resolve active → ctx.load()"""
        all_mods = {}
        for p in self._providers:
            mods = await p.discover(self._ctx.config.target_version)
            all_mods.update(mods)
        active = resolve_active_uuids(all_mods, self._ctx.config.paths.config_folder)
        self._ctx.load(all_mods, active)

    def compute_stats(self, active_ids: set[str]) -> CollectionStats:
        """pure: делегирует context"""
        return self._ctx.compute_stats(active_ids)

    async def save_active_layout(self, active_ids: list[str]) -> bool:
        """I/O: resolve package_ids из ctx.all_mods → write_mods_config"""
```

### `ui/main_window.py`

```python
class MainWindow(QMainWindow):
    BADGE_ALL = 0; BADGE_ACTIVE = 1; BADGE_STEAM = 2
    BADGE_LOCAL = 3; BADGE_INACTIVE = 4; BADGE_ERRORS = 5

    def __init__(self, ctx: CoreContext, mod_service: ModService) -> None
        self._ctx = ctx
        self._mod_service = mod_service

    async def load_mods_async(self) -> None:
        await self._mod_service.discover()
        self.mod_list.load_mods(self._ctx.all_mods, self._ctx.active_uuids)
        stats = self._mod_service.compute_stats(self._ctx.active_uuids)
        self._update_sidebar_badges(stats)

    def _on_active_mods_changed(self) -> None:
        ids = self.mod_list.active_uuids()
        stats = self._mod_service.compute_stats(set(ids))
        self._update_sidebar_badges(stats)

    def _save_mods_config(self) -> None:
        ids = self.mod_list.active_uuids()
        ok = await self._mod_service.save_active_layout(ids)
        self.status_bar.showMessage(...)
```

**Из MainWindow убираются все импорты из:** `Path`, `mods_config`, `mod_discovery`, `AboutXmlMod`, `ListedMod`, `ModType`, `msgspec`.

### `_app.py`

```python
class App:
    def __init__(self):
        cfg = load_config()
        if not cfg.paths.game:
            detected = detect_game_paths()
            if detected.game:
                cfg.paths = detected
                save_config(cfg)

        ctx = CoreContext(cfg)
        providers = create_providers(cfg.paths)
        mod_service = ModService(ctx, providers)
        self.main_window = MainWindow(ctx, mod_service)

    async def async_run(self):
        self.main_window.show()
        if not cfg.paths.game:
            result, dialog = await await_dialog(SettingsPanel, cfg)
            ...
            save_config(cfg)
            ctx.update_config(cfg)

        await self.main_window.load_mods_async()
        await app_close_event.wait()
```

## Файлы: создание / изменение / удаление

| Действие | Файл |
|---|---|
| **Создать** | `core/__init__.py` |
| **Создать** | `core/context.py` — CoreContext |
| **Создать** | `core/structures.py` — CollectionStats |
| **Создать** | `core/mod_service.py` — ModService |
| **Создать** | `core/providers/__init__.py` — create_providers |
| **Создать** | `core/providers/base.py` — BaseModProvider |
| **Создать** | `core/providers/core.py` — CoreModProvider |
| **Создать** | `core/providers/local.py` — LocalModProvider |
| **Создать** | `core/providers/workshop.py` — WorkshopModProvider |
| **Изменить** | `ui/main_window.py` — DI через ctx + mod_service |
| **Изменить** | `_app.py` — сборка DI |
| **Не трогать** | `_compat/config.py`, `_compat/mods_config.py`, `_compat/xml.py` |
| **Не трогать** | `ui/mod_list_panel.py`, `ui/mod_info_panel.py`, `ui/sidebar_panel.py`, `ui/menu_bar.py`, `ui/settings_panel.py` |
| **Не трогать** | `services/mod_discovery.py` — остаётся как utilities |

## Граф зависимостей

```
_app.py
  └── core/context.py        ─── _compat/config.py (AppConfig)
  └── core/mod_service.py    ─── core/context.py
  │                          ─── core/providers/* (через BaseModProvider)
  │                          ─── services/mod_discovery.py (resolve_active_uuids)
  │                          ─── _compat/mods_config.py (write_mods_config)
  └── ui/main_window.py      ─── core/context.py
                             ─── core/mod_service.py

core/providers/core.py       ─── services/mod_discovery.py (scan_mod_directory)
                             ─── models/metadata/parsing.py (create_listed_mod_from_path)
core/providers/local.py      ─── то же
core/providers/workshop.py   ─── то же
```

MainWindow НЕ знает про `services/`, `_compat/`, `models/`. Только про `core/`.

## Тестируемость

```python
# context
ctx = CoreContext(AppConfig())
ctx.load({"a": ListedMod(...)}, {"a"})
stats = ctx.compute_stats({"a"})
assert stats.total == 1 and stats.active == 1

# mod_service с мок-провайдером
ctx = CoreContext(AppConfig())
providers = [MockProvider(...)]
svc = ModService(ctx, providers)
await svc.discover()
assert len(ctx.all_mods) == expected

# provider с подставной директорией
provider = CoreModProvider(tmp_path)
mods = await provider.discover("1.5")
assert len(mods) > 0
```

## Следующий шаг (после реализации)

Добавление `SteamWorkshopModProvider` (через Steam API) — новый файл в `core/providers/`,
передача его в `create_providers()`. Никаких изменений в UI.
