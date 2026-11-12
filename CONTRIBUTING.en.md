[English](CONTRIBUTING.en.md) | [Tiếng Việt](CONTRIBUTING.md)

# Contributing to Ngó Sen for Wayland

Thanks for your interest in `wl-ngosen`! This repository is the Wayland shell of Ngó Sen: it connects a Wayland compositor to the
typing core in the `ngosen` submodule ([ngosen/ngosen](https://github.com/ngosen/ngosen)). Detailed rules for code
changes are in [AGENTS.md](AGENTS.md).

## Bugs, questions and ideas

- **Wrong text, hangs, lost letters:** open an issue with the [bug form](https://github.com/ngosen/wl-ngosen/issues/new/choose)
  and attach a typing log ("Lưu nhật ký gõ" / "Save typing log" in the tray menu, right after the error).
- **Install, update or removal fails:** use the install form and paste the whole terminal output.
- **A new feature:** use the feature form, after searching the existing requests.
- **How to install or use it:** ask in [Discussions](https://github.com/ngosen/wl-ngosen/discussions).
- **Bugs in the fcitx5 edition** (`fcitx5-ngosen`) go to the [ngosen repository](https://github.com/ngosen/ngosen/issues/new/choose).

## Build and test

The packages you need are listed in the README section [Dựng từ mã nguồn](README.md#dựng-từ-mã-nguồn).

```bash
git clone --recurse-submodules https://github.com/ngosen/wl-ngosen.git
cd wl-ngosen
cmake -S . -B build
cmake --build build -j"$(nproc)"
ctest --test-dir build
```

Reuse the `build/` directory for each build instead of creating a new one per experiment.

## Sending a change

1. Fork the repository and branch from `next` (features) or `patch` (fixes for the current release).
2. One logical change per commit, in English [Conventional Commits](https://www.conventionalcommits.org/):
   `fix(sen): keep the word after a click`.
3. Before pushing: `clang-format -i` the C++ files you changed, `ruff check` and `ruff format` if you changed
   `settings/`, then `ctest --test-dir build`.
4. A bug fix comes with a test that fails without it.
5. Add a line under `[Chưa phát hành]` in `CHANGELOG.md`: what changed for the user, with the PR number, in Vietnamese.
6. Open the PR against `next` or `patch` and fill in the template.

Changes to how words are typed (tone marks, auto-correction, Gõ Sen) belong in the core: send them to
[ngosen/ngosen](https://github.com/ngosen/ngosen).

## Branches

- `main` holds releases only and changes through the maintainer's release PR ([RELEASING.md](RELEASING.md)). Do not open PRs against `main`.
- `next`: features and everything else.
- `patch`: fixes for the current release, shipped as X.Y.Z+1 and then merged into `next`.
- If unsure, target `next`; the maintainer will retarget if needed.

## Code of conduct

Everyone taking part follows the [Code of Conduct](CODE_OF_CONDUCT.en.md).

## License

By contributing, you agree that your code is licensed under the project's license (GPL-3.0-or-later).
