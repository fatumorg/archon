# Changelog

## 0.1.0

- GTA V Enhanced: File > Choose game switches between Legacy and Enhanced; Enhanced texture dictionaries, including their sRGB and R16 formats, are read and written.
- Text editor for `.meta`, `.xml`, `.dat` and other text files, with line numbers; keeps encoding, byte-order mark and line endings when saving.
- Model viewer for `.ydr`, `.ydd` and `.yft` from Legacy and Enhanced: orbit, pan and zoom, with diffuse textures from the model and its neighbouring `.ytd` files.
- Binary metadata (PSO, RSC META and RBF: `.ymt`, `.ymf`, `.ymap`, `.ytyp`, `.pso`, `.rbf`) opens as XML in the text editor and saves back to binary; files that do not round-trip byte-exact open read-only. Package Installer runs OIV `pso` commands.
- Application icon and desktop entry.
- Main window: navigation tree, file list, address bar, history, filter box.
- Search entry names across every archive.
- Extract files and folders from the interface.
- Game folder selection and key setup on first start.
- Edit mode: add, replace, delete, rename and new folder inside archives, nested archives included, and loose file replacement in the game folder.
- Copy archives to the `mods` folder before editing them.
- Rebuild archives to reclaim unused space.
- Every change is journaled and backed up; Tools > Change history rolls changes back, and an interrupted change is offered for rollback on the next start.
- Package Creator: writes OIV 2.2 packages from files added to the game folder or into archives (nested ones too) and files to delete, with name, version, author, descriptions, licence, colours and icon.
- Texture dictionaries (.ytd): preview every texture, export as DDS or PNG, and in edit mode replace, add (DDS keeps its format; PNG, JPEG, BMP and TGA become A8R8G8B8 with generated mipmaps), rename and remove textures.
- ASI Manager: install the ASI Loader (Ultimate ASI Loader) and the mods folder loader (RageOpenV) from pinned downloads; add, remove, enable and disable ASI plugins.
- Package Installer: installs OIV packages (2.0, 2.1, 2.2 and 1.x packages made for GTA V) into the "mods" folder or the game folder as one change, with file, archive, text and XML commands; uninstall from Change history. Package files are copied straight from the package to disk, so files of several gigabytes (NaturalVision Evolved) install.
- Proton prefix setup: installs .NET Framework 4.8, the Visual C++ runtime and d3dcompiler_47, sets Windows 10 and the loader override through protontricks, and verifies each component. protontricks runs with fsync off, so its many short Wine calls no longer fail at random, and the record keeps the output of a failed step.
- Builds with Qt 6.8 or newer and GCC 13 or newer; packages for Arch, Debian, Ubuntu, Fedora, Void and Nix in Frepo.

## 0.1.0-beta2

- Derive archive keys from the game executable.
- Read RPF7 archives, nested archives and resources.
- Extract files and folders.
- Detect Steam installs and list DLC.

## 0.1.0-beta1

Project skeleton.
