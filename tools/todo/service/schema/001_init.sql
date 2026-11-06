-- The todo store. Applied once by db.migrate() (schema_migrations keeps what ran).

CREATE TABLE areas (
    id                 text PRIMARY KEY,               -- "engine", "chess"
    title              text NOT NULL,
    vikunja_project    text,                           -- the title of the mirror project, null = no mirror
    vikunja_project_id integer
);

CREATE TABLE stages (
    area   text    NOT NULL REFERENCES areas (id) ON UPDATE CASCADE,
    number integer NOT NULL,
    title  text    NOT NULL,
    PRIMARY KEY (area, number)
);

CREATE TABLE entries (
    id               text PRIMARY KEY,                 -- unique over every area
    area             text NOT NULL REFERENCES areas (id) ON UPDATE CASCADE,
    title            text NOT NULL,
    body             text NOT NULL DEFAULT '',         -- markdown, without the title line
    status           text NOT NULL DEFAULT 'open'
                     CHECK (status IN ('open', 'in progress', 'review', 'deferred', 'closed')),
    status_date      date,
    status_note      text NOT NULL DEFAULT '',
    branch           text,                             -- todo/<branch-id> while taken
    stage            integer,
    plan             text,                             -- the owner's plan (plans.name), not a foreign key
    version          integer NOT NULL DEFAULT 1,
    created_at       timestamptz NOT NULL DEFAULT now(),
    updated_at       timestamptz NOT NULL DEFAULT now(),
    closed_at        timestamptz,
    vikunja_task_id  integer UNIQUE,
    vikunja_snapshot jsonb                              -- what the mirror last wrote: {title, description, done, labels}
);
CREATE INDEX entries_area_status ON entries (area, status);

CREATE TABLE plans (
    name       text PRIMARY KEY,                       -- a bare file name, "todo-loop-x.md"
    entry_id   text REFERENCES entries (id) ON UPDATE CASCADE ON DELETE SET NULL,
    kind       text NOT NULL CHECK (kind IN ('owner', 'slice', 'review')),
    content    text NOT NULL,
    version    integer NOT NULL DEFAULT 1,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX plans_entry ON plans (entry_id);

CREATE TABLE events (
    id       bigserial PRIMARY KEY,
    entry_id text,
    plan     text,
    at       timestamptz NOT NULL DEFAULT now(),
    actor    text NOT NULL,
    kind     text NOT NULL,                            -- create, update, claim, review, stop, close, reopen, plan, vikunja, ...
    data     jsonb NOT NULL DEFAULT '{}'
);
CREATE INDEX events_entry ON events (entry_id, at);

-- One row per entry whose task has to be written; the worker of the mirror empties it.
CREATE TABLE vikunja_outbox (
    entry_id   text PRIMARY KEY REFERENCES entries (id) ON UPDATE CASCADE ON DELETE CASCADE,
    due_at     timestamptz NOT NULL DEFAULT now(),
    attempts   integer NOT NULL DEFAULT 0,
    last_error text
);
