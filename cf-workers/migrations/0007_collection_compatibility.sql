ALTER TABLE collection_summaries ADD COLUMN no_common_version INTEGER NOT NULL DEFAULT 0 CHECK(no_common_version IN (0, 1));

-- Earlier empty intersections conflated conflicting declarations with unknown versions.
DELETE FROM collection_summaries;
