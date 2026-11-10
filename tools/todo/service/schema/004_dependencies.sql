-- "entry_id is blocked by blocker_id": the entry waits until the blocker is closed. Any two entries, across areas; no cycles (store.add_dependency checks).
CREATE TABLE dependencies (
    entry_id   text NOT NULL REFERENCES entries (id) ON UPDATE CASCADE ON DELETE CASCADE,
    blocker_id text NOT NULL REFERENCES entries (id) ON UPDATE CASCADE ON DELETE CASCADE,
    note       text NOT NULL DEFAULT '',                -- why, in a few words ("later slice: the appear motion")
    created_at timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (entry_id, blocker_id),
    CHECK (entry_id <> blocker_id)
);
CREATE INDEX dependencies_blocker ON dependencies (blocker_id);
