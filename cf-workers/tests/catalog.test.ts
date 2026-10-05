import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { resolve as resolvePath } from 'node:path';
import { after, before, test } from 'node:test';
import { Miniflare } from 'miniflare';
import type { CatalogCollection, CatalogItem, CatalogMod, CatalogPage, SteamFile } from '../types';

const creator = '76561198000000001';
function mod(id: string, title: string, children: string[] = [], versions = ['1.6']): SteamFile {
  return {
    publishedfileid: id, result: 1, consumer_appid: 294100, creator_appid: 294100,
    creator, title, file_type: 0, file_description: '[b]A real description format[/b]',
    file_size: '4096', time_created: 1700000000, time_updated: 1720000000,
    preview_url: 'https://images.example.test/preview.png',
    tags: [...versions, 'Quality of life'].map(tag => ({ tag })),
    children: children.map((publishedfileid, sortorder) => ({ publishedfileid, sortorder })),
    vote_data: { votes_up: 90, votes_down: 10 },
  };
}
const files: Record<string, SteamFile> = {
  '10': mod('10', 'Allow Tool', ['12']),
  '11': { ...mod('11', 'Harmony'), kvtags: [{ key: 'packageId', value: 'brrainz.harmony' }] },
  '12': mod('12', 'HugsLib', ['11']),
  '13': mod('13', 'RimHUD', ['11']),
  '14': mod('14', 'Camera+', ['11'], ['1.5']),
  '15': { ...mod('15', 'Another game'), consumer_appid: 730 },
  '16': { publishedfileid: '16', result: 15 },
  '17': { ...mod('17', 'Artwork'), file_type: 3 },
  '20': mod('20', 'Cycle A', ['21']),
  '21': mod('21', 'Cycle B', ['20']),
  '100': { ...mod('100', 'Steam Collection'), file_type: 2, children: [{ publishedfileid: '101', sortorder: 1 }, { publishedfileid: '10', sortorder: 0 }], num_children: 2 },
  '101': { ...mod('101', 'Nested Collection'), file_type: 2, children: [{ publishedfileid: '13', sortorder: 0 }], num_children: 1 },
  '102': { ...mod('102', 'no-thumb collection'), file_type: 2, preview_url: '', children: [{ publishedfileid: '13', sortorder: 0 }, { publishedfileid: '16', sortorder: 1 }, { publishedfileid: '101', sortorder: 2 }, { publishedfileid: '11', sortorder: 3 }], num_children: 4 },
  '18446744073709551615': { ...mod('18446744073709551615', 'Maximum ID'), file_size: '18446744073709551615', vote_data: { votes_up: 0, votes_down: 0 } },
};

let flakyCalls = 0;

async function upstream(request: Request): Promise<Response> {
  const url = new URL(request.url);
  if (url.hostname !== 'api.steampowered.com') return new Response('Unexpected upstream', { status: 502 });
  if (url.pathname.includes('GetPlayerSummaries')) {
    const players = [{ steamid: creator, personaname: 'Fixture Author' }];
    return Response.json({ response: { players: url.pathname.endsWith('/v1/') ? { player: players } : players } });
  }
  const input = JSON.parse(url.searchParams.get('input_json') ?? '{}') as {
    publishedfileids?: string[]; filetype?: number; search_text?: string; cursor?: string; numperpage?: number; requiredtags?: string[];
  };
  if (url.pathname.includes('GetDetails')) {
    if (input.publishedfileids?.includes('999')) {
      return Response.json({ response: { publishedfiledetails: [{ ...mod('999', 'Malformed upstream'), tags: 'not-an-array' }] } });
    }
    return Response.json({ response: { publishedfiledetails: (input.publishedfileids ?? []).flatMap(id => files[id] ? [files[id]] : []) } });
  }
  if (url.pathname.includes('QueryFiles')) {
    if (input.search_text === 'upstream-rate-limit') return new Response('Rate limited', { status: 429 });
    if (input.search_text === 'upstream-flaky' && flakyCalls++ === 0) return new Response('Hiccup', { status: 500 });
    const candidateIds = input.filetype === 1 ? (input.search_text === 'no-thumb' ? ['102'] : ['100', '101']) : ['10', '11', '12', '13', '14'];
    const matching = candidateIds.map(id => files[id]!).filter(file =>
      (file.title ?? '').toLowerCase().includes((input.search_text ?? '').toLowerCase())
      && (input.requiredtags ?? []).every(tag => file.tags?.some(value => value.tag === tag)));
    const offset = input.cursor === '*' ? 0 : Number(input.cursor);
    const details = matching.slice(offset, offset + (input.numperpage ?? 24));
    const next = offset + details.length;
    return Response.json({ response: { total: matching.length, publishedfiledetails: details, next_cursor: next < matching.length ? String(next) : '' } });
  }
  return new Response('Unexpected Steam method', { status: 502 });
}

let worker: Miniflare;
let db: D1Database;

function statements(sql: string): string[] {
  const result: string[] = [];
  let current = '';
  for (const line of sql.split('\n')) {
    current += `${line}\n`;
    const inTrigger = /CREATE TRIGGER/i.test(current) && !/^END;$/m.test(current);
    if (line.trimEnd().endsWith(';') && !inTrigger) { result.push(current.trim()); current = ''; }
  }
  return result.filter(statement => statement && statement !== ';');
}

async function migrate(database: D1Database): Promise<void> {
  for (const file of ['0001_dependencies.sql', '0002_catalog.sql', '0003_search.sql']) {
    const sql = await readFile(resolvePath('migrations', file), 'utf8');
    await database.batch(statements(sql).map(statement => database.prepare(statement)));
  }
}

before(async () => {
  worker = new Miniflare({
    workers: [{
      config: {
        name: 'catalog-tests', compatibilityDate: '2026-10-05',
        manifest: { mainModule: 'worker.js', modules: { 'worker.js': { type: 'esm', contents: await readFile(resolvePath('dist/worker.js'), 'utf8') } } },
        env: {
          DB: { type: 'd1', id: 'catalog-tests' },
          STEAM_API_KEY: { type: 'text', value: 'fixture-only-key' },
          CONTROL_KEYS: { type: 'text', value: '["test-admin"]' },
        },
      },
      dev: { outboundService: { type: 'fetcher', handler: upstream } },
    }],
  });
  db = await worker.getD1Database('DB');
  await migrate(db);
});
after(async () => { await worker?.dispose(); });

async function request(path: string, method = 'GET', body?: unknown, token?: string) {
  return await worker.dispatchFetch(`https://catalog.test${path}`, {
    method,
    headers: { ...(body === undefined ? {} : { 'Content-Type': 'application/json' }), ...(token ? { Authorization: `Bearer ${token}` } : {}) },
    ...(body === undefined ? {} : { body: JSON.stringify(body) }),
  });
}

async function createPick(slug: string, memberIds = ['10', '13'], title = 'Starter colony', extra: Record<string, unknown> = {}) {
  const response = await request(`/catalog/collections/picked/${slug}`, 'PUT', {
    title, author: 'PxModRim', description: 'A focused collection.', member_ids: memberIds,
    tags: ['Quality of life'], supported_versions: ['1.6'], ...extra,
  }, 'test-admin');
  assert.equal(response.status, 200, await response.text());
}

test('mod details expose real tag declarations, creators, votes and required IDs', async () => {
  const response = await request('/catalog/mods/10');
  assert.equal(response.status, 200);
  const detail = await response.json() as CatalogMod;
  assert.equal(detail.author.name, 'Fixture Author');
  assert.equal(detail.description_format, 'bbcode');
  assert.equal(detail.description, '[b]A real description format[/b]');
  assert.deepEqual(detail.supported_versions, ['1.6']);
  assert.deepEqual(detail.dependencies, ['12']);
  assert.deepEqual(detail.votes, { up: 90, down: 10, positive_percent: 90 });
});

test('uint64 IDs and sizes retain their exact string value; unrated is not zero percent', async () => {
  const response = await request('/catalog/mods/18446744073709551615');
  assert.equal(response.status, 200);
  const detail = await response.json() as CatalogMod;
  assert.equal(detail.id, '18446744073709551615');
  assert.equal(detail.file_size, '18446744073709551615');
  assert.equal(detail.votes?.positive_percent, null);
});

test('private, foreign-game and non-mod files are not advertised as downloadable mods', async () => {
  for (const id of ['15', '16', '17', '100']) assert.equal((await request(`/catalog/mods/${id}`)).status, 404);
  const batch = await request('/catalog/mods/batch', 'POST', { ids: ['10', '15', '16', '17', '100'] });
  const value = await batch.json() as { items: CatalogMod[]; unavailable_ids: string[] };
  assert.deepEqual(value.items.map(item => item.id), ['10']);
  assert.deepEqual(value.unavailable_ids, ['15', '16', '17', '100']);
});

test('Steam browsing filters version tags and paginates without repeating the first page', async () => {
  const first = await request('/catalog/mods?version=1.6&limit=2');
  const value = await first.json() as CatalogPage<CatalogMod>;
  assert.deepEqual(value.items.map(item => item.id), ['10', '11']);
  assert.equal(value.total, 4);
  const second = await request(`/catalog/mods?version=1.6&limit=2&cursor=${encodeURIComponent(value.next_cursor!)}`);
  const next = await second.json() as CatalogPage<CatalogMod>;
  assert.deepEqual(next.items.map(item => item.id), ['12', '13']);
  assert.equal(next.next_cursor, null);
});

test('Steam collection details retain ordered members, including nested collections', async () => {
  const response = await request('/catalog/collections/steam/100');
  const value = await response.json() as { collection: CatalogCollection; members: CatalogItem[] };
  assert.equal(response.status, 200);
  assert.equal(value.collection.id, 'steam:100');
  assert.deepEqual(value.collection.member_ids, ['10', '101']);
  assert.deepEqual(value.members.map(item => item.id), ['10', 'steam:101']);
  assert.equal(value.members[1]?.kind, 'collection');
});

test('picked collection mutations require authentication and validate members before persistence', async () => {
  const body = { title: 'Protected', author: 'PxModRim', description: '', member_ids: ['10'] };
  assert.equal((await request('/catalog/collections/picked/protected', 'PUT', body)).status, 401);
  assert.equal((await request('/catalog/collections/picked/protected', 'PUT', body, 'wrong')).status, 401);
  assert.equal((await request('/catalog/collections/picked/protected')).status, 404);
  for (const invalid of [['15'], ['100'], [10], ['18446744073709551616']]) {
    assert.equal((await request('/catalog/collections/picked/protected', 'PUT', { ...body, member_ids: invalid }, 'test-admin')).status, 400);
  }
  await createPick('protected', ['10', '10', '13']);
  const value = await (await request('/catalog/collections/picked/protected')).json() as { collection: CatalogCollection };
  assert.deepEqual(value.collection.member_ids, ['10', '13']);
  assert.equal(value.collection.source, 'picked');
  assert.equal(value.collection.workshop_url, null);
  assert.equal((await request('/catalog/collections/picked/protected', 'DELETE')).status, 401);
  assert.equal((await request('/catalog/collections/picked/protected', 'DELETE', undefined, 'test-admin')).status, 204);
  assert.equal((await request('/catalog/collections/picked/protected')).status, 404);
});

test('picked search treats wildcard text literally, combines tag/version filters, and preserves creation time', async () => {
  await createPick('literal', ['10'], '100% colony', { featured_rank: 1 });
  const before = await db.prepare('SELECT created_at FROM picked_collections WHERE slug = ?').bind('literal').first<{ created_at: number }>();
  await createPick('literal', ['13'], '100% colony updated', { featured_rank: 1 });
  const after = await db.prepare('SELECT created_at FROM picked_collections WHERE slug = ?').bind('literal').first<{ created_at: number }>();
  assert.equal(after?.created_at, before?.created_at);
  const response = await request('/catalog/collections?source=picked&q=%25&tag=Quality%20of%20life&version=1.6');
  const value = await response.json() as CatalogPage<CatalogCollection>;
  assert.deepEqual(value.items.map(item => item.id), ['picked:literal']);
  assert.deepEqual(value.items[0]?.member_ids, ['13']);
  assert.equal((await (await request('/catalog/collections?source=picked&q=%25&version=1.5')).json() as CatalogPage<CatalogCollection>).total, 0);
});

test('mixed collection pagination retains both sources without duplicates or query-mismatched cursors', async () => {
  await createPick('starter', ['10'], 'Starter colony', { featured_rank: 10 });
  await createPick('tools', ['13'], 'Useful tools', { featured_rank: 20 });
  const first = await request('/catalog/collections?limit=3&version=1.6');
  const firstPage = await first.json() as CatalogPage<CatalogCollection>;
  const all = [...firstPage.items];
  let cursor = firstPage.next_cursor;
  while (cursor) {
    const page = await (await request(`/catalog/collections?limit=3&version=1.6&cursor=${encodeURIComponent(cursor)}`)).json() as CatalogPage<CatalogCollection>;
    all.push(...page.items); cursor = page.next_cursor;
  }
  assert.deepEqual(all.map(item => item.id).sort(), ['picked:literal', 'picked:starter', 'picked:tools', 'steam:100', 'steam:101'].sort());
  assert.equal(firstPage.total, 5);
  assert.equal((await request(`/catalog/collections?limit=3&version=1.5&cursor=${encodeURIComponent(firstPage.next_cursor!)}`)).status, 400);
});

test('Unicode queries survive opaque picked collection pagination', async () => {
  await createPick('unicode-a', ['10'], 'Колония A');
  await createPick('unicode-b', ['13'], 'Колония B');
  const path = '/catalog/collections?source=picked&q=%D0%9A%D0%BE%D0%BB%D0%BE%D0%BD%D0%B8%D1%8F&limit=1';
  const first = await (await request(path)).json() as CatalogPage<CatalogCollection>;
  const next = await (await request(`${path}&cursor=${encodeURIComponent(first.next_cursor!)}`)).json() as CatalogPage<CatalogCollection>;
  assert.deepEqual([first.items[0]?.id, next.items[0]?.id], ['picked:unicode-a', 'picked:unicode-b']);
  assert.equal(next.next_cursor, null);
});

test('download resolution expands nested collections, includes transitive dependencies once, and orders libraries first', async () => {
  const response = await request('/catalog/resolve', 'POST', { ids: ['13'], collection_ids: ['steam:100', 'picked:starter'] });
  const value = await response.json() as { mod_ids: string[]; items: Record<string, CatalogItem>; is_complete: boolean };
  assert.equal(response.status, 200);
  assert.deepEqual(value.mod_ids, ['11', '13', '12', '10']);
  assert.equal(value.items['steam:101']?.kind, 'collection');
  assert.equal(value.is_complete, true);
});

test('a collection-only download accepts an empty explicit mod selection but not an entirely empty request', async () => {
  const response = await request('/catalog/resolve', 'POST', { ids: [], collection_ids: ['steam:100'] });
  assert.equal(response.status, 200);
  const value = await response.json() as { mod_ids: string[]; is_complete: boolean };
  assert.deepEqual(value.mod_ids, ['11', '12', '10', '13']);
  assert.equal(value.is_complete, true);
  assert.equal((await request('/catalog/resolve', 'POST', { ids: [], collection_ids: [] })).status, 400);
});

test('download resolution terminates cycles and reports missing items rather than claiming completeness', async () => {
  const cycle = await (await request('/catalog/resolve', 'POST', { ids: ['20'] })).json() as { mod_ids: string[]; is_complete: boolean };
  assert.deepEqual(cycle.mod_ids, ['21', '20']);
  assert.equal(cycle.is_complete, true);
  const missing = await (await request('/catalog/resolve', 'POST', { ids: ['10', '16'] })).json() as { mod_ids: string[]; unavailable_ids: string[]; is_complete: boolean };
  assert.deepEqual(missing.mod_ids, ['11', '12', '10']);
  assert.deepEqual(missing.unavailable_ids, ['16']);
  assert.equal(missing.is_complete, false);
});

test('expired metadata is refreshed instead of hiding a newer Workshop update', async () => {
  const stale = { ...await (await request('/catalog/mods/13')).json() as CatalogMod, updated_at: 1 };
  await db.prepare('UPDATE catalog_items SET data = ?, updated_at = 0 WHERE id = ?').bind(JSON.stringify(stale), '13').run();
  const fresh = await (await request('/catalog/mods/batch', 'POST', { ids: ['13'] })).json() as { items: CatalogMod[] };
  assert.equal(fresh.items[0]?.updated_at, 1720000000);
});

test('invalid input and malformed/rate-limited upstreams have explicit non-success responses without leaking secrets', async () => {
  for (const path of ['/catalog/mods?limit=0', '/catalog/mods?sort=name', '/catalog/mods?version=banana', '/catalog/mods/0']) {
    assert.equal((await request(path)).status, 400);
  }
  assert.equal((await request('/catalog/mods/batch', 'POST', { ids: [10] })).status, 400);
  assert.equal((await request('/catalog/resolve', 'POST', { collection_ids: ['steam:10'] })).status, 400);
  assert.equal((await request('/catalog/mods/999')).status, 502);
  const limited = await request('/catalog/mods?q=upstream-rate-limit');
  assert.equal(limited.status, 503);
  assert.equal((await limited.text()).includes('fixture-only-key'), false);
});

test('a transient Steam failure is retried instead of failing the browse request', async () => {
  const response = await request('/catalog/mods?q=upstream-flaky');
  assert.equal(response.status, 200);
  assert.equal(flakyCalls, 2);
});

test('idempotent migrations and the existing /deps response preserve populated dependency data', async () => {
  const now = Date.now();
  await db.batch([
    db.prepare('INSERT INTO items (id,title,is_collection,status,updated_at,package_id,deps) VALUES (?,?,?,?,?,?,?)').bind('777', 'Existing mod', 0, 'OK', now, 'existing.mod', '778'),
    db.prepare('INSERT INTO items (id,title,is_collection,status,updated_at,package_id,deps) VALUES (?,?,?,?,?,?,?)').bind('778', 'Existing dependency', 0, 'OK', now, 'existing.dependency', ''),
  ]);
  await migrate(db);
  const response = await request('/deps?id=777');
  const value = await response.json() as { rootId: string; totalItemsLoaded: number; isComplete: boolean; items: Record<string, { deps: string[]; package_id: string }> };
  assert.equal(response.status, 200);
  assert.equal(value.rootId, '777');
  assert.equal(value.totalItemsLoaded, 2);
  assert.equal(value.isComplete, true);
  assert.deepEqual(value.items['777']?.deps, ['778']);
  assert.equal(value.items['777']?.package_id, 'existing.mod');
});

test('an unconfigured Steam key does not turn a missing catalog into a successful empty page', async () => {
  const noKey = new Miniflare({ workers: [{
    config: {
      name: 'no-key', compatibilityDate: '2026-10-05',
      manifest: { mainModule: 'worker.js', modules: { 'worker.js': { type: 'esm', contents: await readFile(resolvePath('dist/worker.js'), 'utf8') } } },
      env: { DB: { type: 'd1', id: 'no-key' } },
    },
  }] });
  try {
    const database = await noKey.getD1Database('DB');
    await migrate(database);
    const response = await noKey.dispatchFetch('https://no-key.test/catalog/mods');
    assert.equal(response.status, 503);
    const picks = await noKey.dispatchFetch('https://no-key.test/catalog/collections?source=picked');
    assert.equal(picks.status, 200);
    assert.deepEqual((await picks.json() as CatalogPage<CatalogCollection>).items, []);
  } finally { await noKey.dispose(); }
});

test('newest picks use creation time while recently updated picks use modification time', async () => {
  const slugs = ['ordering-first', 'ordering-second'];
  try {
    for (const slug of slugs) {
      assert.equal((await request(`/catalog/collections/picked/${slug}`, 'PUT', {
        title: slug, author: 'Curator', description: '', tags: ['ordering'], member_ids: ['10'],
      }, 'test-admin')).status, 200);
    }
    await db.prepare("UPDATE picked_collections SET created_at = CASE slug WHEN 'ordering-first' THEN 100 ELSE 200 END, updated_at = CASE slug WHEN 'ordering-first' THEN 300 ELSE 200 END WHERE slug IN ('ordering-first', 'ordering-second')").run();
    const newest = await (await request('/catalog/collections?source=picked&tag=ordering&sort=newest')).json() as CatalogPage<CatalogCollection>;
    assert.deepEqual(newest.items.map(item => item.id), ['picked:ordering-second', 'picked:ordering-first']);
    const updated = await (await request('/catalog/collections?source=picked&tag=ordering&sort=updated')).json() as CatalogPage<CatalogCollection>;
    assert.deepEqual(updated.items.map(item => item.id), ['picked:ordering-first', 'picked:ordering-second']);
  } finally {
    for (const slug of slugs) await request(`/catalog/collections/picked/${slug}`, 'DELETE', undefined, 'test-admin');
  }
});

test('collections without a Steam image get a collage from their member mods, skipping unavailable and nested members', async () => {
  const response = await request('/catalog/collections?source=steam&q=no-thumb');
  const page = await response.json() as CatalogPage<CatalogCollection>;
  assert.equal(response.status, 200);
  assert.equal(page.items[0]?.preview_url, null);
  assert.deepEqual(page.items[0]?.member_previews, Array(2).fill('https://images.example.test/preview.png'));
});

test('text search is answered by the local index only after a full crawl cycle, and filters by tag and version', async () => {
  assert.equal((await (await request('/catalog/index')).json() as { ready: boolean }).ready, false);
  const before = await request('/catalog/mods?q=upstream-rate-limit');
  assert.equal(before.status, 503);

  await (await worker.getWorker()).scheduled({ cron: '* * * * *' });
  const status = await (await request('/catalog/index')).json() as { ready: boolean; indexed: number; cycle_in_progress: boolean };
  assert.equal(status.ready, true);
  assert.equal(status.cycle_in_progress, false);
  assert.ok(status.indexed >= 5);

  const local = await request('/catalog/mods?q=upstream-rate-limit');
  assert.equal(local.status, 200);
  assert.deepEqual((await local.json() as CatalogPage<CatalogMod>).items, []);

  const prefix = await (await request('/catalog/mods?q=harm')).json() as CatalogPage<CatalogMod>;
  assert.deepEqual(prefix.items.map(item => item.id), ['11']);
  assert.equal(prefix.items[0]?.previews.length, 0);

  const old = await (await request('/catalog/mods?q=camera&version=1.5')).json() as CatalogPage<CatalogMod>;
  assert.deepEqual(old.items.map(item => item.id), ['14']);
  const none = await (await request('/catalog/mods?q=camera&version=1.6')).json() as CatalogPage<CatalogMod>;
  assert.deepEqual(none.items, []);

  const first = await (await request('/catalog/mods?q=h&limit=1&sort=updated')).json() as CatalogPage<CatalogMod>;
  assert.equal(first.items.length, 1);
  assert.ok(first.total >= 2 && first.next_cursor?.startsWith('ix:'));
  const second = await (await request(`/catalog/mods?q=h&limit=1&sort=updated&cursor=${first.next_cursor}`)).json() as CatalogPage<CatalogMod>;
  assert.notEqual(second.items[0]?.id, first.items[0]?.id);

  const hostile = await request('/catalog/mods?q=%22%20OR%20*%20NEAR(');
  assert.equal(hostile.status, 200);
});

test('expired detail reads answer from storage immediately and refresh in the background; update checks still wait', async () => {
  const stale = { ...await (await request('/catalog/mods/13')).json() as CatalogMod, title: 'Old title' };
  const expire = () => db.prepare('UPDATE catalog_items SET data = ?, updated_at = 0 WHERE id = ?').bind(JSON.stringify(stale), '13').run();

  await expire();
  const served = await (await request('/catalog/mods/13')).json() as CatalogMod;
  assert.equal(served.title, 'Old title');
  let refreshed = '';
  for (let attempt = 0; attempt < 50 && refreshed !== 'RimHUD'; attempt++) {
    await new Promise(done => setTimeout(done, 20));
    refreshed = (await db.prepare('SELECT json_extract(data, "$.title") AS title FROM catalog_items WHERE id = ?').bind('13').first<{ title: string }>())?.title ?? '';
  }
  assert.equal(refreshed, 'RimHUD');

  await expire();
  const checked = await (await request('/catalog/mods/batch', 'POST', { ids: ['13'] })).json() as { items: CatalogMod[] };
  assert.equal(checked.items[0]?.title, 'RimHUD');
});
