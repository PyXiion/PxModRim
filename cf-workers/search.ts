import type { SteamQuery } from './steam';
import type { CatalogMod, CatalogPage } from './types';

const LIST_DESCRIPTION_CHARS = 300;
const FTS_DESCRIPTION_CHARS = 1000;
const MAX_TOKENS = 8;
const MAX_TOKEN_CHARS = 40;
const BATCH_SIZE = 100;
const CURSOR = /^ix:(\d{1,9})$/;

export interface IndexStatus {
  ready: boolean;
  indexed: number;
  full_at: number | null;
  cycle_in_progress: boolean;
}

export async function indexMods(db: D1Database, mods: CatalogMod[]): Promise<void> {
  const now = Date.now();
  const statement = db.prepare(
    `INSERT INTO catalog_index (id, title, author, tags, tag_text, description, data, subscriptions, created_at, updated_at, indexed_at)
     VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
     ON CONFLICT(id) DO UPDATE SET
       title = excluded.title, author = excluded.author, tags = excluded.tags, tag_text = excluded.tag_text,
       description = excluded.description, data = excluded.data, subscriptions = excluded.subscriptions,
       created_at = excluded.created_at, updated_at = excluded.updated_at, indexed_at = excluded.indexed_at
     WHERE catalog_index.data IS NOT excluded.data`,
  );
  for (let offset = 0; offset < mods.length; offset += BATCH_SIZE) {
    await db.batch(mods.slice(offset, offset + BATCH_SIZE).map(mod => {
      const listed: CatalogMod = { ...mod, description: mod.description.slice(0, LIST_DESCRIPTION_CHARS), previews: [] };
      return statement.bind(
        mod.id, mod.title, mod.author.name ?? '', JSON.stringify(mod.tags), mod.tags.join(' '),
        mod.description.replace(/\[[^\]]*\]/g, ' ').slice(0, FTS_DESCRIPTION_CHARS),
        JSON.stringify(listed), mod.subscriptions ?? 0, mod.created_at ?? 0, mod.updated_at ?? 0, now,
      );
    }));
  }
}

export async function indexStatus(db: D1Database): Promise<IndexStatus> {
  try {
    const state = await db.prepare('SELECT full_at, cycle_started_at FROM index_state WHERE id = 1')
      .first<{ full_at: number; cycle_started_at: number }>();
    const count = await db.prepare('SELECT count(*) AS n FROM catalog_index').first<{ n: number }>();
    return {
      ready: (state?.full_at ?? 0) > 0,
      indexed: count?.n ?? 0,
      full_at: state?.full_at ? state.full_at : null,
      cycle_in_progress: (state?.cycle_started_at ?? 0) > 0,
    };
  } catch {
    return { ready: false, indexed: 0, full_at: null, cycle_in_progress: false };
  }
}

function matchExpression(text: string): string | null {
  const tokens = (text.match(/[\p{L}\p{N}]+/gu) ?? []).slice(0, MAX_TOKENS).map(token => token.slice(0, MAX_TOKEN_CHARS));
  if (!tokens.length) return null;
  return tokens.map((token, index) => `"${token}"${index === tokens.length - 1 ? '*' : ''}`).join(' ');
}

const POPULARITY_BOOST = `(CASE WHEN catalog_index.subscriptions >= 100000 THEN 3.0
  WHEN catalog_index.subscriptions >= 10000 THEN 2.0
  WHEN catalog_index.subscriptions >= 1000 THEN 1.0 ELSE 0.0 END)`;

const ORDER: Record<string, string> = {
  popular: 'catalog_index.subscriptions DESC, catalog_index.id',
  updated: 'catalog_index.updated_at DESC, catalog_index.id',
  newest: 'catalog_index.created_at DESC, catalog_index.id',
  relevance: `(bm25(catalog_fts, 8.0, 2.0, 2.0, 1.0) - ${POPULARITY_BOOST}), catalog_index.id`,
};

/** Returns null when the index cannot answer this query and Steam should. */
export async function searchMods(db: D1Database, query: SteamQuery): Promise<CatalogPage<CatalogMod> | null> {
  const order = ORDER[query.sort];
  const match = matchExpression(query.query);
  const resumed = CURSOR.exec(query.cursor);
  if (!order || !match || (query.cursor !== '*' && !resumed)) return null;
  if (!(await indexStatus(db)).ready) return null;
  const offset = resumed ? Number(resumed[1]) : 0;

  const filters: string[] = [];
  const params: (string | number)[] = [match];
  for (const tag of new Set([query.tag, query.version].filter((value): value is string => value !== null))) {
    filters.push('AND EXISTS (SELECT 1 FROM json_each(catalog_index.tags) WHERE json_each.value = ?)');
    params.push(tag);
  }
  const from = `FROM catalog_fts JOIN catalog_index ON catalog_index.rowid = catalog_fts.rowid
    WHERE catalog_fts MATCH ? ${filters.join(' ')}`;
  try {
    const total = await db.prepare(`SELECT count(*) AS n ${from}`).bind(...params).first<{ n: number }>();
    const rows = await db.prepare(`SELECT catalog_index.data AS data ${from} ORDER BY ${order} LIMIT ? OFFSET ?`)
      .bind(...params, query.limit, offset).all<{ data: string }>();
    const items = rows.results.map(row => JSON.parse(row.data) as CatalogMod);
    const count = total?.n ?? 0;
    return { items, total: count, next_cursor: offset + items.length < count && items.length ? `ix:${offset + items.length}` : null };
  } catch (error) {
    console.error('Search index query failed', error instanceof Error ? error.message : error);
    return null;
  }
}
