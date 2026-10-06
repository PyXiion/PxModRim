import { HttpError } from './types';
import type { CatalogCollection, PickedCollectionInput, PickListOptions } from './types';

interface PickRow {
  slug: string;
  title: string;
  author: string;
  description: string;
  preview_url: string | null;
  tags: string;
  supported_versions: string;
  member_ids: string;
  featured_rank: number;
  created_at: number;
  updated_at: number;
}

export function validatePickSlug(slug: string): void {
  if (typeof slug !== 'string' || slug.length > 80 || slug !== slug.trim() ||
      !/^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(slug)) {
    throw new HttpError(400, 'Invalid picked collection slug');
  }
}

export function parsePickInput(value: unknown): PickedCollectionInput {
  if (typeof value !== 'object' || value === null || Array.isArray(value)) {
    throw new HttpError(400, 'Picked collection input must be an object');
  }
  const input = value as Record<string, unknown>;
  const fields = new Set([
    'title', 'author', 'description', 'member_ids', 'preview_url', 'tags',
    'supported_versions', 'featured_rank',
  ]);
  for (const field of Object.keys(input)) {
    if (!fields.has(field)) throw new HttpError(400, `Unexpected field: ${field}`);
  }
  for (const field of ['title', 'author', 'description', 'member_ids']) {
    if (!Object.prototype.hasOwnProperty.call(input, field)) {
      throw new HttpError(400, `Missing required field: ${field}`);
    }
  }
  if (typeof input.title !== 'string' || input.title.length < 1 || input.title.length > 200) {
    throw new HttpError(400, 'title must contain 1 to 200 characters');
  }
  if (typeof input.author !== 'string' || input.author.length < 1 || input.author.length > 120) {
    throw new HttpError(400, 'author must contain 1 to 120 characters');
  }
  if (typeof input.description !== 'string' || input.description.length > 20000) {
    throw new HttpError(400, 'description must be a string of at most 20000 characters');
  }
  if (!Array.isArray(input.member_ids) || input.member_ids.length < 1 || input.member_ids.length > 500) {
    throw new HttpError(400, 'member_ids must contain 1 to 500 Steam IDs');
  }
  const memberIds: string[] = [];
  const seen = new Set<string>();
  for (const id of input.member_ids) {
    if (typeof id !== 'string' || id.length > 20 || id !== id.trim() ||
        !/^[1-9]\d*$/.test(id) || BigInt(id) > 18446744073709551615n) {
      throw new HttpError(400, 'member_ids must contain positive uint64 Steam IDs as decimal strings');
    }
    if (!seen.has(id)) {
      seen.add(id);
      memberIds.push(id);
    }
  }

  let previewUrl: string | null = null;
  if (Object.prototype.hasOwnProperty.call(input, 'preview_url')) {
    if (input.preview_url !== null) {
      if (typeof input.preview_url !== 'string' || input.preview_url.length > 2048) {
        throw new HttpError(400, 'preview_url must be null or an HTTPS URL of at most 2048 characters');
      }
      let url: URL;
      try {
        url = new URL(input.preview_url);
      } catch {
        throw new HttpError(400, 'preview_url must be a valid HTTPS URL');
      }
      if (url.protocol !== 'https:') throw new HttpError(400, 'preview_url must use HTTPS');
      previewUrl = input.preview_url;
    }
  }
  const tags: string[] = [];
  if (Object.prototype.hasOwnProperty.call(input, 'tags')) {
    if (!Array.isArray(input.tags) || input.tags.length > 30) {
      throw new HttpError(400, 'tags must contain at most 30 strings of at most 100 characters');
    }
    for (const tag of input.tags) {
      if (typeof tag !== 'string' || tag.length > 100) {
        throw new HttpError(400, 'tags must contain at most 30 strings of at most 100 characters');
      }
      tags.push(tag);
    }
  }
  const versions: string[] = [];
  if (Object.prototype.hasOwnProperty.call(input, 'supported_versions')) {
    if (!Array.isArray(input.supported_versions) || input.supported_versions.length > 20) {
      throw new HttpError(400, 'supported_versions must contain at most 20 versions in major.minor[.patch] form');
    }
    for (const version of input.supported_versions) {
      if (typeof version !== 'string' || version !== version.trim() ||
          !/^\d+\.\d+(?:\.\d+)?$/.test(version)) {
        throw new HttpError(400, 'supported_versions must contain at most 20 versions in major.minor[.patch] form');
      }
      versions.push(version);
    }
  }
  let featuredRank = 0;
  if (Object.prototype.hasOwnProperty.call(input, 'featured_rank')) {
    if (typeof input.featured_rank !== 'number' || !Number.isInteger(input.featured_rank) ||
        input.featured_rank < 0 || input.featured_rank > 1000000) {
      throw new HttpError(400, 'featured_rank must be an integer from 0 to 1000000');
    }
    featuredRank = input.featured_rank;
  }
  return {
    title: input.title,
    author: input.author,
    description: input.description,
    member_ids: memberIds,
    preview_url: previewUrl,
    tags,
    supported_versions: versions,
    featured_rank: featuredRank,
  };
}

function toCollection(row: PickRow): CatalogCollection {
  const memberIds = JSON.parse(row.member_ids) as string[];
  return {
    id: `picked:${row.slug}`,
    kind: 'collection',
    source: 'picked',
    steam_id: null,
    title: row.title,
    author: { id: null, name: row.author, profile_url: null },
    description: row.description,
    description_format: 'text',
    preview_url: row.preview_url,
    previews: [],
    workshop_url: null,
    tags: JSON.parse(row.tags) as string[],
    supported_versions: JSON.parse(row.supported_versions) as string[],
    featured_rank: row.featured_rank,
    created_at: row.created_at,
    updated_at: row.updated_at,
    member_ids: memberIds,
    member_count: memberIds.length,
    member_previews: [],
    total_size: null,
    no_common_version: false,
  };
}

export async function listPicks(
  db: D1Database,
  options: PickListOptions,
): Promise<{ items: CatalogCollection[]; total: number; next_offset: number | null }> {
  if (!Number.isSafeInteger(options.offset + options.limit)) {
    throw new HttpError(400, 'Invalid picked collection list options');
  }
  const predicates: string[] = [];
  const bindings: string[] = [];
  if (options.query !== '') {
    predicates.push('(instr(lower(title), lower(?)) > 0 OR instr(lower(author), lower(?)) > 0 OR instr(lower(description), lower(?)) > 0)');
    bindings.push(options.query, options.query, options.query);
  }
  if (options.tag !== null) {
    predicates.push('EXISTS (SELECT 1 FROM json_each(p.tags) WHERE value = ?)');
    bindings.push(options.tag);
  }
  if (options.version !== null) {
    predicates.push('EXISTS (SELECT 1 FROM json_each(p.supported_versions) WHERE value = ?)');
    bindings.push(options.version);
  }
  const where = predicates.length ? ` WHERE ${predicates.join(' AND ')}` : '';
  const orders = {
    featured: 'featured_rank ASC, slug ASC',
    updated: 'updated_at DESC, slug ASC',
    newest: 'created_at DESC, slug ASC',
    name: 'title COLLATE NOCASE ASC, slug ASC',
  };
  const [count, page] = await db.batch<PickRow | { total: number }>([
    db.prepare(`SELECT COUNT(*) AS total FROM picked_collections AS p${where}`).bind(...bindings),
    db.prepare(`SELECT p.* FROM picked_collections AS p${where} ORDER BY ${orders[options.sort]} LIMIT ? OFFSET ?`)
      .bind(...bindings, options.limit, options.offset),
  ]);
  const total = (count.results[0] as { total: number }).total;
  const items = page.results.map(row => toCollection(row as PickRow));
  const nextOffset = options.offset + items.length;
  return { items, total, next_offset: nextOffset < total ? nextOffset : null };
}

export async function getPick(db: D1Database, slug: string): Promise<CatalogCollection | null> {
  validatePickSlug(slug);
  const row = await db.prepare('SELECT * FROM picked_collections WHERE slug = ?').bind(slug).first<PickRow>();
  return row ? toCollection(row) : null;
}

export async function putPick(
  db: D1Database,
  slug: string,
  input: PickedCollectionInput,
): Promise<CatalogCollection> {
  validatePickSlug(slug);
  const now = Math.floor(Date.now() / 1000);
  const row = await db.prepare(`
    INSERT INTO picked_collections (
      slug, title, author, description, preview_url, tags, supported_versions,
      member_ids, featured_rank, created_at, updated_at
    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    ON CONFLICT(slug) DO UPDATE SET
      title = excluded.title,
      author = excluded.author,
      description = excluded.description,
      preview_url = excluded.preview_url,
      tags = excluded.tags,
      supported_versions = excluded.supported_versions,
      member_ids = excluded.member_ids,
      featured_rank = excluded.featured_rank,
      updated_at = excluded.updated_at
    RETURNING *
  `).bind(
    slug, input.title, input.author, input.description, input.preview_url,
    JSON.stringify(input.tags), JSON.stringify(input.supported_versions),
    JSON.stringify(input.member_ids), input.featured_rank, now, now,
  ).first<PickRow>();
  return toCollection(row!);
}

export async function deletePick(db: D1Database, slug: string): Promise<boolean> {
  validatePickSlug(slug);
  const result = await db.prepare('DELETE FROM picked_collections WHERE slug = ?').bind(slug).run();
  return result.meta.changes > 0;
}
