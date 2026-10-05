import { queryFiles } from './steam';
import type { Env } from './types';

const PAGES_PER_TICK = 4;
const PAGE_SIZE = 100;
const FULL_REFRESH_MS = 7 * 24 * 60 * 60 * 1000;
const OVERLAP_MS = 60 * 60 * 1000;

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

  for (let page = 0; page < PAGES_PER_TICK; page++) {
    const result = await queryFiles(env, 'mod', { query: '', tag: null, version: null, sort: 'updated', limit: PAGE_SIZE, cursor: state.cursor });
    state.indexed += result.items.length;
    const reachedSeen = state.since > 0 && result.times_updated.every(time => time * 1000 < state.since);
    if (result.next_cursor && !reachedSeen) {
      state.cursor = result.next_cursor;
      await saveState(env.DB, state);
      continue;
    }
    if (state.since === 0) state.full_at = now;
    state.last_started_at = state.cycle_started_at;
    state.cycle_started_at = 0;
    state.cursor = '*';
    console.log(`[INDEX] Cycle complete, ${state.indexed} mods seen in total`);
    await saveState(env.DB, state);
    return;
  }
}
