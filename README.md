# ARCHON

ARCHON is an open-source, Linux-native tool for browsing and modding the archives of GTA V (Legacy and Enhanced). It aims to give OpenIV users the same workflow on Linux: open the game's RPF archives, extract and replace files, and install mod packages, with every change to the game backed up and reversible.

ARCHON ships no game files and no encryption keys. Keys are derived from your own copy of the game.

## Status

Early development. ARCHON browses and edits GTA V Legacy and Enhanced installs: archives and nested archives, texture dictionaries, text and binary metadata, OIV packages, ASI plugins and the Proton prefix. The model preview is not built yet.

## GTA V Enhanced

ARCHON finds both editions. File > Choose game switches between them; the tree, the address bar and the status bar show which one is open. Browsing, editing, textures, the Package Installer and Creator, the ASI Manager (the ASI Loader is `xinput1_4.dll` on Enhanced, `dinput8.dll` on Legacy) and the Proton prefix setup (app 3240220) all work on Enhanced. Enhanced texture dictionaries have their own layout, which ARCHON reads and writes. One Enhanced dictionary, `x64e.rpf/levels/generic/icons.rpf/markers.ytd`, uses an older resource version and does not open.

## Editing

ARCHON starts read-only. Turn on edit mode (`F6`) to add, replace, delete, rename and create folders inside archives, nested archives included, and to replace loose files in the game folder.

Editing an archive outside the `mods` folder first offers to copy it there, so the original stays untouched. The game only reads edited archives from `mods` when a mods loader is installed. Edited archives are saved unencrypted (`OPEN`). File > Rebuild writes an archive again without the space that earlier edits left unused.

Every change is written through a journal and backed up before any byte is overwritten. Tools > Change history lists the changes and rolls any of them back, together with every newer one. If ARCHON stops in the middle of a change, it offers to roll it back on the next start. A rollback is refused when the file was changed by something else afterwards, for example by Steam verifying the game.

## Text files

Opening a `.meta`, `.xml`, `.dat`, `.txt`, `.ini`, `.cfg`, `.ide`, `.ipl`, `.log`, `.csv`, `.lua` or `.json` file shows it with line numbers. In edit mode it can be changed and saved; the file keeps its encoding (UTF-8 or Latin-1), byte-order mark and line endings. Saving inside an archive outside `mods` asks first, like every other archive edit.

Binary metadata (`.ymt`, `.ymf`, `.ymap`, `.ytyp`, `.pso` and `.rbf` in PSO, RSC META or RBF form) opens in the same editor as XML and saves back to its binary form. Names come from the game: the first binary file opened collects every identifier in the install's text files and entry names, and hashes with no known name show as `hash_XXXXXXXX`. A file whose XML does not reproduce every byte of the original opens read-only and says why.

## Package Creator

Tools > Package Creator writes an OIV 2.2 package. The Package tab holds the name, version, author, descriptions, licence, header colours and icon; the Content tab lists files to add, each with its target folder in the game (archives included, for example `x64e.rpf/levels/gta5/vehicles.rpf`), and game files to delete. Packages it writes are OIV 2.2 and install with the Package Installer.

## Textures

Opening a `.ytd` file shows its textures with a preview. Export saves the selected texture or all of them as DDS or PNG. In edit mode the dictionary can be changed: replace or add textures from DDS files (kept in their own format and mipmaps) or from PNG, JPEG, BMP and TGA images (stored as A8R8G8B8 with generated mipmaps), rename and remove textures, then Save. Saving inside an archive outside `mods` asks first, like every other archive edit. ARCHON has no block-compression encoder, so an imported image is stored uncompressed.

Supported formats: DXT1, DXT3, DXT5, ATI1, ATI2, BC7 and the uncompressed A8R8G8B8, X8R8G8B8, A8B8G8R8, R5G6B5, A1R5G5B5, A8 and L8; on Enhanced also the sRGB forms of DXT1, DXT3, DXT5 and BC7, and R16, which a Legacy dictionary cannot hold. Block decoding uses [bcdec](https://github.com/iOrange/bcdec).

## Package Installer

Tools > Package Installer, or opening an `.oiv` file in the file list, shows the package with its own colours, description and licence. It installs into the `mods` folder (default: every archive the package changes is copied there first) or straight into the game folder. A package installs completely or not at all, and it is one entry in Change history, so rolling that entry back uninstalls it.

Supported: OIV 2.0, 2.1 and 2.2 (file add and delete, archives including nested and new ones, defragmentation, text XML and PSO commands) and 1.x packages that target GTA V. PSO commands edit binary metadata through its XML form and are refused for a file that opens read-only.

## ASI Manager

Tools > ASI Manager installs what the game needs to load mods:

- **ASI Loader**: [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) v10.0.2, installed as `dinput8.dll` (Legacy) or `xinput1_4.dll` (Enhanced).
- **Mods folder loader**: [RageOpenV](https://github.com/Chiheb-Bacha/RageOpenV) v1.0, the open-source replacement for OpenIV.ASI and OpenRPF.asi. With it the game reads archives from the `mods` folder.

Both are downloaded from their GitHub releases and checked against a pinned SHA-256; a local copy is accepted when it matches the same digest. The plugin list shows `.asi` files in the game folder, `scripts` and `plugins`. Disabling a plugin renames it to `.asi.disabled`. Every action is a change in Tools > Change history.

On Linux, Prefix setup in the ASI Manager prepares the game's Proton prefix for script mods with `protontricks`: .NET Framework 4.8, the Visual C++ 2015-2022 runtime, d3dcompiler_47, Windows 10 and the ASI Loader DLL override. Each component is checked after the run and the result is written to a log. Prefix changes cannot be rolled back. Under Proton GE 11.12 the .NET Framework 4.8 installer fails inside Wine (MSI error 1603); ARCHON reports it as not installed.

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
