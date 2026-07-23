# RimSort Steam Workshop CF Worker

Worker that resolves RimWorld Steam Workshop mod dependency trees. Primary data source is Steam WebAPI (
`IPublishedFileService/GetDetails/v1`), with ScrapingAnt HTML scraping as fallback. D1 caches results. Optional
Cloudflare Queue handles background refresh.

## Exports

| Export      | Trigger          | Purpose                                                                    |
|-------------|------------------|----------------------------------------------------------------------------|
| `fetch`     | HTTP request     | `GET /` — health check; `GET /deps?id=<steamid>` — dependency tree         |
| `scheduled` | Cron             | One `QueryFiles` page fetch + one `GetDetails` batch (200) scrape per tick |
| `queue`     | Cloudflare Queue | Background re-scrape of incomplete trees                                   |

## Environment bindings

| Binding         | Required      | Description                                       |
|-----------------|---------------|---------------------------------------------------|
| `STEAM_API_KEY` | Yes           | Steam WebAPI key (fallback: ScrapingAnt only)     |
| `DB`            | Yes           | D1 database binding                               |
| `SCRAPI`        | Fallback only | ScrapingAnt API key                               |
| `SCRAPE_QUEUE`  | Optional      | Cloudflare Queue binding for background rescrapes |
| `USE_QUEUE`     | Optional      | Set to `"true"` to enable queue path              |
| `CONTROL_KEYS`  | `/purge` only | JSON array of admin keys, e.g. `["abc123"]`      |

## D1 schema

### `items` — mod cache

```sql
CREATE TABLE items (
  id TEXT PRIMARY KEY,
  title TEXT NOT NULL DEFAULT '',
  is_collection INTEGER NOT NULL DEFAULT 0,
  status TEXT NOT NULL DEFAULT '',
  updated_at INTEGER NOT NULL DEFAULT 0,
  package_id TEXT NOT NULL DEFAULT '',
  deps TEXT NOT NULL DEFAULT ''
);
```

`deps` is a comma-separated list of Steam workshop IDs. Collections (`is_collection=1`) have `deps=''` when they appear as sub-dependencies. Root collections are an exception — `ensureCollectionRootDeps` re-fetches the root's children from Steam API and writes them to `deps`.

### `crawl_state` — cron pagination cursor

```sql
CREATE TABLE crawl_state (
  id INTEGER PRIMARY KEY DEFAULT 1,
  cursor TEXT NOT NULL DEFAULT '*',
  todo_ids TEXT NOT NULL DEFAULT '',
  total_processed INTEGER NOT NULL DEFAULT 0,
  updated_at INTEGER NOT NULL DEFAULT 0
);
```

Single-row table (id=1). `todo_ids` is comma-separated IDs from the current `QueryFiles` page awaiting scrape. Seeded on
first access if missing.

## Status codes

| status                     | Meaning                        |
|----------------------------|--------------------------------|
| `OK`                       | Successfully scraped           |
| `PENDING`                  | Not yet scraped                |
| `PRIVATE`                  | Steam result=15 or hidden page |
| `DELETED`                  | Not in API response            |
| `ERROR_RESULT_{code}`      | Steam returned non-1 result    |
| `INVALID_PAGE`             | HTML scrape returned no title  |
| `SCRAPE_ERROR_HTTP_{code}` | ScrapingAnt HTTP error         |

## Data flow

```
Request /deps?id=X
  │
  ├─ prewarmCacheFromD1(DB, X)
  │   BFS traversal via D1 batch SELECT (99/batch, 200 per step)
  │   Populates GLOBAL_RAM_CACHE (LRUMap, 10k cap)
  │
  ├─ buildTree(X)
  │   Walks cache: fresh OK → itemsMap; expired/missing → missingIds
  │
  ├─ scrapeInline(env, missingIds)
  │   BFS batch scrape with two tiers:
  │     1. fetchSteamApiDetails — batched GetDetails (200/batch, 800 cap)
  │     2. scrapeViaScrapingAnt — HTML fallback per-ID
  │   Results → saveToDB → GLOBAL_RAM_CACHE
  │
  ├─ (optional queue enqueue if missing remain)
  │
  └─ JSON response { rootId, totalItemsLoaded, isComplete, items }
```

### Key behaviors

- **Deps from Steam API**: `children` from `GetDetails` is parsed for ALL mods (both individual and collections). The API returns required items as `children` for individual mods, and collection items as `children` for collections.
- **Collection deps stripped**: When saving a mod via `scrapeInline`, collection children are stripped unless the mod is the root (`mod.id === rootId`). This prevents sub-collections from expanding, while the root collection's children are preserved.
- **Root collection fallback**: `ensureCollectionRootDeps` handles pre-cached roots — if a root collection was cached without children (from a previous scrape), it re-fetches them and updates D1 + RAM cache.
- **ScrapingAnt fallback**: Only used when Steam API call fails entirely (null response). Individual mods get
  `RequiredItems` HTML parsing for deps; collections get `deps = []`.
- **BFS expansion**: Only from `deps` in cached/scraped results. Since collections have no deps and individual mods have
  no deps from Steam API, BFS only expands via ScrapingAnt-fallback individual mods' `RequiredItems`.

## Constants

| Constant               | Value | Description                             |
|------------------------|-------|-----------------------------------------|
| `CACHE_TTL_OK_MOD`         | 72h   | Fresh OK mod entry lifetime                 |
| `CACHE_TTL_OK_COLLECTION` | 1h    | Fresh OK collection entry lifetime          |
| `CACHE_TTL_ERROR`      | 1h    | Error entry lifetime                    |
| `QUEUE_DEDUP_TTL_MS`   | 60s   | Dedup window for re-enqueuing same root |
| `MAX_INLINE_SCRAPE`    | 800   | Max mods scraped per `/deps` request    |
| `STEAM_API_BATCH_SIZE` | 200   | Mods per GetDetails call                |
| `QUERY_MODS_PAGE_SIZE` | 3000  | Mods per QueryFiles call (crawl)        |
| `CRAWL_BATCH_SIZE`     | 200   | Mods scraped per cron tick              |
| `LRU maxSize`          | 10000 | GLOBAL_RAM_CACHE capacity               |

## Crawl system (`scheduled`)

Per tick:

1. Read `crawl_state` from D1
2. If `todo_ids` empty → `queryMods(cursor)` → `findStaleIds` (missing + expired) → store as `todo_ids`
3. Slice 200 from `todo_ids` → `scrapeInline` → increment total
4. Save state. If page consumed + `cursor=""` → reset to `"*"`

Cron schedule is configured in `wrangler.toml` (not in repo — user-managed).

## Queue system

Enabled when `USE_QUEUE="true"` and `SCRAPE_QUEUE` binding exists. Handler sends a single `{ rootId }` message per
incomplete tree. Queue consumer runs `scrapeInline` + `buildTree`; if still incomplete, re-enqueues with 30s
`deliveryDelay`. 60s dedup via `RECENTLY_QUEUED` Map.

## Endpoints

### `GET /deps?id=<steamid>`

- Response: `{ rootId, totalItemsLoaded, isComplete, items: { [id]: {...} } }`
- Each item: `{ id, title, is_collection, is_available, status, deps, package_id }`

### `GET /purge?id=<steamid>&key=<admin_key>`

- Purges the cached response for a specific mod from the edge cache.
- Requires `key` matching a value in `CONTROL_KEYS` env var.
- Response: `Purged <steamid>` or `401 Unauthorized`.

### `GET /`

- Response: `ok`
