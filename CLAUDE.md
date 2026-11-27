# Notes for Claude

## Releases

- ZeroOS is released with the Release workflow (`.github/workflows/release.yml`): run it from
  main with a version such as `0.9.1`. Stay on 0.9.x until the owner says it is time for 1.0:
  every release, features or fixes, is a patch bump (0.9.1, 0.9.2, ...). Never go to 0.10 or 1.0
  without being asked.
- Every time a release is cut, update `README.md` in the same session: the "newest release is
  x.y.z" line in the Status section, and anything else that names the current release as the
  newest. Lines that say what changed "up to" or "after" an older version are history; leave them.
