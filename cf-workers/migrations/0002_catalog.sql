CREATE TABLE IF NOT EXISTS catalog_items (
  id TEXT PRIMARY KEY,
  data TEXT NOT NULL CHECK(json_valid(data)),
  updated_at INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS picked_collections (
  slug TEXT PRIMARY KEY,
  title TEXT NOT NULL,
  author TEXT NOT NULL,
  description TEXT NOT NULL,
  preview_url TEXT,
  tags TEXT NOT NULL CHECK(json_valid(tags) AND json_type(tags) = 'array'),
  supported_versions TEXT NOT NULL CHECK(json_valid(supported_versions) AND json_type(supported_versions) = 'array'),
  member_ids TEXT NOT NULL CHECK(json_valid(member_ids) AND json_type(member_ids) = 'array'),
  featured_rank INTEGER NOT NULL DEFAULT 0,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS picked_collections_featured ON picked_collections(featured_rank, slug);
CREATE INDEX IF NOT EXISTS picked_collections_updated ON picked_collections(updated_at DESC, slug);
CREATE INDEX IF NOT EXISTS picked_collections_name ON picked_collections(title COLLATE NOCASE, slug);
