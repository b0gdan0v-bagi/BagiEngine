-- How an entry's slices go: null = each slice waits for the owner's merge; 'through' = the next slice starts on the same
-- branch as soon as the last one is approved, and the owner merges once (todo-loop/SKILL.md, "Through").
-- cost_cap: the dollars a through run of the entry may spend before it stops (null = no cap).
ALTER TABLE entries
    ADD COLUMN slicing  text CHECK (slicing IS NULL OR slicing = 'through'),
    ADD COLUMN cost_cap numeric(10, 2) CHECK (cost_cap IS NULL OR cost_cap > 0);
-- Two more kinds of plans: 'stages' (the stage plan of a whole entry, by its first architect) and 'remarks' (the owner's
-- remarks a review branch is sent back with).
ALTER TABLE plans DROP CONSTRAINT plans_kind_check;
ALTER TABLE plans ADD CONSTRAINT plans_kind_check CHECK (kind IN ('owner', 'slice', 'review', 'stages', 'remarks'));
