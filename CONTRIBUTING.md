# Contributing

## Commit messages

Commit subjects (PR titles, since squash-merge makes the PR title the commit
message) follow [Conventional Commits](https://www.conventionalcommits.org/):

```
<type>(<optional scope>): <subject>
```

`type` is one of:

| type       | shows up in the changelog as |
|------------|-------------------------------|
| `feat`     | Added                         |
| `fix`      | Fixed                         |
| `refactor` | Changed                       |
| `perf`     | Changed                       |
| `docs`     | Changed                       |
| `style`    | (skipped)                     |
| `test`     | (skipped)                     |
| `chore`    | (skipped)                     |
| `ci`       | (skipped)                     |

Lowercase, no capital letter after the colon. `cliff.toml` matches these
exactly (case-insensitively) to build `CHANGELOG.md` from commit history on
every merge to `main`; anything that doesn't match one of these types is
dropped rather than showing up as a stray entry. See `cliff.toml` for the
full parsing rules.

Examples:

```
fix(overlay): restore the caller's GL matrix mode after the overlay pass
feat(diagnostics): show which heading source is currently active
ci: install redhat-lsb-core so the fedora tarball name gets a version
```

## Releasing

`./release.sh` manages the plugin version (`VERSION_MAJOR`/`VERSION_MINOR`/
`VERSION_PATCH` in `CMakeLists.txt`) and tagging; run it with no arguments for
usage. Pushing a `v*` tag triggers both `release.yml` (GitHub Release +
changelog) and `build.yml`'s Cloudsmith upload.
