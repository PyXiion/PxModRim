import { deletePick, getPick, listPicks, parsePickInput, putPick, validatePickSlug } from './picks';
import { indexStatus, searchMods } from './search';
import { attachMemberPreviews, getSteamItems, querySteam, validateSteamId } from './steam';
import type { SteamQuery, SteamSort } from './steam';
import { HttpError } from './types';
import type { CatalogCollection, CatalogItem, CatalogMod, CatalogPage, Env } from './types';

const MAX_BODY_BYTES = 64 * 1024;
const MAX_GRAPH_ITEMS = 1000;
const SORTS: SteamSort[] = ['popular', 'updated', 'newest', 'trending', 'relevance'];

interface CollectionCursor {
  key: string;
  steam: string | null;
  picked: number | null;
  steam_total: number;
}

function json(value: unknown, status = 200): Response {
  return Response.json(value, {
    status,
    headers: { 'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff' },
  });
}

function integer(value: string | null, fallback: number, min: number, max: number): number {
  if (value === null) return fallback;
  if (!/^\d+$/.test(value)) throw new HttpError(400, 'Invalid pagination number');
  const result = Number(value);
  if (!Number.isSafeInteger(result) || result < min || result > max) throw new HttpError(400, 'Pagination number is out of range');
  return result;
}

function queryOptions(url: URL): SteamQuery {
  const query = (url.searchParams.get('q') ?? '').trim();
  const tag = url.searchParams.get('tag');
  const version = url.searchParams.get('version');
  const sort = url.searchParams.get('sort') ?? (query ? 'relevance' : 'popular');
  const cursor = url.searchParams.get('cursor') ?? '*';
  if (query.length > 200 || (tag !== null && (!tag.trim() || tag.length > 100))
    || (version !== null && !/^\d+\.\d+(?:\.\d+)?$/.test(version)) || cursor.length > 8192) {
    throw new HttpError(400, 'Invalid search, tag, version or cursor');
  }
  if (!SORTS.includes(sort as SteamSort)) throw new HttpError(400, 'Unsupported sort');
  if (sort === 'relevance' && !query) throw new HttpError(400, 'Relevance sorting requires a search query');
  return { query, tag, version, sort: sort as SteamSort, limit: integer(url.searchParams.get('limit'), 24, 1, 50), cursor };
}

async function readJson(request: Request): Promise<Record<string, unknown>> {
  if (request.headers.get('Content-Type')?.split(';')[0]?.trim().toLowerCase() !== 'application/json') {
    throw new HttpError(415, 'Use application/json');
  }
  const reader = request.body?.getReader();
  if (!reader) throw new HttpError(400, 'A JSON body is required');
  const decoder = new TextDecoder();
  let size = 0;
  let text = '';
  while (true) {
    const { done, value } = await reader.read();
    if (done) break;
    size += value.byteLength;
    if (size > MAX_BODY_BYTES) { await reader.cancel(); throw new HttpError(413, 'JSON body exceeds 64 KiB'); }
    text += decoder.decode(value, { stream: true });
  }
  text += decoder.decode();
  let value: unknown;
  try { value = JSON.parse(text); } catch { throw new HttpError(400, 'Invalid JSON'); }
  if (!value || typeof value !== 'object' || Array.isArray(value)) throw new HttpError(400, 'JSON body must be an object');
  return value as Record<string, unknown>;
}

function onlyFields(body: Record<string, unknown>, fields: string[]): void {
  if (Object.keys(body).some(key => !fields.includes(key))) throw new HttpError(400, 'Unexpected request field');
}

function idsFrom(value: unknown, max: number): string[] {
  if (!Array.isArray(value) || !value.length || value.length > max) throw new HttpError(400, `Provide between 1 and ${max} Workshop IDs`);
  for (const id of value as unknown[]) {
    if (typeof id !== 'string') throw new HttpError(400, 'Workshop IDs must be strings, not JSON numbers');
    validateSteamId(id);
  }
  return [...new Set(value as string[])];
}

function requireMethod(request: Request, methods: string[]): void {
  if (!methods.includes(request.method)) throw new HttpError(405, `Allowed methods: ${methods.join(', ')}`);
}

function requireAdmin(request: Request, env: Env): void {
  const authorization = request.headers.get('Authorization');
  if (!authorization?.startsWith('Bearer ') || !authorization.slice(7)) throw new HttpError(401, 'A valid admin Bearer token is required');
  let keys: unknown;
  try { keys = JSON.parse(env.CONTROL_KEYS ?? '[]'); } catch { throw new HttpError(503, 'Admin keys are not configured correctly'); }
  if (!Array.isArray(keys) || !keys.every(key => typeof key === 'string' && key.length)) throw new HttpError(503, 'Admin keys are not configured correctly');
  if (!keys.includes(authorization.slice(7))) throw new HttpError(401, 'A valid admin Bearer token is required');
}

function encodeCursor(cursor: CollectionCursor): string {
  return btoa(String.fromCharCode(...new TextEncoder().encode(JSON.stringify(cursor))))
    .replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

function decodeCursor(value: string, key: string): CollectionCursor {
  let cursor: unknown;
  try {
    if (!/^[A-Za-z0-9_-]+$/.test(value)) throw new Error('Invalid base64');
    const base64 = value.replace(/-/g, '+').replace(/_/g, '/');
    const bytes = Uint8Array.from(atob(base64.padEnd(Math.ceil(base64.length / 4) * 4, '=')), char => char.charCodeAt(0));
    cursor = JSON.parse(new TextDecoder().decode(bytes));
  } catch { throw new HttpError(400, 'Invalid collection cursor'); }
  if (!cursor || typeof cursor !== 'object' || !('key' in cursor) || cursor.key !== key
    || !('steam' in cursor) || (cursor.steam !== null && (typeof cursor.steam !== 'string' || !cursor.steam || cursor.steam.length > 4096))
    || !('picked' in cursor) || (cursor.picked !== null && (typeof cursor.picked !== 'number' || !Number.isSafeInteger(cursor.picked) || cursor.picked < 0))
    || !('steam_total' in cursor) || typeof cursor.steam_total !== 'number' || !Number.isSafeInteger(cursor.steam_total) || cursor.steam_total < 0) {
    throw new HttpError(400, 'Collection cursor does not match this query');
  }
  return cursor as CollectionCursor;
}

async function modPage(env: Env, ctx: ExecutionContext, query: SteamQuery): Promise<CatalogPage<CatalogMod>> {
  return await searchMods(env.DB, query) ?? await querySteam(env, ctx, 'mod', query);
}

async function collectionPage(env: Env, ctx: ExecutionContext, url: URL, query: SteamQuery): Promise<CatalogPage<CatalogCollection>> {
  const source = url.searchParams.get('source') ?? 'all';
  if (!['all', 'steam', 'picked'].includes(source)) throw new HttpError(400, 'Source must be all, steam or picked');
  const key = JSON.stringify([source, query.query, query.tag, query.version, query.sort, query.limit]);
  const cursor = query.cursor === '*' ? { key, steam: source === 'picked' ? null : '*', picked: source === 'steam' ? null : 0, steam_total: 0 } : decodeCursor(query.cursor, key);
  if ((source === 'steam' && cursor.picked !== null) || (source === 'picked' && cursor.steam !== null)) {
    throw new HttpError(400, 'Collection cursor source does not match this query');
  }
  const pickLimit = cursor.steam === null ? query.limit : Math.max(1, Math.floor(query.limit / 2));
  const picked = source === 'steam' ? { items: [], total: 0, next_offset: null } : await listPicks(env.DB, {
    query: query.query, tag: query.tag, version: query.version,
    sort: query.sort === 'updated' || query.sort === 'newest' ? query.sort : 'featured',
    limit: pickLimit, offset: cursor.picked ?? 0,
  });
  const pickedItems = cursor.picked === null ? [] : picked.items;
  const steamLimit = query.limit - pickedItems.length;
  let steam: CatalogPage<CatalogCollection> = { items: [], total: cursor.steam_total, next_cursor: cursor.steam };
  if (cursor.steam !== null && steamLimit > 0) {
    steam = await querySteam(env, ctx, 'collection', { ...query, limit: steamLimit, cursor: cursor.steam });
  }
  const next: CollectionCursor = {
    key, steam: steam.next_cursor, picked: cursor.picked === null ? null : picked.next_offset, steam_total: steam.total,
  };
  return {
    items: await attachCollectionSummaries(env.DB, await attachMemberPreviews(env, [...pickedItems, ...steam.items])),
    total: (source === 'steam' ? 0 : picked.total) + steam.total,
    next_cursor: next.steam === null && next.picked === null ? null : encodeCursor(next),
  };
}

const SUMMARY_TTL_MS = 24 * 60 * 60 * 1000;
const MAX_SIZE = 2n ** 63n - 1n;

interface SummaryRow { id: string; total_size: string | null; supported_versions: string; no_common_version: number }

// Totals and versions are denormalised from the members; they expire so a member update cannot leave them wrong for long.
async function attachCollectionSummaries(db: D1Database, collections: CatalogCollection[]): Promise<CatalogCollection[]> {
  const summaries = new Map<string, SummaryRow>();
  const cutoff = Date.now() - SUMMARY_TTL_MS;
  for (let offset = 0; offset < collections.length; offset += 90) {
    const chunk = collections.slice(offset, offset + 90);
    const rows = await db.prepare(
      `SELECT id, total_size, supported_versions, no_common_version FROM collection_summaries WHERE computed_at > ? AND id IN (${chunk.map(() => '?').join(',')})`,
    ).bind(cutoff, ...chunk.map(collection => collection.id)).all<SummaryRow>();
    for (const row of rows.results) summaries.set(row.id, row);
  }
  return collections.map(collection => {
    const declared = [...new Set([...collection.supported_versions, ...collection.tags.filter(tag => /^\d+\.\d+(?:\.\d+)?$/.test(tag))])];
    const summary = summaries.get(collection.id);
    if (!summary) return { ...collection, supported_versions: declared, total_size: null, no_common_version: false };
    const versions = [...new Set([...declared, ...JSON.parse(summary.supported_versions) as string[]])];
    return { ...collection, total_size: summary.total_size, supported_versions: versions, no_common_version: declared.length === 0 && Boolean(summary.no_common_version) };
  });
}

// Stored only once every member is a known mod. Undeclared member versions cannot disprove compatibility.
async function recordCollectionSummary(db: D1Database, collection: CatalogCollection, members: CatalogItem[], complete: boolean): Promise<void> {
  const mods = members.filter((member): member is CatalogMod => member.kind === 'mod');
  if (!complete || mods.length !== members.length || mods.length !== collection.member_count) {
    await db.prepare('DELETE FROM collection_summaries WHERE id = ?').bind(collection.id).run();
    return;
  }
  let total: bigint | null = 0n;
  for (const mod of mods) {
    total = total === null || mod.file_size === null || !/^\d+$/.test(mod.file_size) ? null : total + BigInt(mod.file_size);
  }
  if (total !== null && total > MAX_SIZE) total = null;
  const declaredVersions = mods.map(mod => mod.supported_versions).filter(versions => versions.length > 0);
  const versions = declaredVersions.length
    ? declaredVersions.reduce((common, next) => common.filter(version => next.includes(version)))
    : [];
  const noCommonVersion = declaredVersions.length > 0 && versions.length === 0;
  await db.prepare(
    `INSERT INTO collection_summaries (id, total_size, supported_versions, no_common_version, computed_at) VALUES (?, ?, ?, ?, ?)
     ON CONFLICT(id) DO UPDATE SET total_size = excluded.total_size, supported_versions = excluded.supported_versions, no_common_version = excluded.no_common_version, computed_at = excluded.computed_at`,
  ).bind(collection.id, total === null ? null : total.toString(), JSON.stringify(versions), Number(noCommonVersion), Date.now()).run();
}

async function collectionDetails(env: Env, ctx: ExecutionContext, collection: CatalogCollection): Promise<Response> {
  const { items, unavailable_ids } = await getSteamItems(env, collection.member_ids, ctx);
  const members: CatalogItem[] = [];
  for (const id of collection.member_ids) {
    const item = items.get(id);
    if (item) members.push(item);
  }
  const isComplete = unavailable_ids.length === 0 && collection.member_count === collection.member_ids.length;
  await recordCollectionSummary(env.DB, collection, members, isComplete);
  const [summarised] = await attachCollectionSummaries(env.DB, [collection]);
  const [withPreviews] = await attachMemberPreviews(env, [summarised!], items);
  return json({ collection: withPreviews!, members, unavailable_ids, is_complete: isComplete });
}

async function resolve(env: Env, body: Record<string, unknown>): Promise<Response> {
  onlyFields(body, ['ids', 'collection_ids']);
  const roots = body.ids === undefined || (Array.isArray(body.ids) && body.ids.length === 0)
    ? [] : idsFrom(body.ids, 50);
  const collectionIds = body.collection_ids === undefined ? [] : body.collection_ids;
  if (!Array.isArray(collectionIds) || collectionIds.length > 10) throw new HttpError(400, 'Provide at most 10 collection IDs');
  if (!roots.length && !collectionIds.length) throw new HttpError(400, 'Provide at least one root mod or collection');
  const items: Record<string, CatalogItem> = {};
  const pending = new Set(roots);
  const pickedRoots: CatalogCollection[] = [];
  for (const value of collectionIds as unknown[]) {
    if (typeof value !== 'string') throw new HttpError(400, 'Collection IDs must be strings');
    if (value.startsWith('steam:')) { const id = value.slice(6); validateSteamId(id); pending.add(id); }
    else if (value.startsWith('picked:')) {
      const slug = value.slice(7); validatePickSlug(slug);
      const collection = await getPick(env.DB, slug);
      if (!collection) throw new HttpError(404, 'Picked collection not found');
      items[collection.id] = collection; pickedRoots.push(collection);
      collection.member_ids.forEach(id => pending.add(id));
    } else throw new HttpError(400, 'Collection IDs must start with steam: or picked:');
  }
  const visited = new Set<string>();
  const unavailable = new Set<string>();
  const incompleteCollections = new Set<string>();
  while (pending.size) {
    const batch = [...pending].slice(0, 100);
    for (const id of batch) { pending.delete(id); visited.add(id); }
    if (visited.size > MAX_GRAPH_ITEMS) throw new HttpError(413, `Dependency graph exceeds ${MAX_GRAPH_ITEMS} Workshop items`);
    const result = await getSteamItems(env, batch);
    result.unavailable_ids.forEach(id => unavailable.add(id));
    for (const item of result.items.values()) {
      items[item.id] = item;
      if (item.kind === 'collection' && item.member_count !== item.member_ids.length) incompleteCollections.add(item.id);
      const children = item.kind === 'mod' ? item.dependencies : item.member_ids;
      for (const child of children) if (!visited.has(child)) pending.add(child);
    }
    if (visited.size + pending.size > MAX_GRAPH_ITEMS) throw new HttpError(413, `Dependency graph exceeds ${MAX_GRAPH_ITEMS} Workshop items`);
  }
  for (const value of collectionIds as string[]) {
    if (value.startsWith('steam:') && items[value] === undefined && !unavailable.has(value.slice(6))) {
      throw new HttpError(400, 'A collection ID refers to an individual mod');
    }
  }
  const ordered: string[] = [];
  const orderedSeen = new Set<string>();
  function visit(id: string): void {
    const item = items[id] ?? items[`steam:${id}`];
    if (!item || orderedSeen.has(item.id)) return;
    orderedSeen.add(item.id);
    for (const child of item.kind === 'mod' ? item.dependencies : item.member_ids) visit(child);
    if (item.kind === 'mod') ordered.push(item.id);
  }
  roots.forEach(visit);
  for (const value of collectionIds as string[]) if (value.startsWith('steam:')) visit(value);
  pickedRoots.forEach(collection => collection.member_ids.forEach(visit));
  return json({ roots: [...roots, ...new Set(collectionIds as string[])], items, mod_ids: ordered, unavailable_ids: [...unavailable], incomplete_collection_ids: [...incompleteCollections], is_complete: unavailable.size === 0 && incompleteCollections.size === 0 });
}

async function route(request: Request, env: Env, ctx: ExecutionContext): Promise<Response> {
  const url = new URL(request.url);
  if (url.pathname === '/catalog/discover') {
    requireMethod(request, ['GET']);
    const query = queryOptions(url);
    if (query.cursor !== '*') throw new HttpError(400, 'Use the browsing endpoints for pagination');
    const [mods, collections] = await Promise.all([
      modPage(env, ctx, { ...query, limit: 4 }),
      collectionPage(env, ctx, url, { ...query, limit: 3 }),
    ]);
    return json({ mods, collections });
  }
  if (url.pathname === '/catalog/mods') {
    requireMethod(request, ['GET']);
    const query = queryOptions(url);
    return json(await modPage(env, ctx, query));
  }
  if (url.pathname === '/catalog/index') {
    requireMethod(request, ['GET']);
    return json(await indexStatus(env.DB));
  }
  if (url.pathname === '/catalog/collections') {
    requireMethod(request, ['GET']);
    return json(await collectionPage(env, ctx, url, queryOptions(url)));
  }
  if (url.pathname === '/catalog/mods/batch') {
    requireMethod(request, ['POST']);
    const body = await readJson(request); onlyFields(body, ['ids']);
    const ids = idsFrom(body.ids, 100);
    const result = await getSteamItems(env, ids);
    const items: CatalogMod[] = [];
    const unavailable = new Set(result.unavailable_ids);
    for (const id of ids) {
      const item = result.items.get(id);
      if (item?.kind === 'mod') items.push(item); else unavailable.add(id);
    }
    return json({ items, unavailable_ids: [...unavailable] });
  }
  if (url.pathname === '/catalog/resolve') {
    requireMethod(request, ['POST']);
    return await resolve(env, await readJson(request));
  }
  const modMatch = /^\/catalog\/mods\/([^/]+)$/.exec(url.pathname);
  if (modMatch) {
    requireMethod(request, ['GET']);
    const id = modMatch[1]!; validateSteamId(id);
    const result = await getSteamItems(env, [id], ctx);
    const item = result.items.get(id);
    if (item?.kind !== 'mod') throw new HttpError(404, 'Public RimWorld mod not found');
    return json(item);
  }
  const collectionMatch = /^\/catalog\/collections\/(steam|picked)\/([^/]+)$/.exec(url.pathname);
  if (collectionMatch) {
    const source = collectionMatch[1]!;
    const id = collectionMatch[2]!;
    if (source === 'steam') {
      requireMethod(request, ['GET']); validateSteamId(id);
      const result = await getSteamItems(env, [id], ctx);
      const item = result.items.get(id);
      if (item?.kind !== 'collection') throw new HttpError(404, 'Public RimWorld collection not found');
      return await collectionDetails(env, ctx, item);
    }
    validatePickSlug(id);
    requireMethod(request, ['GET', 'PUT', 'DELETE']);
    if (request.method === 'PUT') {
      requireAdmin(request, env);
      const input = parsePickInput(await readJson(request));
      const members = await getSteamItems(env, input.member_ids);
      if (members.unavailable_ids.length || [...members.items.values()].some(item => item.kind !== 'mod')) {
        throw new HttpError(400, 'Picked collections may only contain available RimWorld mods');
      }
      return json(await putPick(env.DB, id, input));
    }
    if (request.method === 'DELETE') {
      requireAdmin(request, env);
      if (!await deletePick(env.DB, id)) throw new HttpError(404, 'Picked collection not found');
      return new Response(null, { status: 204, headers: { 'Cache-Control': 'no-store' } });
    }
    const collection = await getPick(env.DB, id);
    if (!collection) throw new HttpError(404, 'Picked collection not found');
    return await collectionDetails(env, ctx, collection);
  }
  throw new HttpError(404, 'Catalog endpoint not found');
}

export async function handleCatalogRequest(request: Request, env: Env, ctx: ExecutionContext): Promise<Response> {
  try {
    return await route(request, env, ctx);
  } catch (error) {
    if (error instanceof HttpError) return json({ error: { message: error.message } }, error.status);
    console.error('[CATALOG] Internal request failure', error instanceof Error ? `${error.name}: ${error.message}` : String(error));
    return json({ error: { message: 'Internal catalog error' } }, 500);
  }
}
