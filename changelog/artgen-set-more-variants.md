# Art generation: a finished set cannot get more variants on a new seed, and its review sits on a separate tab

- Version: 0.3.8
- Entry: artgen-set-more-variants
- Date: 2026-11-17
- Bump: patch

## Added
- A finished set can get more variants: the Set tab's **More variants** (count 1 to 16) and the control method `artgen_more {set, count?, seed?, jobs?}` queue N more variants per row on one fresh seed, appended to the old ones; picks and accepted rows stay, kept, failed, cancelled and full (64 variants) rows are skipped and listed. A more-run that fails, is cancelled or interrupted leaves the row done with its earlier variants. A set manifest job gets a `more` key (`null` in older manifests).

## Changed
- The Generate panel has no Review tab: the Set tab draws the set list and the selected set's rows (variants, pick, Keep, Accept all, Keep all) under the subject rows. Adapt: nothing.
- `artgen_keep` and the panel's Keep are refused (`artgen_busy`) while the set runs. Adapt: a client keeps after the set is done.
