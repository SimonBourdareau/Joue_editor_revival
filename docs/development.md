# Joué Revival: development guide

## Repository layout

| Path | Content |
|---|---|
| `cpp/core` | The engine: SysEx and file transfer, the device, module blobs and the designer's `Design`, library XML, the PDF writer, the MIDI backends (CoreMIDI, winmm, ALSA), the French texts (`i18n_fr.inc`), and the tables measured from the manufacturer's applications (`bindings_data.inc`, `settings_data.inc`, `moduleart_data.inc`). |
| `cpp/app` | The Dear ImGui editor: the main window, the live instrument, the module designer, the pictures of the modules (`device_pictures.inc`, `logo.inc`), the icons. |
| `cpp/third_party` | `stb_image.h`. |
| `docs` | The READMEs and the two guides, in English and in French, and the pictures they use. |
| `packaging` | What the release workflow uses: the packages, the AppImage, the Windows installer (`joue-revival.iss`), the disk image, the macOS notarization, the version check and the release notes. |
| `.github/workflows` | `build.yml` and `release.yml`. |
| `Joue Editor.bat`, `Joue Editor.command`, `joue-editor.sh` | Launchers that build on first use and start the program. |

Dear ImGui, GLFW and pugixml are fetched by CMake at the first configure; nothing else is needed. The tests, the generators of the tables, the notes made while reverse engineering the instrument and the first implementation in C# are not part of this source.

## Building

All systems: CMake 3.20 or newer, a C++17 compiler, git, and a network connection for the first configure (it fetches pugixml 1.14, GLFW 3.4 and Dear ImGui 1.91.9).

```sh
cmake -S cpp -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
build/joue_editor
```

**Linux.** Packages (Debian, Ubuntu): `build-essential cmake git libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl-dev libasound2-dev`. GLFW is built for X11 only: the window works under XWayland. ALSA is optional: without it the program builds with a stub MIDI backend and cannot reach the instrument. `joue-editor.sh` builds when needed and starts the program.

**macOS.** Xcode command line tools and CMake. The program links CoreMIDI and CoreFoundation. `Joue Editor.command` builds on first use and starts the program. The CoreMIDI backend handles the packet layout of both processors, but only Apple silicon has been built and run.

**Windows.** MinGW-w64 (MSYS2) or Visual Studio 2022. The MinGW build links statically so that the program runs on a clean machine. `Joue Editor.bat` configures, builds and starts the program with whichever generator CMake picks. The resource file (`joue.rc.in`) gives the executable its icon and version information.

## Translations

The interface is in English or French. English is the language of the source: a visible text is written in English between `tr("...")` in `cpp/app` (`tr` is `joue::i18n::tr`), and its French is one line of `cpp/core/src/i18n_fr.inc`: the English text, a tab (written `\t`), the French text.

- A **label or a format string** (`ImGui::Button(tr("Save"))`, `ImGui::Text(tr("Slot %d"), n)`): an exact entry; `%d` and the other specifiers stay in the same order. A text with `##id` keeps its id.
- A **message made at run time**, with names and numbers in it: `joue::i18n::message(text)` at the place where it is shown, with an entry in which `{}` stands for each part that varies: `Slot {}: layout read, {}, {} bytes`. The most specific entry wins, and a part that varies is translated too when it is a message itself. `\n` is a line break.
- A **text from data** (the title of a setting, a choice in a list, a scale): `joue::i18n::text(text)`, an exact entry.
- A text with no entry is shown in English. A text that stays the same in French is written with the same text on both sides. Avoid the characters that the interface font does not have (œ, the typographic apostrophe, the ellipsis character): write `...` and `'`.

The language is chosen in the View menu; `--lang en|fr` overrides it for a start. Look at a new text in both languages in the running program (`build/joue_editor --lang fr`).

## The tables

`bindings_data.inc`, `settings_data.inc` and `moduleart_data.inc` were measured from the manufacturer's macOS application (its own encoder, run in an emulator, one setting at a time) and from the four modules of a real Joué. They say which bytes of a module each setting moves, the titles and choices of the settings, and the drawing of each module. They are data: change them only to add a module or a setting, and check the result against a real module.

## Continuous integration and releases

Two workflows in `.github/workflows` (both pass `actionlint`).

**`build.yml`** runs on every push to `main` or `master` and on pull requests (and by hand): it builds **Linux** (Ubuntu 22.04), **macOS** (Apple silicon; an Intel build is tried too and may fail without stopping the rest) and **Windows** (MSYS2 MinGW-w64).

**`release.yml`** makes a release from the source code when a tag that starts with `v` is pushed:

```sh
# raise the version in cpp/CMakeLists.txt, commit, then
git tag v0.37.0 && git push origin v0.37.0
```

1. **Version:** `packaging/check-version.sh` stops the release when the tag is not the version of the project.
2. **Build** on the three systems, then the packages with `packaging/package-release.sh` (a zip for Windows and macOS, a `tar.gz` for Linux, each with the README, the guides in both languages and the licence).
3. **Installers**, each in a step that may fail without stopping the release: an **AppImage** for Linux (`packaging/make-appimage.sh`, with `appimagetool` downloaded in the job); a **Windows installer** with Inno Setup (`packaging/joue-revival.iss`, English and French); a **disk image** for macOS (`packaging/make-dmg.sh`).
4. **macOS signing and notarization**, only when the secrets are set in the repository (**Settings > Secrets and variables > Actions**): `MACOS_CERTIFICATE` (the Developer ID Application certificate, `.p12`, in base64), `MACOS_CERTIFICATE_PASSWORD`, `MACOS_SIGN_IDENTITY`, `APPLE_ID`, `APPLE_TEAM_ID` and `APPLE_APP_PASSWORD` (an app-specific password). `packaging/notarize-macos.sh` does it. Without them the app is signed ad hoc and the first start needs `xattr -cr`. It needs a paid Apple developer account.
5. **Publication:** the source of the tag (`git archive`), `SHA256SUMS.txt`, the notes from the commits since the previous tag (`packaging/release-notes.sh`), and the release itself on GitHub (a tag with a dash, such as `v0.37.0-beta`, is a pre-release).

**Dry run:** Actions > Release > *Run workflow*, with "publish" left off, does everything except the publication; the packages can be downloaded from the run.

The two workflows **have not run on GitHub yet**, and the Windows installer, the disk image and the notarization could not be tried here: the first real run may need small fixes (runner labels, action versions, the Inno Setup path). The AppImage was built and started on Linux. The factory libraries of the manufacturer are never in a release.

`packaging/package-release.sh linux|windows|macos BUILD_DIR OUT_DIR` makes the same package by hand from any finished build; the macOS app is signed ad hoc (`codesign`, or `rcodesign` on Linux).

## Versions

One delivered version is one commit, tagged `vX.Y.Z`; no version is ever lost. The version is in `cpp/CMakeLists.txt` (`project(... VERSION ...)`): the program shows it at the bottom right and in About, and the Windows resource file carries it.

## Licence

GNU GPL v3 or later (`LICENSE`). A contribution is accepted under the same licence. The manufacturer's data (its factory libraries) are not covered by it and are never added to the repository.
