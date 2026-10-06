CREATE TRIGGER IF NOT EXISTS catalog_items_ad AFTER DELETE ON catalog_items BEGIN
  DELETE FROM catalog_index WHERE id = old.id;
END;
