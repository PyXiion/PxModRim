# PxModRim Workshop backend

TypeScript Cloudflare Worker for the native Workshop browser and the existing dependency resolver. Public catalog data comes from Steam; PxModRim-picked collections are managed in D1. There is no demo catalog or production seed data.

This is the backend only. Installed state, comparing installed timestamps against Workshop metadata, downloading files, and activation remain client responsibilities. The Worker neither subscribes a Steam account nor downloads mod archives.

## Local development with Wrangler

Requires Node.js 22+ and npm. Run these commands from `cf-workers/`:

```sh
npm ci
npm run db:migrate
npm run dev
```

`wrangler.local.jsonc` binds a local D1 database; its `local-only` database ID is deliberately not a production database ID. The migration script uses `--local`. The health check and picked-collection listing work without Steam credentials.

For Steam-backed requests, create a gitignored `.dev.vars`:

```dotenv
STEAM_API_KEY="your-steam-web-api-key"
CONTROL_KEYS='["a-long-random-admin-token"]'
```

Do not distribute `CONTROL_KEYS` to application clients. They authorize picked-collection writes and the existing cache-purge endpoint.

```sh
curl 'http://localhost:8787/'
curl 'http://localhost:8787/catalog/collections?source=picked'
curl 'http://localhost:8787/catalog/mods?version=1.6&sort=popular&limit=24'
```

Verification:

```sh
npm run check   # strict TypeScript checking
npm run build   # Wrangler deployment dry-run; bundles dist/worker.js
npm test        # builds first, then runs the bundled Worker in Miniflare/workerd with D1
```

Tests intercept Steam HTTP requests with explicit fixtures. They exercise metadata boundaries, collection kinds, pagination, authentication, curation persistence, dependency expansion, cache expiry, error handling, and the existing `/deps` response. They do not demonstrate live Steam credentials or a remote deployment.

## Deployment

Production Wrangler configuration remains user-managed. Point its `main` at `cf-workers/worker.ts` and its D1 `migrations_dir` at `cf-workers/migrations`, resolving both relative to that configuration file. Keep the existing real database ID, routes, cron schedule, and optional queue bindings. Apply the additive migrations before deploying:

```sh
npx wrangler d1 migrations apply DB --remote --config /path/to/production/wrangler.toml
npx wrangler deploy --config /path/to/production/wrangler.toml
```

These commands change production; local development and `npm run build` do not execute them. Set secrets using `wrangler secret put NAME --config /path/to/production/wrangler.toml`. No production database ID or secret is checked into this directory.

| Binding | Requirement | Purpose |
|---|---|---|
| `DB` | Required | Dependency cache, catalog metadata, and picked collections |
| `STEAM_API_KEY` | Uncached Steam requests and crawling | Steam WebAPI key |
| `CONTROL_KEYS` | Admin operations | JSON array of non-empty admin tokens |
| `SCRAPI` | Optional, legacy resolver only | ScrapingAnt HTML fallback key |
| `SCRAPE_QUEUE` | Optional | Background dependency refresh queue |
| `USE_QUEUE` | Optional | Set to `"true"` to enable the queue path |

Exports remain `fetch`, `scheduled`, and `queue`. Catalog routes use Steam directly; they do not scrape Steam HTML or use ScrapingAnt.

## Catalog API

All Workshop IDs and file sizes are decimal **strings**, not JavaScript numbers. Mod IDs are bare Workshop IDs; collection IDs are namespaced as `steam:<Workshop-ID>` or `picked:<slug>`. Timestamps are Unix seconds. Public results exclude unavailable, private, banned, non-RimWorld, and non-mod/non-collection Steam files.

Catalog responses are JSON with `Cache-Control: no-store` and `X-Content-Type-Options: nosniff`. Steam query pages (`query_cache`) and metadata (`catalog_items`) are stored in D1, and browsing never waits for a refresh: a query page older than five minutes and a mod or collection detail older than fifteen minutes are returned as stored while a background refresh runs (pages are kept for a week). Only data that was never stored waits for Steam. Curated listings are read from D1 on each request, so writes and deletes are not hidden by a public response cache.

Successful, complete Steam detail refreshes evict cached files that are no longer public, including their local search entries. Subsequent detail reads return 404, and batch/resolve and collection member responses include the evicted IDs in `unavailable_ids`. Failed or incomplete upstream responses retain cached records; PxModRim-picked collections are never evicted by Steam refreshes. Migration `0005_catalog_eviction.sql` connects metadata deletion to the existing search-index/FTS deletion triggers.

### Discovery and browsing

| Method | Path | Response |
|---|---|---|
| GET | `/catalog/discover` | `{ mods: Page<Mod>, collections: Page<Collection> }`; four mods and up to three mixed-source collections |
| GET | `/catalog/mods` | `Page<Mod>` |
| GET | `/catalog/collections` | `Page<Collection>`; `source=all` (default), `steam`, or `picked` |

A page is `{ items, total, next_cursor }`; `next_cursor: null` ends pagination. Steam totals reflect Steam's query count and can include entries subsequently excluded by public metadata validation. Picked totals count matching stored collections.

Browsing parameters:

- `q`: text, at most 200 characters. Steam matches titles/descriptions, **not creator nicknames**. Picked collections also search the curator field; `%` and `_` are literal characters, not SQL wildcards.
- `tag`: exact tag, at most 100 characters.
- `version`: version tag such as `1.6`.
- `sort`: `popular` (default), `updated`, `newest`, `trending`, or `relevance`.
- `limit`: 1–50, default 24.
- `cursor`: use the previous `next_cursor`; omit for the first page.

Steam sort modes use its corresponding query rankings; trending uses seven days. Picks use `featured_rank` for popular/trending/relevance, creation time for newest, and modification time for updated. Mixed collection pages reserve room for both sources and continue the remaining source when the other is exhausted. Reuse a collection cursor with the same source, search, filters, sort, and limit; mismatched queries return 400. Text matching for picks follows SQLite's built-in `lower()` behavior, not full Unicode case folding.

Discovery does not accept a continuation cursor. Start the browsing endpoints with their own page size when opening a tab; if consuming discovery's returned page cursors directly, retain their original limits (four mods / three collections).

### Details and installed-mod metadata

| Method | Path | Response |
|---|---|---|
| GET | `/catalog/mods/:id` | A mod, or 404 |
| POST | `/catalog/mods/batch` | `{ items: Mod[], unavailable_ids: string[] }` |
| GET | `/catalog/collections/steam/:id` | `{ collection, members, unavailable_ids, is_complete }` |
| GET | `/catalog/collections/picked/:slug` | Same detail envelope |

Batch request: `{ "ids": ["2009463077", "818773962"] }`, at most 100 IDs. Metadata older than fifteen minutes is refetched from Steam before answering, so the client can compare each mod's `updated_at` against its installed copy. `/catalog/resolve` behaves the same.

Mods expose title, Steam author/profile, description, previews, Workshop URL, tags, supported versions, publication/update times, file size, subscription count, votes, dependency IDs, package ID when provided by Steam, and incompatibility status. Missing optional data remains `null`; unrated mods do not receive an invented approval percentage.

Collections expose source, author, description, preview, tags, supported versions, ordered direct member IDs, member count, and timestamps. A Steam member may itself be a collection. Detail responses hydrate available direct members only; download resolution expands the complete nested graph. A member-count mismatch or unavailable member makes `is_complete` false.

Collections without a primary preview include up to four `member_previews` in both listing and detail responses. Detail thumbnails reuse the already hydrated direct members rather than fetching them again.

Steam descriptions have `description_format: "bbcode"`; picked descriptions use `"text"`. Treat both as untrusted content in clients, not executable HTML. Author names come from `ISteamUser/GetPlayerSummaries`; missing names are `null`, with the Steam ID retained. Do not infer local installed status from any catalog field.

### Download resolution

```http
POST /catalog/resolve
Content-Type: application/json

{"ids":[],"collection_ids":["steam:123456789","picked:starter-pack"]}
```

Examples illustrate the request format; replace IDs/slugs with actual public items and existing picks.

- `ids`: optional array of up to 50 Workshop IDs.
- `collection_ids`: optional array of up to ten namespaced collection IDs.
- At least one root is required. An empty explicit mod selection is valid when collections are present.
- Expands transitive required mods, nested Steam collections, and picked collections.
- Deduplicates IDs and terminates cycles. `mod_ids` is dependency-first where the graph is acyclic; a cycle has no valid topological order.
- Returns `{ roots, items, mod_ids, unavailable_ids, incomplete_collection_ids, is_complete }`.
- `items` uses mod IDs and namespaced collection IDs as keys; `mod_ids` contains only downloadable mods, never collection IDs.
- Missing/private files or incomplete collection membership set `is_complete: false`. A Steam collection ID referring to a mod returns 400.
- Graphs exceeding 1,000 Steam items return 413 rather than silently truncating the download plan.

The client downloads `mod_ids` and decides how to present an incomplete plan. Resolution does not activate mods or modify subscriptions.

### Managing PxModRim picks

`PUT /catalog/collections/picked/:slug` creates or replaces a collection. `DELETE` removes it. Both require `Authorization: Bearer <token>` matching `CONTROL_KEYS`. Slugs are lowercase letters/digits separated by single dashes, at most 80 characters.

```json
{
  "title": "Starter pack",
  "author": "PxModRim",
  "description": "A curated set of quality-of-life mods.",
  "preview_url": null,
  "tags": ["Quality of Life"],
  "supported_versions": ["1.6"],
  "member_ids": ["2009463077", "818773962"],
  "featured_rank": 10
}
```

Required: title (1–200 characters), author (1–120), description (up to 20,000), and 1–500 member IDs. Optional: HTTPS preview URL, up to 30 tags (100 characters each), up to 20 numeric version tags, and integer rank 0–1,000,000 (lower ranks first). Duplicate members are removed while retaining their first position. Every member must resolve to an available public RimWorld **mod**, not a collection. Unknown fields are rejected.

Successful PUT returns the stored collection with HTTP 200 and preserves its original `created_at`; DELETE returns 204, or 404 for an absent slug. There is no public write or client-embedded admin token.

### Errors and request limits

Errors use `{ "error": { "message": "..." } }`. Invalid inputs return 400, unauthorized writes 401, missing items/routes 404, unsupported methods 405, oversized bodies/graphs 413, and unsupported content types 415. Steam rate limiting, rejected/missing credentials return 503; malformed/unavailable upstream responses return 502. Unexpected server errors return a sanitized 500.

JSON requests require `application/json` and are bounded to 64 KiB, including streamed bodies. Numeric JSON IDs, noncanonical/zero/out-of-range uint64 IDs, malformed cursors, and unknown payload fields are rejected. Secrets are not returned in upstream errors. The backend does not turn unavailable Steam data into a successful empty catalog.

## D1 migrations

- `0001_dependencies.sql`: existing `items` and `crawl_state` tables, created only when absent. Existing dependency data is preserved.
- `0002_catalog.sql`: `catalog_items` metadata cache and `picked_collections` with JSON-array constraints and browsing indexes. No sample rows are inserted.
- `0003_search.sql`: `catalog_index` (mods seen by the catalog) with an FTS5 table, and `index_state` for the crawler. The scheduled handler walks the Workshop newest-updated first; `/catalog/index` reports `ready` once one full pass has finished. After that, mod searches and the popular/updated/newest listings (with tag and version filters) are answered from the index; trending, collections and cursors that do not start with `ix:` still go to Steam. Every mod the catalog fetches for any reason is indexed too.
  Full passes also confirm stored mods omitted by Steam's listing through fresh `GetDetails` requests, at most 100 per tick. Public files remain indexed; confirmed unavailable files are evicted from metadata and search. The verification phase walks ids in order, isolates files whose refresh keeps failing so later ids are still checked, and restarts the pass instead of completing while any omitted file is unverified.
- `0004_query_cache.sql`: `query_cache`, the ordered IDs, total and cursor of each Steam query page. Pages and metadata both live in D1, so they survive restarts and deploys and are identical at every edge.
- `0006_collection_sizes.sql`: `collection_sizes`, the denormalised total download size (bytes) of each collection, written when its details are read and only when every member is known and sized. Listings join it without reading members; triggers drop a row when the collection's members change or the collection is deleted. Collections without a row report `total_size: null`.

The catalog cache is separate from `items`: the legacy resolver intentionally strips nested collection children in some paths; catalog resolution retains them.

## Existing dependency resolver

`GET /deps?id=<Workshop-ID>` retains `{ rootId, totalItemsLoaded, isComplete, items }`. Each item retains `{ id, title, is_collection, is_available, status, deps, package_id }`. `GET /` still returns `ok`.

The resolver prewarms its 10,000-entry RAM cache from D1, traverses dependencies, and scrapes missing/expired entries inline. Steam `GetDetails` children supply both mod requirements and collection members. HTML scraping through ScrapingAnt is only a fallback when Steam detail fetching fails entirely. Non-root collection children remain stripped in this legacy path; root collection children are preserved/refetched as before.

| Setting | Value |
|---|---|
| Fresh mod cache | 72 hours |
| Fresh collection / error cache | 1 hour |
| Inline scrape cap | 800 items |
| Legacy Steam detail batch / cron scrape batch | 200 items |
| Cron query page | 3,000 items |
| Queue deduplication window | 60 seconds |

Stored statuses remain `OK`, `PENDING`, `PRIVATE`, `DELETED`, `ERROR_RESULT_<code>`, `INVALID_PAGE`, and `SCRAPE_ERROR_HTTP_<code>`.

The scheduled handler fetches a Workshop query page when needed, processes one batch, and persists `crawl_state`. The optional queue sends `{ rootId }`; its consumer refreshes the tree and re-enqueues incomplete work with Cloudflare's `delaySeconds: 30`. Cron/queue provisioning remains in production Wrangler configuration.

`GET /purge?id=<Workshop-ID>&key=<admin-key>` retains the existing authenticated edge-response purge behavior. It does not delete catalog metadata or picks; catalog metadata expires separately.

## Steam contracts

[IPublishedFileService documentation](https://partner.steamgames.com/doc/webapi/IPublishedFileService) defines `QueryFiles` parameters passed via `input_json`. Matching file type is 0 for items and 1 for collections, while returned `EWorkshopFileType` is 0 for mods and **2** for collections; these are different enums. Rich metadata/children fields follow the [Steam published-file protobuf definitions](https://github.com/SteamDatabase/Protobufs/blob/master/steam/steammessages_publishedfile.steamclient.proto).
