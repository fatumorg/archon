# ARCHON

ARCHON is an open-source, Linux-native tool for browsing and modding the archives of GTA V (Legacy and Enhanced). It aims to give OpenIV users the same workflow on Linux: open the game's RPF archives, extract and replace files, and install mod packages, with every change to the game backed up and reversible.

ARCHON ships no game files and no encryption keys. Keys are derived from your own copy of the game.

## Status

Early development. The interface is an empty window while archive reading is built in the core library.

## Build

```
nix build
./result/bin/archon
```

## Develop

```
nix develop
cmake --preset dev
cmake --build --preset dev
QT_QPA_PLATFORM=offscreen ctest --preset dev
```

Tests that read a real game install are skipped unless `ARCHON_GTA5_DIR` and `ARCHON_MAGIC_DAT` are set.

## Licence

See `LICENSE`.
