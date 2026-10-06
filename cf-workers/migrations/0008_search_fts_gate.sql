-- Gate catalog_index FTS updates to run only when searchable text fields change.
-- Prevents write amplification and FTS shadow table churn when updating non-searchable
-- columns such as subscriptions, indexed_at, updated_at, or listed JSON metadata.
DROP TRIGGER IF EXISTS catalog_index_au;

CREATE TRIGGER IF NOT EXISTS catalog_index_au AFTER UPDATE ON catalog_index
WHEN old.title IS NOT new.title
  OR old.author IS NOT new.author
  OR old.tag_text IS NOT new.tag_text
  OR old.description IS NOT new.description
BEGIN
  INSERT INTO catalog_fts(catalog_fts, rowid, title, author, tag_text, description)
  VALUES ('delete', old.rowid, old.title, old.author, old.tag_text, old.description);
  INSERT INTO catalog_fts(rowid, title, author, tag_text, description)
  VALUES (new.rowid, new.title, new.author, new.tag_text, new.description);
END;
