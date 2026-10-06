import { indexMods } from './search';
import { HttpError } from './types';
import type { Author, CatalogCollection, CatalogItem, CatalogMod, CatalogPage, Env, Preview, SteamFile } from './types';

const APP_ID = 294100;
const DETAIL_TTL_MS = 15 * 60 * 1000;
const QUERY_TTL_SECONDS = 5 * 60;
const API_BATCH_SIZE = 100;
const MAX_STEAM_ID = 18446744073709551615n;

export type SteamSort = 'popular' | 'updated' | 'newest' | 'trending' | 'relevance';
export interface SteamQuery {
  query: string;
  tag: string | null;
  version: string | null;
  sort: SteamSort;
  limit: number;
  cursor: string;
}

export function validateSteamId(id: string): void {
  if (!/^[1-9]\d{0,19}$/.test(id) || BigInt(id) > MAX_STEAM_ID) {
    throw new HttpError(400, 'Workshop IDs must be positive uint64 decimal strings');
  }
}

function steamUrl(env: Env, service: string, method: string, version = 1): URL {
  if (!env.STEAM_API_KEY) throw new HttpError(503, 'STEAM_API_KEY is required for uncached Steam catalog data');
  const url = new URL(`https://api.steampowered.com/${service}/${method}/v${version}/`);
  url.searchParams.set('key', env.STEAM_API_KEY);
  return url;
}

const STEAM_ATTEMPTS = 3;
const STEAM_ATTEMPT_TIMEOUT_MS = 4_000;

async function steamJson(url: URL): Promise<unknown> {
  for (let attempt = 1; ; attempt++) {
    const started = Date.now();
    let stage = 'headers';
    let status: number | null = null;
    try {
      const response = await fetch(url, { signal: AbortSignal.timeout(STEAM_ATTEMPT_TIMEOUT_MS) });
      status = response.status;
      if (response.status === 429) throw new HttpError(503, 'Steam API rate limit reached');
      if (response.status === 401 || response.status === 403) throw new HttpError(503, 'Steam API credentials were rejected');
      if (!response.ok) {
        stage = 'error-body';
        console.error(`Steam ${url.pathname} HTTP ${status} body: ${(await response.text()).slice(0, 300)}`);
        throw new HttpError(502, 'Steam API request failed');
      }
      stage = 'body';
      return await response.json();
    } catch (error) {
      const retryable = !(error instanceof HttpError) || error.status === 502;
      if (!(error instanceof HttpError)) {
        const reason = error instanceof Error ? `${error.name}: ${error.message}` : String(error);
        console.error(`Steam ${url.pathname} attempt ${attempt}/${STEAM_ATTEMPTS} failed at ${stage} after ${Date.now() - started}ms (status ${status ?? 'none'}): ${reason}`);
      }
      if (!retryable) throw error;
      if (attempt === STEAM_ATTEMPTS) {
        if (error instanceof HttpError) throw error;
        throw new HttpError(502, 'Steam API is unavailable or returned invalid JSON');
      }
    }
  }
}

function responseObject(value: unknown): Record<string, unknown> {
  if (!value || typeof value !== 'object' || !('response' in value)
    || !value.response || typeof value.response !== 'object') {
    throw new HttpError(502, 'Steam API returned an invalid response');
  }
  return value.response as Record<string, unknown>;
}

function invalidMetadata(): never {
  throw new HttpError(502, 'Steam API returned invalid file metadata');
}

function fileDetails(response: Record<string, unknown>): SteamFile[] {
  if (response.total === 0 && response.publishedfiledetails === undefined) return [];
  if (!Array.isArray(response.publishedfiledetails)) {
    throw new HttpError(502, 'Steam API returned invalid file details');
  }
  for (const value of response.publishedfiledetails as unknown[]) {
    if (!value || typeof value !== 'object' || !('publishedfileid' in value)
      || typeof value.publishedfileid !== 'string' || !('result' in value) || typeof value.result !== 'number') {
      throw new HttpError(502, 'Steam API returned invalid file details');
    }
    try { validateSteamId(value.publishedfileid); } catch { throw new HttpError(502, 'Steam API returned an invalid Workshop ID'); }
    const file = value as Record<string, unknown>;
    for (const key of ['creator', 'title', 'file_description', 'description', 'short_description', 'preview_url']) {
      if (file[key] !== undefined && typeof file[key] !== 'string') invalidMetadata();
    }
    for (const key of ['consumer_appid', 'creator_appid', 'file_type', 'time_created', 'time_updated', 'visibility', 'subscriptions', 'num_children']) {
      if (file[key] !== undefined && (typeof file[key] !== 'number' || !Number.isSafeInteger(file[key]) || file[key] < 0)) invalidMetadata();
    }
    for (const key of ['banned', 'incompatible']) if (file[key] !== undefined && typeof file[key] !== 'boolean') invalidMetadata();
    if (file.file_size !== undefined && !(typeof file.file_size === 'string' && /^\d+$/.test(file.file_size))
      && !(typeof file.file_size === 'number' && Number.isSafeInteger(file.file_size) && file.file_size >= 0)) invalidMetadata();
    for (const [key, required] of [['tags', ['tag']], ['kvtags', ['key', 'value']], ['children', ['publishedfileid']], ['previews', []]] as const) {
      const list = file[key];
      if (list === undefined) continue;
      if (!Array.isArray(list)) invalidMetadata();
      for (const entry of list as unknown[]) {
        if (!entry || typeof entry !== 'object') invalidMetadata();
        const record = entry as Record<string, unknown>;
        for (const field of required) if (typeof record[field] !== 'string') invalidMetadata();
        for (const field of ['url', 'youtubevideoid']) if (record[field] !== undefined && typeof record[field] !== 'string') invalidMetadata();
        for (const field of ['sortorder', 'preview_type']) if (record[field] !== undefined && (typeof record[field] !== 'number' || !Number.isSafeInteger(record[field]) || record[field] < 0)) invalidMetadata();
      }
    }
    if (file.vote_data !== undefined) {
      if (!file.vote_data || typeof file.vote_data !== 'object') invalidMetadata();
      const votes = file.vote_data as Record<string, unknown>;
      for (const key of ['votes_up', 'votes_down']) if (votes[key] !== undefined && (typeof votes[key] !== 'number' || !Number.isSafeInteger(votes[key]) || votes[key] < 0)) invalidMetadata();
    }
  }
  return response.publishedfiledetails as SteamFile[];
}

async function authorsFor(env: Env, details: SteamFile[]): Promise<Map<string, Author>> {
  const ids = [...new Set(details.map(file => file.creator).filter((id): id is string => typeof id === 'string' && /^[1-9]\d{0,19}$/.test(id)))];
  const authors = new Map<string, Author>();
  for (let offset = 0; offset < ids.length; offset += API_BATCH_SIZE) {
    const url = steamUrl(env, 'ISteamUser', 'GetPlayerSummaries', 2);
    url.searchParams.set('steamids', ids.slice(offset, offset + API_BATCH_SIZE).join(','));
    const response = responseObject(await steamJson(url));
    if (!Array.isArray(response.players)) throw new HttpError(502, 'Steam API returned invalid creator profiles');
    for (const player of response.players as unknown[]) {
      if (!player || typeof player !== 'object' || !('steamid' in player) || typeof player.steamid !== 'string') {
        throw new HttpError(502, 'Steam API returned invalid creator profiles');
      }
      authors.set(player.steamid, {
        id: player.steamid,
        name: 'personaname' in player && typeof player.personaname === 'string' ? player.personaname : null,
        profile_url: `https://steamcommunity.com/profiles/${player.steamid}/`,
      });
    }
  }
  return authors;
}

function publicFile(file: SteamFile): boolean {
  return file.result === 1 && file.consumer_appid === APP_ID && !file.banned
    && (file.visibility === undefined || file.visibility === 0) && (file.file_type === 0 || file.file_type === 2);
}

function normalized(file: SteamFile, authors: Map<string, Author>): CatalogItem {
  const tags = [...new Set((file.tags ?? []).map(tag => tag.tag))];
  const supportedVersions = tags.filter(tag => /^\d+\.\d+(?:\.\d+)?$/.test(tag));
  const children = [...(file.children ?? [])].sort((a,b) => (a.sortorder ?? 0) - (b.sortorder ?? 0));
  const childIds = [...new Set(children.map(child => child.publishedfileid))];
  childIds.forEach(id => {
    try { validateSteamId(id); } catch { throw new HttpError(502, 'Steam API returned an invalid child Workshop ID'); }
  });
  const previews: Preview[] = [];
  for (const preview of [...(file.previews ?? [])].sort((a,b) => (a.sortorder ?? 0) - (b.sortorder ?? 0))) {
    if (preview.preview_type === 0 && preview.url) previews.push({ type: 'image', url: preview.url });
    if (preview.preview_type === 1 && preview.youtubevideoid) previews.push({ type: 'video', url: `https://www.youtube.com/watch?v=${encodeURIComponent(preview.youtubevideoid)}` });
  }
  const creator = typeof file.creator === 'string' ? file.creator : null;
  const common = {
    source: 'steam' as const,
    title: file.title ?? '',
    author: creator ? authors.get(creator) ?? { id: creator, name: null, profile_url: `https://steamcommunity.com/profiles/${creator}/` } : { id: null, name: null, profile_url: null },
    description: file.file_description ?? file.description ?? file.short_description ?? '',
    description_format: 'bbcode' as const,
    preview_url: file.preview_url || null,
    previews,
    workshop_url: `https://steamcommunity.com/sharedfiles/filedetails/?id=${file.publishedfileid}`,
    tags,
    supported_versions: supportedVersions,
    created_at: file.time_created ?? null,
    updated_at: file.time_updated ?? null,
  };
  if (file.file_type === 2) {
    return {
      ...common,
      id: `steam:${file.publishedfileid}`,
      kind: 'collection',
      steam_id: file.publishedfileid,
      member_ids: childIds,
      member_count: file.num_children ?? childIds.length,
      member_previews: [],
    };
  }
  const votes = file.vote_data;
  const up = votes?.votes_up ?? 0;
  const down = votes?.votes_down ?? 0;
  return {
    ...common,
    id: file.publishedfileid,
    kind: 'mod',
    file_size: file.file_size === undefined ? null : String(file.file_size),
    subscriptions: file.subscriptions ?? null,
    votes: votes ? { up, down, positive_percent: up + down > 0 ? Math.round(up / (up + down) * 100) : null } : null,
    dependencies: childIds,
    package_id: file.kvtags?.find(tag => tag.key === 'packageId')?.value ?? null,
    incompatible: file.incompatible ?? false,
  };
}

async function saveItems(db: D1Database, items: CatalogItem[]): Promise<void> {
  const now = Date.now();
  for (let offset = 0; offset < items.length; offset += API_BATCH_SIZE) {
    await db.batch(items.slice(offset, offset + API_BATCH_SIZE).map(item => db.prepare(
      'INSERT INTO catalog_items (id, data, updated_at) VALUES (?, ?, ?) ON CONFLICT(id) DO UPDATE SET data = excluded.data, updated_at = excluded.updated_at',
    ).bind(item.kind === 'collection' ? item.steam_id : item.id, JSON.stringify(item), now)));
  }
  await indexMods(db, items.filter((item): item is CatalogMod => item.kind === 'mod')).catch(error =>
    console.error('Search index update failed', error instanceof Error ? error.message : error));
}

async function fetchItems(env: Env, ids: string[]): Promise<Map<string, CatalogItem>> {
  const fetched = new Map<string, CatalogItem>();
  const unavailable: string[] = [];
  for (let offset = 0; offset < ids.length; offset += API_BATCH_SIZE) {
    const chunk = ids.slice(offset, offset + API_BATCH_SIZE);
    const url = steamUrl(env, 'IPublishedFileService', 'GetDetails');
    url.searchParams.set('input_json', JSON.stringify({
      publishedfileids: chunk,
      includechildren: true,
      includetags: true,
      includekvtags: true,
      includevotes: true,
      includeadditionalpreviews: true,
      short_description: false,
    }));
    const response = responseObject(await steamJson(url));
    const details = fileDetails(response);
    const returned = new Set(details.map(file => file.publishedfileid));
    if ((response.result !== undefined && response.result !== 1) || details.length !== chunk.length
      || returned.size !== chunk.length || chunk.some(id => !returned.has(id))) {
      throw new HttpError(502, 'Steam API returned incomplete file details');
    }
    for (const file of details) {
      if (file.result === 1 && (file.consumer_appid === undefined || file.file_type === undefined)) invalidMetadata();
    }
    const files = details.filter(publicFile);
    const authors = await authorsFor(env, files);
    const items = files.map(file => normalized(file, authors));
    unavailable.push(...details.filter(file => !publicFile(file)).map(file => file.publishedfileid));
    for (const item of items) fetched.set(item.kind === 'collection' ? item.steam_id! : item.id, item);
  }
  // Do not evict anything if a later Steam batch or creator lookup fails.
  await saveItems(env.DB, [...fetched.values()]);
  for (let offset = 0; offset < unavailable.length; offset += API_BATCH_SIZE) {
    await env.DB.batch(unavailable.slice(offset, offset + API_BATCH_SIZE).map(id => env.DB.prepare(
      "DELETE FROM catalog_items WHERE id = ? AND json_extract(data, '$.source') = 'steam'",
    ).bind(id)));
  }
  return fetched;
}

/**
 * With `ctx`, expired rows are returned as they are and refreshed in the background,
 * so only IDs that were never stored wait for Steam. Without it expired rows are refetched first.
 */
export async function getSteamItems(env: Env, requestedIds: string[], ctx?: ExecutionContext): Promise<{ items: Map<string, CatalogItem>; unavailable_ids: string[] }> {
  const ids = [...new Set(requestedIds)];
  const items = new Map<string, CatalogItem>();
  const stale: string[] = [];
  const cutoff = Date.now() - DETAIL_TTL_MS;
  for (let offset = 0; offset < ids.length; offset += 90) {
    const chunk = ids.slice(offset, offset + 90);
    const rows = await env.DB.prepare(`SELECT id, data, updated_at FROM catalog_items WHERE id IN (${chunk.map(() => '?').join(',')})`)
      .bind(...chunk).all<{ id: string; data: string; updated_at: number }>();
    for (const row of rows.results) {
      if (row.updated_at > cutoff) items.set(row.id, JSON.parse(row.data) as CatalogItem);
      else if (ctx) { items.set(row.id, JSON.parse(row.data) as CatalogItem); stale.push(row.id); }
    }
  }
  if (ctx && stale.length) {
    ctx.waitUntil(fetchItems(env, stale).then(() => undefined).catch(error => console.error('Background refresh failed', error instanceof Error ? error.message : error)));
  }
  const fetched = await fetchItems(env, ids.filter(id => !items.has(id)));
  for (const [id, item] of fetched) items.set(id, item);
  return { items, unavailable_ids: ids.filter(id => !items.has(id)) };
}

const COLLAGE_SIZE = 4;
const COLLAGE_CANDIDATES = 8;

// Steam gives collections no image; its site shows a collage of member previews.
export async function attachMemberPreviews(env: Env, collections: CatalogCollection[], members?: Map<string, CatalogItem>): Promise<CatalogCollection[]> {
  const candidates = collections.map(collection => collection.preview_url ? [] : collection.member_ids.slice(0, COLLAGE_CANDIDATES));
  const ids = [...new Set(candidates.flat())];
  if (!ids.length) return collections;
  if (!members) {
    try {
      members = (await getSteamItems(env, ids)).items;
    } catch (error) {
      console.error('Collection thumbnails unavailable:', error instanceof Error ? error.message : error);
      return collections;
    }
  }
  return collections.map((collection, index) => {
    const previews: string[] = [];
    for (const id of candidates[index]!) {
      const member = members.get(id);
      if (member?.kind === 'mod' && member.preview_url) previews.push(member.preview_url);
      if (previews.length === COLLAGE_SIZE) break;
    }
    return previews.length ? { ...collection, member_previews: previews } : collection;
  });
}

export interface FilesPage<T extends CatalogItem> {
  items: T[];
  total: number;
  next_cursor: string | null;
  /** Steam update times (seconds) of every file returned, including non-public ones. */
  times_updated: number[];
}

export async function queryFiles<K extends 'mod' | 'collection'>(env: Env, kind: K, query: SteamQuery): Promise<FilesPage<K extends 'mod' ? CatalogMod : CatalogCollection>> {
  const queryTypes: Record<SteamSort, number> = { popular: 9, updated: 21, newest: 1, trending: 3, relevance: 12 };
  const requiredTags = [...new Set([query.tag, query.version].filter((tag): tag is string => tag !== null))];
  const url = steamUrl(env, 'IPublishedFileService', 'QueryFiles');
  url.searchParams.set('input_json', JSON.stringify({
    query_type: queryTypes[query.sort],
    page: 1,
    cursor: query.cursor,
    numperpage: query.limit,
    creator_appid: APP_ID,
    appid: APP_ID,
    filetype: kind === 'mod' ? 0 : 1,
    search_text: query.query,
    requiredtags: requiredTags,
    match_all_tags: true,
    days: 7,
    ids_only: false,
    return_vote_data: true,
    return_tags: true,
    return_kv_tags: true,
    return_children: true,
    return_previews: true,
    return_short_description: false,
    cache_max_age_seconds: QUERY_TTL_SECONDS,
  }));
  const response = responseObject(await steamJson(url));
  if (typeof response.total !== 'number' || !Number.isSafeInteger(response.total) || response.total < 0
    || (response.next_cursor !== undefined && typeof response.next_cursor !== 'string')) {
    throw new HttpError(502, 'Steam API returned invalid pagination');
  }
  const details = fileDetails(response);
  const files = details.filter(file => publicFile(file) && file.file_type === (kind === 'mod' ? 0 : 2));
  const authors = await authorsFor(env, files);
  const items = files.map(file => normalized(file, authors));
  await saveItems(env.DB, items);
  const cursor = typeof response.next_cursor === 'string' ? response.next_cursor : '';
  return {
    items,
    total: response.total,
    next_cursor: details.length && cursor && cursor !== query.cursor ? cursor : null,
    times_updated: details.map(file => file.time_updated ?? 0),
  } as FilesPage<K extends 'mod' ? CatalogMod : CatalogCollection>;
}

const QUERY_KEEP_MS = 7 * 24 * 60 * 60 * 1000;

async function storedItems(db: D1Database, ids: string[]): Promise<Map<string, CatalogItem>> {
  const stored = new Map<string, CatalogItem>();
  for (let offset = 0; offset < ids.length; offset += 90) {
    const chunk = ids.slice(offset, offset + 90);
    const rows = await db.prepare(`SELECT id, data FROM catalog_items WHERE id IN (${chunk.map(() => '?').join(',')})`)
      .bind(...chunk).all<{ id: string; data: string }>();
    for (const row of rows.results) stored.set(row.id, JSON.parse(row.data) as CatalogItem);
  }
  return stored;
}

/**
 * Query pages are remembered in D1 as ordered IDs. A remembered page is returned at once,
 * however old, and refreshed in the background once it is older than five minutes.
 */
export async function querySteam<K extends 'mod' | 'collection'>(env: Env, ctx: ExecutionContext, kind: K, query: SteamQuery): Promise<CatalogPage<K extends 'mod' ? CatalogMod : CatalogCollection>> {
  type Page = CatalogPage<K extends 'mod' ? CatalogMod : CatalogCollection>;
  const key = JSON.stringify([kind, query]);

  const refresh = async (): Promise<Page> => {
    const { items, total, next_cursor } = await queryFiles(env, kind, query);
    const ids = items.map(item => item.kind === 'collection' ? item.steam_id! : item.id);
    await env.DB.batch([
      env.DB.prepare(
        `INSERT INTO query_cache (key, ids, total, next_cursor, fetched_at) VALUES (?, ?, ?, ?, ?)
         ON CONFLICT(key) DO UPDATE SET ids = excluded.ids, total = excluded.total, next_cursor = excluded.next_cursor, fetched_at = excluded.fetched_at`,
      ).bind(key, JSON.stringify(ids), total, next_cursor, Date.now()),
      env.DB.prepare('DELETE FROM query_cache WHERE fetched_at < ?').bind(Date.now() - QUERY_KEEP_MS),
    ]);
    return { items, total, next_cursor } as Page;
  };

  const row = await env.DB.prepare('SELECT ids, total, next_cursor, fetched_at FROM query_cache WHERE key = ? AND fetched_at > ?')
    .bind(key, Date.now() - QUERY_KEEP_MS).first<{ ids: string; total: number; next_cursor: string | null; fetched_at: number }>();
  if (row) {
    const ids = JSON.parse(row.ids) as string[];
    const stored = await storedItems(env.DB, ids);
    if (ids.every(id => stored.has(id))) {
      if (Date.now() - row.fetched_at > QUERY_TTL_SECONDS * 1000) {
        ctx.waitUntil(refresh().catch(error => console.error('Background refresh failed', error instanceof Error ? error.message : error)));
      }
      return { items: ids.map(id => stored.get(id)!), total: row.total, next_cursor: row.next_cursor } as Page;
    }
  }
  return await refresh();
}
