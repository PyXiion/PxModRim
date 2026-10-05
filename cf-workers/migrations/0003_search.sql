CREATE TABLE IF NOT EXISTS catalog_index (
  id TEXT PRIMARY KEY,
  title TEXT NOT NULL,
  author TEXT NOT NULL DEFAULT '',
  tags TEXT NOT NULL CHECK(json_valid(tags) AND json_type(tags) = 'array'),
  tag_text TEXT NOT NULL DEFAULT '',
  description TEXT NOT NULL DEFAULT '',
  data TEXT NOT NULL CHECK(json_valid(data)),
  subscriptions INTEGER NOT NULL DEFAULT 0,
  created_at INTEGER NOT NULL DEFAULT 0,
  updated_at INTEGER NOT NULL DEFAULT 0,
  indexed_at INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS catalog_index_popular ON catalog_index(subscriptions DESC, id);
CREATE INDEX IF NOT EXISTS catalog_index_updated ON catalog_index(updated_at DESC, id);
CREATE INDEX IF NOT EXISTS catalog_index_created ON catalog_index(created_at DESC, id);

CREATE VIRTUAL TABLE IF NOT EXISTS catalog_fts USING fts5(
  title, author, tag_text, description,
  content = 'catalog_index', content_rowid = 'rowid',
  tokenize = 'unicode61 remove_diacritics 2'
);

CREATE TRIGGER IF NOT EXISTS catalog_index_ai AFTER INSERT ON catalog_index BEGIN
  INSERT INTO catalog_fts(rowid, title, author, tag_text, description)
  VALUES (new.rowid, new.title, new.author, new.tag_text, new.description);
END;

CREATE TRIGGER IF NOT EXISTS catalog_index_ad AFTER DELETE ON catalog_index BEGIN
  INSERT INTO catalog_fts(catalog_fts, rowid, title, author, tag_text, description)
  VALUES ('delete', old.rowid, old.title, old.author, old.tag_text, old.description);
END;

CREATE TRIGGER IF NOT EXISTS catalog_index_au AFTER UPDATE ON catalog_index BEGIN
  INSERT INTO catalog_fts(catalog_fts, rowid, title, author, tag_text, description)
  VALUES ('delete', old.rowid, old.title, old.author, old.tag_text, old.description);
  INSERT INTO catalog_fts(rowid, title, author, tag_text, description)
  VALUES (new.rowid, new.title, new.author, new.tag_text, new.description);
END;

CREATE TABLE IF NOT EXISTS index_state (
  id INTEGER PRIMARY KEY CHECK(id = 1),
  cursor TEXT NOT NULL DEFAULT '*',
  cycle_started_at INTEGER NOT NULL DEFAULT 0,
  since INTEGER NOT NULL DEFAULT 0,
  last_started_at INTEGER NOT NULL DEFAULT 0,
  full_at INTEGER NOT NULL DEFAULT 0,
  indexed INTEGER NOT NULL DEFAULT 0,
  updated_at INTEGER NOT NULL DEFAULT 0
);

INSERT OR IGNORE INTO index_state (id) VALUES (1);
