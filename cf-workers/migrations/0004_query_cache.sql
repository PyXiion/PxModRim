CREATE TABLE IF NOT EXISTS query_cache (
  key TEXT PRIMARY KEY,
  ids TEXT NOT NULL CHECK(json_valid(ids) AND json_type(ids) = 'array'),
  total INTEGER NOT NULL,
  next_cursor TEXT,
  fetched_at INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS query_cache_fetched ON query_cache(fetched_at);
