-- Where an entry's branch was made: the base branch it was cut from and the checkout (`<game repository>@<host>`); null for an entry claimed before this.
ALTER TABLE entries ADD COLUMN base text, ADD COLUMN origin text;
