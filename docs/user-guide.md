# Joué Revival: user guide

*English · [Français](user-guide.fr.md)*

This guide covers the main window. For drawing layouts, printing them and writing tags, see the [Module designer guide](module-designer.md).

## The window

- **Top: Live instrument.** The Joué as it is now: its three slots, the modules in them, and the touches. It says *Joué Disconnected* when the instrument is not there. **Hide** closes it; **View > Live instrument** brings it back.
- **Left: the library.** Folders and presets. The factory presets have a padlock.
- **Centre: the module.** Its picture (click an element to edit it) and, for a preset that fits a module in the instrument, the upload panel.
- **Right: Settings and MIDI mapping.** The settings of the module, and those of the element you clicked.

Menus: **File** (New library, Open..., Open from path..., Open the factory presets, Module designer..., Save, Export a copy..., Import presets..., Export my presets..., Quit), **Edit** (Undo, Redo), **View** (Device information, Module designer, Live instrument, Language / Langue, Touch to select, Listen to the Play port), **Help** (Quick start, User guide, Module designer guide, updates).

## The library

The factory presets (`presets/factoryLibrary.xml`) load by themselves the first time the program starts; **File > Open the factory presets** loads them again, and **File > Open...** opens another library file. The first time you press **Save** your own `library.xml` is made in the data folder; every save keeps the previous file in a `backups` folder, so no version is lost.

- **Factory presets are locked** (padlock). They cannot be deleted, and their settings are greyed. **Unlock to edit** unlocks one for this session; **Duplicate** makes a copy that is yours. Play Audio is locked like the others, though the original library leaves it unlocked.
- **New folder, Duplicate, Up, Down, Delete** work on the selected item. A folder that holds a locked preset cannot be deleted either.
- **Undo and Redo** (Ctrl+Z, Shift+Ctrl+Z) cover every change.
- **Search presets** (the box above the list) shows only the presets and folders whose name, or kind of module, contains what you type; folders open while you search, and clearing the box brings the list back.
- **File > Import presets...** adds the presets of another library file (for instance one that a friend exported) in a new folder named after the file; the locked factory presets in that file are left out. Undo takes the import away, and nothing is kept until you press **Save**. **File > Export my presets...** writes only your own presets and folders, to share them; it does not change your library.

## Editing a module's settings

Click an element in the picture to see its **MIDI mapping** on the right.

- **Buttons and pads** have an **Action** (Note, Channel selection, Sustain, Vibrato, Aftertouch, Y control, Glissando, Control change, Program change, Pitch bend); the options that follow belong to the Action chosen.
- **Ranges** have a **Direction** (Natural or Inverted; Up or Down for a pitch bend) that keeps the range when you change it.
- **Keys and pads, Custom mapping off:** a click edits the *group* (ALL_KEYS, ALL_PADS): its channel, velocity and Y control apply to every key at once, and on the Scaler and Play Keys it has the **First note** and the **Scale**.
- **Custom mapping on:** a click edits that key or pad alone. Ticking or unticking Custom mapping makes the selection follow.
- **Scales:** the 23 of the original editor, then 47 more under headings (melodic minor and its modes, harmonic minor, harmonic major, pentatonic and hexatonic, Japanese and Chinese, eastern and folk, bebop and symmetric). A scale and a first note rewrite the notes of all 17 keys; notes that follow no scale are a custom mapping.
- **Pressure range** (Low or High) sets the depth of every pad, key, string and button at once.

## The live instrument

1. **Quit the original Joué Editor** first: it holds the instrument's MIDI ports.
2. Connect the Joué. The modules appear in their slots with the picture of their layout. The board says the slot in which it reads the tag of a module, and the module starts at that slot minus its tag position plus one (the position is in the module itself: 1 on the Pro modules, whose tag is in their first slot, and 3 on the Play modules, whose tag is in their third). So a module of two slots on slots 2 and 3 is shown there, not on slots 1 and 2. If a module you put in is not shown, open **Details** under the live instrument: the log says what the board announced and, when the layout could not be read, why. Send those lines when you report it.
3. **Touches** are drawn from the notes, pressure and controllers the instrument plays; they are not measured positions. The **octave** buttons on the picture correct the octave the program assumes.
4. If something looks wrong, press **Rescan modules**, and open **Details** and its **Detection log**: they say what the board announced, what was read for each slot and why a read was refused.
5. **Touch to select** (View menu, on by default): put a finger on the instrument and the editor selects that element, as a click on it would, in the open module when it is of the same kind as the module you touched (a Scaler preset, or a Scaler opened with Edit). Touching the temporary layout of the designer selects the element in the designer. It needs the Play port: see the next point.
6. **View > Listen to the Play port (touches)** off: the program no longer holds the Play port (use it if a MIDI monitor or a synthesizer stops getting the notes), and no touches are drawn.

A module that the board announces but whose layout cannot be read is listed in orange (*detected, but its layout could not be read*); the reason is in the **Detection log**. If the Joué is in firmware update mode the program says so, instead of *No Joué found*: update it with the Joué firmware updater first. On a **Joué Play** board (one without the Pro firmware) the editor reads and shows the modules but cannot change them: uploading, writing and erasing a tag and the temporary layout are disabled, with a message. This has not been tried on a real Play board.

A new, blank tag in a slot is not a module: the panel lists it as *a new, blank tag (no module yet)*, **Erase a tag...** says there is nothing to erase, and **Write to a tag...** writes to it directly. Only a tag that holds a custom layout is erased. If the panel says nothing for a slot where you put a tag, open **View > Device information...**: the line *Tag in slot N* says whether the board sees a tag there at all.

## Editing the module that is in the instrument

Press **Edit** on a module in the live picture. It opens as a copy in the folder *From the instrument*, filled with what the module holds. Only the settings that can be written into that module are shown.

- **Upload to Joué...** lists the settings you changed and writes only those. The module's own layout, type and colour stay.
- **Restore the original...** writes back the layout as it was when read. **Revert my edits** drops your changes without touching the module.
- The original is saved as a file in the data folder (`module-backups`) before anything is written.

## Uploading a preset to a module

A preset has an **Upload to Joué** panel when a module of the same type, version and revision is in the instrument. The confirmation says whether the preset fits, and lists the settings that will change. **Restore the previous layout...** undoes the last upload.

## Device information

**View > Device information...** shows what the instrument says about itself, without writing anything: the hardware id and the family of firmware it takes, the limitations of the board (whether it is a Play board), the firmware version and build, the touch processor, the serial of the board, the id of the tag in each slot, the modules announced, and the MIDI ports in use. **Read again** reads it again; **Copy** puts it on the clipboard, always in English, to send with a question or a bug report. A board that does not answer is shown as *not answered*, never as zeros.

## Comparing

- **Compare...** (next to the name of a preset) lists the settings that differ between this preset and another one chosen from the library.
- **Compare with slot N...** (in the upload panel, when a module that fits is in the instrument) lists what an upload would change in that module. It is the list of the upload confirmation, and **nothing is written**.

## Help and updates

- **Help > Quick start** (also **F1**) shows the six first steps. The first time the program starts it opens by itself as *Welcome*; untick **Show this window at start** to stop that.
- **Help > User guide** and **Module designer guide** open the guide in your language: the file that is next to the program, or the online one when it is not there.
- **Updates.** Once a day, at start, the program asks GitHub for the latest release of the project and, when it is newer than the running version, shows **Version ... is available** at the right of the menu bar; a click opens the release page. It sends nothing but that request (GitHub sees your address, as for any web page). **Help > Check for updates at start** turns it off, **Check now** asks at once, and `--no-update-check` skips it for one start. It needs `curl` on macOS and Linux (installed on most systems) and uses the system's own library on Windows. It never downloads or installs anything.

## Language

The interface is in English or in French. The program starts in the language of your system; **View > Language / Langue** changes it at once, and the choice is kept (`language=` in `ui.txt`). `--lang en` or `--lang fr` on the command line sets it for that start only. The names that come from a library (the names of presets and folders) are not translated.

## Files

The data folder is `%APPDATA%\JoueEditorNG` on Windows, `~/Library/Application Support/JoueEditorNG` on macOS and `$XDG_DATA_HOME/JoueEditorNG` (or `~/.local/share/JoueEditorNG`) on Linux.

| File or folder | What it is |
|---|---|
| `library.xml` | Your library. |
| `backups/` | The previous library at every save. |
| `module-backups/` | The layout of a module before every upload. |
| `graphics.log` | What the graphics driver reported at the last start. |
| `ui.txt` | The few choices kept between starts (Listen to the Play port, Touch to select, the language, the welcome window, the update check and when it last ran). |

## Command line

`joue_editor [--library FILE] [--data DIR] [--select MODULE] [--designer FILE] [--scale S] [--safe-graphics] [--no-touches] [--lang en|fr] [--no-update-check] [--single-window]`

`--scale 1.5` makes everything larger, `--safe-graphics` draws the instrument without textures (for old drivers and virtual machines), `--no-touches` starts without listening to the Play port, `--designer FILE` opens the module designer on a layout.

## Troubleshooting

| Problem | What to do |
|---|---|
| Black areas in the window | Start with `--safe-graphics` (Windows: `JoueRevival-safe-graphics.bat`). |
| No *Connected* | Quit the original editor; unplug and plug the USB cable; look at the ports line under **Details**. If it says the Joué is in firmware update mode, update it with the Joué firmware updater first. |
| A module is missing or in the wrong slot | **Rescan modules**; read the **Detection log**. |
| The upload and tag buttons are greyed, with a message about a Joué Play | The board is a Play board: it cannot change modules, a Joué Pro is needed. |
| A module is listed in orange: *detected, but its layout could not be read* | Open **Details**, **Detection log**, and send the lines for that slot when you report it. |
| Another program gets no notes | Switch off **View > Listen to the Play port**. |
| macOS says the app is damaged or from an unknown developer | `xattr -cr "Joue Revival.app"`, or right click, **Open**. |
| Windows SmartScreen warns | **More info**, **Run anyway**: the file is not signed. |
