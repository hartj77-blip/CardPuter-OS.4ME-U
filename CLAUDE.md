# Notes for Claude

## Releases

- ZeroOS is released with the Release workflow (`.github/workflows/release.yml`): run it from
  main with a version such as `0.5.0`. Pick the version from what changed since the last tag
  (new features: minor bump; fixes only: patch bump).
- Every time a release is cut, update `README.md` in the same session: the "newest release is
  x.y.z" line in the Status section, and anything else that names the current release as the
  newest. Lines that say what changed "up to" or "after" an older version are history; leave them.
