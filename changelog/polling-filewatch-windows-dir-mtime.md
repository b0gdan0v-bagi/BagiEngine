# Windows: the polling file watch misses a file created in the same clock tick as the last listing (FileWatchBackendPollingTest flaky)

- Version: 0.9.2
- Entry: polling-filewatch-windows-dir-mtime
- Date: 2026-11-23
- Bump: patch

## Changed
- `PollingFileWatchBackend` lists a folder again at every poll while its write time is within `kFolderStampUnsettledSeconds` (2 s) of the moment it was last listed: a file system stamps a folder from a coarse clock (NTFS from the system tick, FAT in 2 s steps), so a file made right after a listing could leave the stamp unchanged and was never reported. A quiet folder still costs stats only and allocates nothing.
  Adapt: nothing; a game's allocation test over the polling backend ages its freshly made folders first (`std::filesystem::last_write_time`), or it sees the listing.
