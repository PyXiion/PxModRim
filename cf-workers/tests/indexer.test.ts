import assert from 'node:assert/strict';
import { readFile, readdir } from 'node:fs/promises';
import { resolve as resolvePath } from 'node:path';
import { after, before, beforeEach, test, type TestContext } from 'node:test';
import { Miniflare } from 'miniflare';
import { indexTick } from '../indexer';
import { queryFiles, querySteam, refreshSteamItems, type SteamQuery } from '../steam';
import type { Env, SteamFile } from '../types';

const NOW = 1_800_000_000_000;
const SINCE = NOW - 2 * 60 * 60 * 1000;
const creator = '76561198000000001';
const query: SteamQuery = { query: '', tag: null, version: null, sort: 'updated', limit: 100, cursor: '*' };

function mod(id: string, updated = SINCE / 1000 - 1): SteamFile {
  return {
    publishedfileid: id, result: 1, consumer_appid: 294100, creator_appid: 294100,
    file_type: 0, creator, title: `Fixture ${id}`, file_description: 'Fixture description',
    time_created: 1_700_000_000, time_updated: updated, subscriptions: 100, tags: [{ tag: '1.6' }],
  };
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

before(async () => {
  worker = new Miniflare({
    workers: [{
      config: {
        name: 'indexer-tests', compatibilityDate: '2026-10-05',
        manifest: { mainModule: 'worker.js', modules: { 'worker.js': { type: 'esm', contents: 'export default { fetch() { return new Response("D1 test host"); } };' } } },
        env: { DB: { type: 'd1', id: 'indexer-tests' } },
      },
    }],
  });
  db = await worker.getD1Database('DB');
  for (const file of (await readdir(resolvePath('migrations'))).filter(file => file.endsWith('.sql')).sort()) {
    const sql = await readFile(resolvePath('migrations', file), 'utf8');
    await db.batch(statements(sql).map(statement => db.prepare(statement)));
  }
});
after(async () => { await worker?.dispose(); });
beforeEach(async () => {
  await db.batch([
    db.prepare('DELETE FROM catalog_items'),
    db.prepare('DELETE FROM catalog_index'),
    db.prepare('DELETE FROM query_cache'),
    db.prepare("UPDATE index_state SET cursor = '*', cycle_started_at = 0, since = 0, last_started_at = 0, full_at = 0, indexed = 0, updated_at = 0 WHERE id = 1"),
  ]);
});

function upstream(t: TestContext, pages: Record<string, { files: SteamFile[]; next?: string }>, details: Record<string, SteamFile> = {}) {
  const queries: string[] = [];
  const detailRequests: string[][] = [];
  const logs: { category: string; rows_written: number }[] = [];
  t.mock.method(Date, 'now', () => NOW);
  t.mock.method(console, 'log', (label: string, payload: string) => {
    if (label === '[D1_WRITES]') logs.push(JSON.parse(payload));
  });
  t.mock.method(globalThis, 'fetch', async (url: URL) => {
    assert.equal(url.hostname, 'api.steampowered.com');
    if (url.pathname.includes('GetPlayerSummaries')) {
      return Response.json({ response: { players: [{ steamid: creator, personaname: 'Fixture Author' }] } });
    }
    const input = JSON.parse(url.searchParams.get('input_json')!) as { cursor: string; publishedfileids: string[] };
    if (url.pathname.includes('QueryFiles')) {
      queries.push(input.cursor);
      const page = pages[input.cursor];
      assert.ok(page, `Unexpected query cursor ${input.cursor}`);
      return Response.json({ response: { total: page.files.length, publishedfiledetails: page.files, next_cursor: page.next ?? '' } });
    }
    assert.ok(url.pathname.includes('GetDetails'), `Unexpected Steam method ${url.pathname}`);
    detailRequests.push(input.publishedfileids);
    return Response.json({ response: { publishedfiledetails: input.publishedfileids.map(id => details[id] ?? { publishedfileid: id, result: 9 }) } });
  });
  return { env: { DB: db, STEAM_API_KEY: 'fixture-only-key' } satisfies Env, queries, detailRequests, logs };
}

async function incrementalState(): Promise<void> {
  await db.prepare('UPDATE index_state SET cycle_started_at = ?, since = ?, last_started_at = ?, full_at = ? WHERE id = 1')
    .bind(NOW, SINCE, SINCE + 60 * 60 * 1000, NOW - 60 * 60 * 1000).run();
}

async function storedRows() {
  const catalog = await db.prepare('SELECT * FROM catalog_items ORDER BY id').all();
  const index = await db.prepare('SELECT * FROM catalog_index ORDER BY id').all();
  return { catalog: catalog.results, index: index.results };
}

const ctx = {
  waitUntil() { assert.fail('Fresh query pages should not schedule a background refresh'); },
} as unknown as ExecutionContext;

test('queryFiles fetches and normalizes without persisting catalog or query rows', async t => {
  const { env } = upstream(t, { '*': { files: [mod('10'), { ...mod('11'), visibility: 2 }] } });
  const page = await queryFiles(env, 'mod', query);
  assert.deepEqual(page.items.map(item => item.id), ['10']);
  assert.deepEqual(page.times_updated, [SINCE / 1000 - 1, SINCE / 1000 - 1]);
  assert.deepEqual(await storedRows(), { catalog: [], index: [] });
  assert.equal((await db.prepare('SELECT count(*) AS n FROM query_cache').first<{ n: number }>())?.n, 0);
});

test('an all-old incremental boundary stops pagination without any catalog or index item writes', async t => {
  const existing = mod('10');
  const { env, queries, logs } = upstream(t, {
    '*': { files: [{ ...existing, title: 'Must not replace cached title' }, mod('11'), { ...mod('12'), time_updated: undefined }], next: 'must-not-fetch' },
  }, { '10': existing });
  await refreshSteamItems(env, ['10']);
  await db.prepare('UPDATE catalog_items SET updated_at = 0').run();
  const before = await storedRows();
  logs.length = 0;
  await incrementalState();

  await indexTick(env);

  assert.deepEqual(await storedRows(), before);
  assert.deepEqual(queries, ['*']);
  assert.deepEqual(logs.filter(log => log.category === 'catalog_cache' || log.category === 'search_index'), []);
  const state = await db.prepare('SELECT cycle_started_at, cursor, full_at FROM index_state WHERE id = 1')
    .first<{ cycle_started_at: number; cursor: string; full_at: number }>();
  assert.equal(state?.cycle_started_at, 0);
  assert.equal(state?.cursor, '*');
  assert.equal(state?.full_at, NOW - 60 * 60 * 1000);
});

test('mixed incremental pages persist new and exactly-at-cutoff items, skipping strictly older ones', async t => {
  const existing = mod('12');
  const { env, queries } = upstream(t, {
    '*': {
      files: [mod('10', SINCE / 1000 + 1), mod('11', SINCE / 1000), { ...existing, title: 'Do not update old item' }, mod('13'), { ...mod('14', SINCE / 1000 + 1), visibility: 2 }],
      next: 'boundary',
    },
    boundary: { files: [mod('15')], next: 'must-not-fetch' },
  }, { '12': existing });
  await refreshSteamItems(env, ['12']);
  await db.prepare('UPDATE catalog_items SET updated_at = 0').run();
  const before = await storedRows();
  await incrementalState();

  await indexTick(env);

  const after = await storedRows();
  assert.deepEqual(after.catalog.map(row => row.id), ['10', '11', '12']);
  assert.deepEqual(after.index.map(row => row.id), ['10', '11', '12']);
  assert.deepEqual(after.catalog.find(row => row.id === '12'), before.catalog[0]);
  assert.deepEqual(after.index.find(row => row.id === '12'), before.index[0]);
  assert.deepEqual(queries, ['*', 'boundary']);
  assert.equal((await db.prepare('SELECT cycle_started_at FROM index_state WHERE id = 1').first<{ cycle_started_at: number }>())?.cycle_started_at, 0);
});

test('weekly full refresh updates unchanged listing verification markers and verifies omitted/private mods', async t => {
  const listed = mod('10');
  const omitted = mod('11');
  const details: Record<string, SteamFile> = { '10': listed, '11': omitted, '12': mod('12'), '13': mod('13') };
  const { env, detailRequests } = upstream(t, { '*': { files: [listed] } }, details);
  await refreshSteamItems(env, ['10', '11', '12', '13']);
  await db.batch([
    db.prepare('UPDATE catalog_items SET updated_at = 0'),
    db.prepare('UPDATE catalog_index SET indexed_at = 0'),
    db.prepare('UPDATE index_state SET full_at = 1 WHERE id = 1'),
  ]);
  detailRequests.length = 0;
  details['12'] = { ...mod('12'), visibility: 2 };
  details['13'] = { publishedfileid: '13', result: 9 };

  await indexTick(env);

  assert.deepEqual(detailRequests, [['11', '12', '13']]);
  const rows = await storedRows();
  assert.deepEqual(rows.catalog.map(row => row.id), ['10', '11']);
  assert.deepEqual(rows.index.map(row => row.id), ['10', '11']);
  assert.ok(rows.catalog.every(row => row.updated_at === NOW));
  const state = await db.prepare('SELECT full_at, cycle_started_at, since FROM index_state WHERE id = 1')
    .first<{ full_at: number; cycle_started_at: number; since: number }>();
  assert.deepEqual(state, { full_at: NOW, cycle_started_at: 0, since: 0 });
});

test('querySteam browsing persists results, remembers ordered IDs, cleans old queries and reuses fresh pages', async t => {
  const { env, queries } = upstream(t, { '*': { files: [mod('10'), mod('11'), { ...mod('12'), visibility: 2 }], next: 'next' } });
  await db.prepare("INSERT INTO query_cache (key, ids, total, next_cursor, fetched_at) VALUES ('expired', '[]', 0, NULL, 0)").run();

  const page = await querySteam(env, ctx, 'mod', query);

  assert.deepEqual(page.items.map(item => item.id), ['10', '11']);
  const rows = await storedRows();
  assert.deepEqual(rows.catalog.map(row => row.id), ['10', '11']);
  assert.deepEqual(rows.index.map(row => row.id), ['10', '11']);
  const cached = await db.prepare('SELECT ids, total, next_cursor, fetched_at FROM query_cache WHERE key = ?')
    .bind(JSON.stringify(['mod', query])).first<{ ids: string; total: number; next_cursor: string; fetched_at: number }>();
  assert.deepEqual(cached, { ids: '["10","11"]', total: 3, next_cursor: 'next', fetched_at: NOW });
  assert.equal(await db.prepare("SELECT key FROM query_cache WHERE key = 'expired'").first(), null);
  assert.deepEqual(await querySteam(env, ctx, 'mod', query), page);
  assert.deepEqual(queries, ['*']);
});

test('querySteam collection browsing still persists collection data without indexing it as a mod', async t => {
  const { env } = upstream(t, { '*': { files: [{ ...mod('100'), file_type: 2, children: [{ publishedfileid: '10' }] }] } });
  const page = await querySteam(env, ctx, 'collection', query);
  assert.deepEqual(page.items.map(item => item.id), ['steam:100']);
  const rows = await storedRows();
  assert.deepEqual(rows.catalog.map(row => row.id), ['100']);
  assert.deepEqual(rows.index, []);
  assert.equal((await db.prepare('SELECT ids FROM query_cache').first<{ ids: string }>())?.ids, '["100"]');
});
