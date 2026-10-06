-- Denormalised total download size (bytes) of collections whose members are all known.
CREATE TABLE IF NOT EXISTS collection_sizes (
  id TEXT PRIMARY KEY,
  total_size INTEGER NOT NULL
);

CREATE TRIGGER IF NOT EXISTS collection_sizes_pick_au AFTER UPDATE OF member_ids ON picked_collections
WHEN old.member_ids IS NOT new.member_ids BEGIN
  DELETE FROM collection_sizes WHERE id = 'picked:' || old.slug;
END;

CREATE TRIGGER IF NOT EXISTS collection_sizes_pick_ad AFTER DELETE ON picked_collections BEGIN
  DELETE FROM collection_sizes WHERE id = 'picked:' || old.slug;
END;

CREATE TRIGGER IF NOT EXISTS collection_sizes_item_au AFTER UPDATE OF data ON catalog_items
WHEN json_extract(new.data, '$.kind') = 'collection'
  AND json_extract(old.data, '$.member_ids') IS NOT json_extract(new.data, '$.member_ids') BEGIN
  DELETE FROM collection_sizes WHERE id = 'steam:' || old.id;
END;

CREATE TRIGGER IF NOT EXISTS collection_sizes_item_ad AFTER DELETE ON catalog_items BEGIN
  DELETE FROM collection_sizes WHERE id = 'steam:' || old.id;
END;
