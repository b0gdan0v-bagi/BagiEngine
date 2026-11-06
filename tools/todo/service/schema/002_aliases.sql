-- Old ids of an entry (the `id:` the old entry files kept for the tracker): a Vikunja marker with one of them links the task.
ALTER TABLE entries ADD COLUMN aliases text[] NOT NULL DEFAULT '{}';
CREATE INDEX entries_aliases ON entries USING gin (aliases);
