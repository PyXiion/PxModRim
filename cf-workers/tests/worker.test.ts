import assert from 'node:assert/strict';
import { readFile, readdir } from 'node:fs/promises';
import { resolve as resolvePath } from 'node:path';
import { after, before, beforeEach, test, type TestContext } from 'node:test';
import { Miniflare } from 'miniflare';
import { fetchSteamFileDetails } from '../steam';
import { HttpError } from '../types';
import type { Env } from '../types';
import { crawlTick } from '../worker';

const KEY = 'fixture-only-key';
const DETAILS_OPTIONS = {
  includeChildren: true, includeTags: false, includeKvTags: true, includeVotes: false, includeAdditionalPreviews: false,
};

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
        name: 'worker-tests', compatibilityDate: '2026-10-05',
        manifest: { mainModule: 'worker.js', modules: { 'worker.js': { type: 'esm', contents: 'export default { fetch() { return new Response("D1 test host"); } };' } } },
        env: { DB: { type: 'd1', id: 'worker-tests' } },
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
  await db.batch([db.prepare('DELETE FROM items'), db.prepare('DELETE FROM crawl_state')]);
  await db.prepare("INSERT INTO crawl_state (id, cursor, todo_ids, total_processed, updated_at) VALUES (1, ?, '', 0, 0)").bind('*').run();
});

function env(extra: Partial<Env> = {}): Env {
  return { DB: db, STEAM_API_KEY: KEY, ...extra };
}

async function savedState() {
  return await db.prepare('SELECT cursor, todo_ids, total_processed FROM crawl_state WHERE id = 1')
    .first<{ cursor: string; todo_ids: string; total_processed: number }>();
}

async function seedCursor(cursor: string): Promise<void> {
  await db.prepare('UPDATE crawl_state SET cursor = ? WHERE id = 1').bind(cursor).run();
}

function mockFetch(t: TestContext, handler: (url: URL, init?: RequestInit) => Response | Promise<Response>): URL[] {
  const calls: URL[] = [];
  t.mock.method(globalThis, 'fetch', async (input: RequestInfo | URL, init?: RequestInit) => {
    const url = new URL(input instanceof Request ? input.url : String(input));
    calls.push(url);
    return await handler(url, init);
  });
  return calls;
}

function captureErrors(t: TestContext): string[] {
  const messages: string[] = [];
  t.mock.method(console, 'error', (...args: unknown[]) => { messages.push(args.map(value => String(value)).join(' ')); });
  return messages;
}

test('a QueryFiles HTTP failure keeps the saved crawl cursor', async t => {
  await seedCursor('page-7');
  const errors = captureErrors(t);
  const calls = mockFetch(t, () => new Response('upstream down', { status: 500 }));

  await crawlTick(env());

  assert.equal(calls.length, 3);
  assert.deepEqual(await savedState(), { cursor: 'page-7', todo_ids: '', total_processed: 0 });
  assert.ok(errors.some(message => message.includes('keeping cursor "page-7"')), errors.join('\n'));
});

test('a Steam rate limit is reported without moving or resetting the cursor', async t => {
  await seedCursor('page-9');
  const errors = captureErrors(t);
  const calls = mockFetch(t, () => new Response('limited', { status: 429 }));

  await crawlTick(env());

  assert.equal(calls.length, 1);
  assert.deepEqual(await savedState(), { cursor: 'page-9', todo_ids: '', total_processed: 0 });
  assert.ok(errors.some(message => message.includes('HTTP 503')), errors.join('\n'));
});

test('an unparseable QueryFiles body cannot look like crawl exhaustion', async t => {
  await seedCursor('page-3');
  const errors = captureErrors(t);
  const calls = mockFetch(t, () => new Response('not json', { status: 200 }));

  await crawlTick(env());

  assert.equal(calls.length, 3);
  assert.deepEqual(await savedState(), { cursor: 'page-3', todo_ids: '', total_processed: 0 });
  assert.ok(errors.some(message => message.includes('HTTP 502')), errors.join('\n'));
});

test('a QueryFiles response without file details or total is an error, not an empty page', async t => {
  await seedCursor('page-3');
  const errors = captureErrors(t);
  const calls = mockFetch(t, () => Response.json({ response: {} }));

  await crawlTick(env());

  assert.equal(calls.length, 1);
  assert.deepEqual(await savedState(), { cursor: 'page-3', todo_ids: '', total_processed: 0 });
  assert.ok(errors.some(message => message.includes('invalid file details')), errors.join('\n'));
});

test('a successful empty page still restarts the crawl cycle', async t => {
  await seedCursor('page-3');
  const calls = mockFetch(t, () => Response.json({ response: { total: 0, publishedfiledetails: [], next_cursor: '' } }));

  await crawlTick(env());

  assert.equal(calls.length, 1);
  assert.deepEqual(await savedState(), { cursor: '*', todo_ids: '', total_processed: 0 });
});

test('an omitted details array with total zero is a legitimate empty page', async t => {
  await seedCursor('page-3');
  mockFetch(t, () => Response.json({ response: { total: 0 } }));

  await crawlTick(env());

  assert.deepEqual(await savedState(), { cursor: '*', todo_ids: '', total_processed: 0 });
});

test('a successful page advances the cursor without scraping fresh items', async t => {
  const now = Date.now();
  await db.batch([
    db.prepare('INSERT INTO items (id, title, is_collection, status, updated_at, package_id, deps) VALUES (?, ?, 0, ?, ?, ?, ?)').bind('501', 'Fresh A', 'OK', now, '', ''),
    db.prepare('INSERT INTO items (id, title, is_collection, status, updated_at, package_id, deps) VALUES (?, ?, 0, ?, ?, ?, ?)').bind('502', 'Fresh B', 'OK', now, '', ''),
  ]);
  const calls = mockFetch(t, () => Response.json({
    response: { total: 2, publishedfiledetails: [{ publishedfileid: '501' }, { publishedfileid: '502' }], next_cursor: 'page-2' },
  }));

  await crawlTick(env());

  assert.equal(calls.length, 1);
  assert.deepEqual(await savedState(), { cursor: 'page-2', todo_ids: '', total_processed: 0 });
});

test('stale crawl IDs resolve through the shared GetDetails operation with dependency options', async t => {
  const inputs: Record<string, unknown>[] = [];
  const calls = mockFetch(t, url => {
    if (url.pathname.includes('QueryFiles')) {
      return Response.json({ response: { total: 1, publishedfiledetails: [{ publishedfileid: '601' }], next_cursor: 'page-2' } });
    }
    assert.ok(url.pathname.includes('GetDetails'), `Unexpected Steam method ${url.pathname}`);
    const input = JSON.parse(url.searchParams.get('input_json')!) as { publishedfileids: string[] } & Record<string, unknown>;
    inputs.push(input);
    return Response.json({ response: { publishedfiledetails: input.publishedfileids.map(id => id === '601'
      ? { publishedfileid: '601', result: 1, title: 'Crawl root', file_type: 0, children: [{ publishedfileid: '602' }] }
      : { publishedfileid: '602', result: 1, title: 'Crawl dependency', file_type: 0, kvtags: [{ key: 'packageId', value: 'crawl.dependency' }] }) } });
  });

  await crawlTick(env());

  assert.equal(calls.length, 3);
  assert.deepEqual(inputs[0], {
    publishedfileids: ['601'],
    includechildren: true,
    includetags: false,
    includekvtags: true,
    includevotes: false,
    includeadditionalpreviews: false,
    short_description: false,
  });
  assert.deepEqual(await savedState(), { cursor: 'page-2', todo_ids: '', total_processed: 1 });
  const rows = await db.prepare('SELECT id, title, status, package_id, deps FROM items ORDER BY id')
    .all<{ id: string; title: string; status: string; package_id: string; deps: string }>();
  assert.deepEqual(rows.results, [
    { id: '601', title: 'Crawl root', status: 'OK', package_id: '', deps: '602' },
    { id: '602', title: 'Crawl dependency', status: 'OK', package_id: 'crawl.dependency', deps: '' },
  ]);
});

test('a Steam detail fault without a configured scraper fails instead of recording results', async t => {
  const errors = captureErrors(t);
  const calls = mockFetch(t, url => {
    if (url.pathname.includes('QueryFiles')) {
      return Response.json({ response: { total: 1, publishedfiledetails: [{ publishedfileid: '701' }], next_cursor: 'page-2' } });
    }
    return new Response('unavailable', { status: 503 });
  });

  await assert.rejects(
    crawlTick(env()),
    (error: unknown) => error instanceof HttpError && error.status === 502 && /SCRAPI is not configured/.test(error.message),
  );

  assert.equal(calls.length, 4);
  assert.deepEqual(await savedState(), { cursor: '*', todo_ids: '', total_processed: 0 });
  assert.equal(await db.prepare('SELECT count(*) AS n FROM items').first<{ n: number }>().then(row => row?.n), 0);
  assert.ok(errors.some(message => message.includes('HTTP 502')), errors.join('\n'));
});

test('the configured ScrapingAnt fallback still recovers a Steam detail fault', async t => {
  const scraperKeys: Array<string | null> = [];
  mockFetch(t, (url, init) => {
    if (url.hostname === 'api.scrapingant.com') {
      scraperKeys.push(new Headers(init?.headers).get('x-api-key'));
      return new Response('<div class="workshopItemTitle">Fallback mod</div>', { status: 200 });
    }
    assert.equal(url.hostname, 'api.steampowered.com');
    if (url.pathname.includes('QueryFiles')) {
      return Response.json({ response: { total: 1, publishedfiledetails: [{ publishedfileid: '801' }], next_cursor: 'page-2' } });
    }
    return new Response('unavailable', { status: 503 });
  });

  await crawlTick(env({ SCRAPI: 'fixture-scrapi-key' }));

  assert.deepEqual(scraperKeys, ['fixture-scrapi-key']);
  assert.deepEqual(await savedState(), { cursor: 'page-2', todo_ids: '', total_processed: 1 });
  assert.deepEqual(
    await db.prepare('SELECT title, status FROM items WHERE id = ?').bind('801').first<{ title: string; status: string }>(),
    { title: 'Fallback mod', status: 'OK' },
  );
});

test('the shared GetDetails operation classifies upstream faults instead of returning files', async t => {
  let status = 429;
  let payload: unknown = 'limited';
  const calls = mockFetch(t, () => typeof payload === 'string' ? new Response(payload, { status }) : Response.json(payload));

  await assert.rejects(
    fetchSteamFileDetails(env(), ['601'], DETAILS_OPTIONS),
    (error: unknown) => error instanceof HttpError && error.status === 503,
  );
  assert.equal(calls.length, 1);

  status = 200;
  payload = { response: { publishedfiledetails: [{ publishedfileid: '601', result: 1, tags: 'not-an-array' }] } };
  await assert.rejects(
    fetchSteamFileDetails(env(), ['601'], DETAILS_OPTIONS),
    (error: unknown) => error instanceof HttpError && error.status === 502 && /invalid file metadata/.test(error.message),
  );

  await assert.rejects(
    fetchSteamFileDetails({ DB: db }, ['601'], DETAILS_OPTIONS),
    (error: unknown) => error instanceof HttpError && error.status === 503 && /STEAM_API_KEY/.test(error.message),
  );
});
