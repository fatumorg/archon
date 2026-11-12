# ARCHON

ARCHON is an open-source, Linux-native tool for browsing and modding the archives of GTA V (Legacy and Enhanced). It aims to give OpenIV users the same workflow on Linux: open the game's RPF archives, extract and replace files, and install mod packages, with every change to the game backed up and reversible.

ARCHON ships no game files and no encryption keys. Keys are derived from your own copy of the game.

## Status

Early development. ARCHON browses a GTA V install: folders, archives and nested archives, filtering and searching entry names, and extracting files. Installing mod packages is not built yet.

## Editing

ARCHON starts read-only. Turn on edit mode (`F6`) to add, replace, delete, rename and create folders inside archives, nested archives included, and to replace loose files in the game folder.

Editing an archive outside the `mods` folder first offers to copy it there, so the original stays untouched. The game only reads edited archives from `mods` when a mods loader is installed. Edited archives are saved unencrypted (`OPEN`). File > Rebuild writes an archive again without the space that earlier edits left unused.

Every change is written through a journal and backed up before any byte is overwritten. Tools > Change history lists the changes and rolls any of them back, together with every newer one. If ARCHON stops in the middle of a change, it offers to roll it back on the next start. A rollback is refused when the file was changed by something else afterwards, for example by Steam verifying the game.

Known limits:

- Archives encrypted with AES that contain scripts (`.ysc`) cannot be edited, and scripts cannot be added or renamed: ARCHON cannot encrypt them.
- An archive cannot grow past 4 GB.
- Folders and files outside archives cannot be renamed, and folders outside archives cannot be deleted.

On first start ARCHON downloads the public key table file `magic.dat` from the CodeWalker repository, checks it against a pinned SHA-256, and uses it with the key found in your own game executable.

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
