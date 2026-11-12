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

The release is `X.Y.Z-N` (`N` is the packaging revision), tagged `wl-ngosen-X.Y.Z-N`. Every release is
a full one: the package repositories skip prereleases, so a prerelease would never reach users.

1. On a `release/X.Y.Z` branch, point the `ngosen` submodule at a released `ngosen-*` tag
   (`misc/check-core-tag.sh` must pass) and set the version in `CMakeLists.txt`,
   `packaging/debian/changelog`, `packaging/rpm/wl-ngosen.spec` and
   `packaging/rpm/opensuse/wl-ngosen.spec` (each with a `%changelog` entry) and
   `packaging/arch/PKGBUILD`. `packaging/check-version.sh X.Y.Z-N` must pass; the release workflow runs
   it on the tag.
2. Run the checks on niri, Sway and Hyprland from the release branch (see "Testing on a real
   compositor" in [AGENTS.md](AGENTS.md)) and list the results in the release PR.
3. `CHANGELOG.md`: turn `## [Chưa phát hành]` into `## [X.Y.Z-N] — DD/MM/YYYY` with a one-line summary,
   the core tag it uses and a link to the release, and start a new empty `## [Chưa phát hành]`.
4. Merge the PR into `main` once CI passes, then tag the merge commit through the API, not from a local
   checkout, where a stale local tag can point at the wrong commit:

   ```
   gh api repos/ngosen/wl-ngosen/git/refs -f ref=refs/tags/wl-ngosen-X.Y.Z-N -f sha=<merge commit>
   ```

5. The tag starts `Release` (`release.yml`), which builds every package and opens a draft release. Check
   the draft before publishing: the `.deb` for each Debian and Ubuntu codename and architecture, the
   Fedora RPMs for x86_64 and aarch64, the openSUSE RPM, the Arch package, the source tarball and
   `SHA256SUMS` are attached, and the sums match. A missing file stops the package repositories of
   every edition, not only this one. Release notes list only the compositors typed on with this
   version.
6. Publish it: `gh release edit wl-ngosen-X.Y.Z-N --repo ngosen/wl-ngosen --draft=false --latest`.
   There is no AUR package; Arch users get the package from the `[ngosen]` pacman repository.
7. Publishing starts `Fedora COPR` (`copr.yml`), which builds the release tarball in the COPR project
   `nguyenphi/ngosen`, next to `fcitx5-ngosen` and `ibus-ngosen`; check that it passes. It needs the
   `COPR_CONFIG` secret (the API token from https://copr.fedorainfracloud.org/api/) in the `release`
   environment. If it fails, rerun it:
   `gh workflow run copr.yml --repo ngosen/wl-ngosen --ref wl-ngosen-X.Y.Z-N`.
8. Refresh the APT, pacman and zypper repositories, which are built in `ngosen/ngosen` and run on their
   own only when a core version is released. Run the workflow from the newest `ngosen-*` tag and check
   that it passes:

   ```
   gh workflow run package-repos.yml --repo ngosen/ngosen --ref "$(gh release view --repo ngosen/ngosen --json tagName --jq .tagName)"
   ```

   That tag must already list `ngosen/wl-ngosen` in `package-repos.yml`; an older one builds the
   repositories without this edition and drops it from them. If a published release turns out broken,
   mark it as a prerelease and run the workflow again from the same newest tag: the download step
   skips prereleases and picks the previous release.
