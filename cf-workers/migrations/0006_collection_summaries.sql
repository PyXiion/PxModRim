-- Denormalised facts derived from a collection's members, written when its details are read.
-- total_size is decimal bytes (TEXT keeps full 64-bit precision) and NULL when any member size is unknown.
CREATE TABLE IF NOT EXISTS collection_summaries (
  id TEXT PRIMARY KEY,
  total_size TEXT,
  supported_versions TEXT NOT NULL CHECK(json_valid(supported_versions) AND json_type(supported_versions) = 'array'),
  computed_at INTEGER NOT NULL
);

CREATE TRIGGER IF NOT EXISTS collection_summaries_pick_au AFTER UPDATE OF member_ids ON picked_collections
WHEN old.member_ids IS NOT new.member_ids BEGIN
  DELETE FROM collection_summaries WHERE id = 'picked:' || old.slug;
END;

CREATE TRIGGER IF NOT EXISTS collection_summaries_pick_ad AFTER DELETE ON picked_collections BEGIN
  DELETE FROM collection_summaries WHERE id = 'picked:' || old.slug;
END;

CREATE TRIGGER IF NOT EXISTS collection_summaries_item_au AFTER UPDATE OF data ON catalog_items
WHEN json_extract(new.data, '$.kind') = 'collection'
  AND json_extract(old.data, '$.member_ids') IS NOT json_extract(new.data, '$.member_ids') BEGIN
  DELETE FROM collection_summaries WHERE id = 'steam:' || old.id;
END;

CREATE TRIGGER IF NOT EXISTS collection_summaries_item_ad AFTER DELETE ON catalog_items BEGIN
  DELETE FROM collection_summaries WHERE id = 'steam:' || old.id;
END;
