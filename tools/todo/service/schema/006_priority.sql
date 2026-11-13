-- The owner's priority of an entry: null = not rated yet (counts as 'normal' when entries are ordered). Mirrored into the
-- task's priority field in Vikunja, both ways (service/vikunja.py, "Owned fields").
ALTER TABLE entries
    ADD COLUMN priority text CHECK (priority IS NULL OR priority IN ('now', 'high', 'normal', 'low'));
