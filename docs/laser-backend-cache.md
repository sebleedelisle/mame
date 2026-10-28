# Reusable Laser MAME backends

`Laser Backend` builds the laser-only backend on Windows x64 and macOS Apple
Silicon (minimum macOS 12). It runs for backend changes on
`laser-pipe-ipc-mame`, or by manual dispatch with that branch/commit selected.
The upstream full-MAME workflows exclude the laser branch.

Both platform builds and standalone provider tests must pass before publishing
`laser-backend-v1-<full source SHA>` in this fork. The release holds unsigned
`.tar.gz` bundles and SHA-256 checksum files; it is a backend dependency cache,
not a Liberation Arcade app release. Published bundles are never overwritten.

Each bundle contains a manifest with the exact source commit, platform, build
variant and per-file hashes. Windows includes the required MinGW runtime DLLs
and embedded DWARF symbols. macOS includes a dSYM generated while the original
build objects are available. Licenses are included; ROMs are not included.

Liberation Arcade selects the bundle for its pinned MAME submodule commit,
validates the archive and manifest, and runs its own provider tests. A missing
bundle or locally modified source falls back to a source build. Arcade release
packaging applies production signatures and keeps the matching backend symbols.

The `v1` cache recipe builds Release with `SYMBOLS=1`, `SYMLEVEL=1`, and
`STRIP_SYMBOLS=0`. Incompatible recipe changes require a new cache prefix.
