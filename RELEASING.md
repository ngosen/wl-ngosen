# Releasing

What to update when a pull request is merged and when a version is released.

## After a pull request is merged

- `CHANGELOG.md`: one line under `## [Chưa phát hành]` saying what changed for the user, with the PR
  number. Part of every PR, not a follow-up.
- `README.md`: when a feature, a supported compositor or the list of limits changes.

## Branches

- `next` holds features, `patch` fixes for the current release, and `main` only what was released.
- After a release, bring `main` into `next` and `patch` through PRs.

## Copied code

`COPIED_FROM` names the ibus-ngosen commit the config, app modes, typing log and settings window were
copied from. The weekly `Copied code` workflow opens an issue when ibus-ngosen `next` has changed those
files since; bring the change over in a PR and move `COPIED_FROM` forward.

## Releasing a version

The release is `X.Y.Z-N` (`N` is the packaging revision), tagged `wl-ngosen-X.Y.Z-N`.

1. On a `release/X.Y.Z` branch, point the `ngosen` submodule at a released `ngosen-*` tag
   (`misc/check-core-tag.sh` must pass) and set the version in `CMakeLists.txt` and
   `packaging/arch/PKGBUILD`.
2. Run the checks on niri, Sway and Hyprland from the release branch (see "Testing on a real
   compositor" in [AGENTS.md](AGENTS.md)) and list the results in the release PR.
3. `CHANGELOG.md`: turn `## [Chưa phát hành]` into `## [X.Y.Z-N] — DD/MM/YYYY` with a one-line summary,
   the core tag it uses and a link to the release, and start a new empty `## [Chưa phát hành]`.
4. Merge the PR into `main` once CI passes, then tag the merge commit through the API, not from a local
   checkout, where a stale local tag can point at the wrong commit:

   ```
   gh api repos/ngosen/wl-ngosen/git/refs -f ref=refs/tags/wl-ngosen-X.Y.Z-N -f sha=<merge commit>
   ```

5. Check the draft release before publishing: the source tarball, the Arch package and `SHA256SUMS`
   are attached and the sums match; release notes list only the compositors typed on with this version.
6. Publish it: `gh release edit wl-ngosen-X.Y.Z-N --repo ngosen/wl-ngosen --draft=false --latest`.
