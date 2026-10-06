import { queryFiles, refreshSteamItems } from './steam';
import type { Env } from './types';

const PAGES_PER_TICK = 4;
const PAGE_SIZE = 100;
const FULL_REFRESH_MS = 7 * 24 * 60 * 60 * 1000;
const OVERLAP_MS = 60 * 60 * 1000;
const VERIFY_PREFIX = 'verify:';

interface IndexState {
  cursor: string;
  cycle_started_at: number;
  since: number;
  last_started_at: number;
  full_at: number;
  indexed: number;
}

async function loadState(db: D1Database): Promise<IndexState> {
  await db.prepare('INSERT OR IGNORE INTO index_state (id) VALUES (1)').run();
  const row = await db.prepare('SELECT cursor, cycle_started_at, since, last_started_at, full_at, indexed FROM index_state WHERE id = 1').first<IndexState>();
  return row!;
}

async function saveState(db: D1Database, state: IndexState): Promise<void> {
  await db.prepare(
    'UPDATE index_state SET cursor = ?, cycle_started_at = ?, since = ?, last_started_at = ?, full_at = ?, indexed = ?, updated_at = ? WHERE id = 1',
  ).bind(state.cursor, state.cycle_started_at, state.since, state.last_started_at, state.full_at, state.indexed, Date.now()).run();
}

/** Refreshes `ids`, bisecting on failure so one bad file cannot block its neighbours. */
async function refreshIsolating(env: Env, ids: string[]): Promise<boolean> {
  try {
    await refreshSteamItems(env, ids);
    return true;
  } catch (error) {
    if (ids.length === 1) {
      console.error(`[INDEX] Verification of ${ids[0]} failed`, error instanceof Error ? error.message : error);
      return false;
    }
  }
  const middle = ids.length >> 1;
  const left = await refreshIsolating(env, ids.slice(0, middle));
  const right = await refreshIsolating(env, ids.slice(middle));
  return left && right;
}

function parseVerifyCursor(cursor: string): { failed: boolean; after: string } {
  const [, failed, after] = cursor.split(':');
  return { failed: failed === '1', after: after ?? '' };
}

/**
 * Confirms stored mods that Steam's listing omitted, walking ids in keyset order.
 * Ids that keep failing are skipped for the rest of the pass so later ids still get
 * checked, but the pass then restarts instead of completing: the index is never
 * marked verified while an omitted file is unchecked.
 */
async function verifyUnseenMods(env: Env, state: IndexState): Promise<boolean> {
  const { failed, after } = parseVerifyCursor(state.cursor);
  const rows = await env.DB.prepare(
    `SELECT i.id FROM catalog_index AS i JOIN catalog_items AS c ON c.id = i.id
     WHERE c.updated_at < ? AND i.id > ? ORDER BY i.id LIMIT ?`,
  ).bind(state.cycle_started_at, after, PAGE_SIZE).all<{ id: string }>();
  const ids = rows.results.map(row => row.id);
  const ok = ids.length === 0 || await refreshIsolating(env, ids);
  const hasFailure = failed || !ok;
  if (ids.length === PAGE_SIZE) {
    state.cursor = `${VERIFY_PREFIX}${hasFailure ? 1 : 0}:${ids[ids.length - 1]}`;
    return false;
  }
  if (!hasFailure) return true;
  console.warn('[INDEX] Some omitted mods could not be verified; retrying the pass');
  state.cursor = `${VERIFY_PREFIX}0:`;
  return false;
}

async function finishCycle(db: D1Database, state: IndexState, now: number): Promise<void> {
  if (state.since === 0) state.full_at = now;
  state.last_started_at = state.cycle_started_at;
  state.cycle_started_at = 0;
  state.cursor = '*';
  console.log(`[INDEX] Cycle complete, ${state.indexed} mods seen in total`);
  await saveState(db, state);
}

/**
 * Walks the Workshop newest-updated first. The first cycle covers every mod and
 * marks the index ready; later cycles stop once they reach mods that were already
 * seen, with a full pass weekly to refresh subscription counts.
 */
export async function indexTick(env: Env): Promise<void> {
  if (!env.STEAM_API_KEY) return;
  const state = await loadState(env.DB);
  const now = Date.now();
  if (state.cycle_started_at === 0) {
    state.cycle_started_at = now;
    state.cursor = '*';
    state.since = state.full_at > 0 && now - state.full_at < FULL_REFRESH_MS ? Math.max(0, state.last_started_at - OVERLAP_MS) : 0;
  }

  for (let page = 0; page < PAGES_PER_TICK && !state.cursor.startsWith(VERIFY_PREFIX); page++) {
    const result = await queryFiles(env, 'mod', { query: '', tag: null, version: null, sort: 'updated', limit: PAGE_SIZE, cursor: state.cursor });
    state.indexed += result.items.length;
    const reachedSeen = state.since > 0 && result.times_updated.every(time => time * 1000 < state.since);
    if (result.next_cursor && !reachedSeen) {
      state.cursor = result.next_cursor;
      await saveState(env.DB, state);
      continue;
    }
    if (state.since !== 0) {
      await finishCycle(env.DB, state, now);
      return;
    }
    state.cursor = `${VERIFY_PREFIX}0:`;
    await saveState(env.DB, state);
  }
  if (!state.cursor.startsWith(VERIFY_PREFIX)) return;
  if (await verifyUnseenMods(env, state)) await finishCycle(env.DB, state, now);
  else await saveState(env.DB, state);
}
