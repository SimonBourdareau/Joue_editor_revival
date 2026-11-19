# Mackie Control

Joué Revival can make the instrument a control surface for a DAW that speaks **Mackie Control** (Bitwig Studio, Ableton Live, Reaper, Logic...): faders, transport, solo, mute, arm, pan, banks and track names. It is a **preview**: the Scaler and Grand Clavier presets have been tried with Bitwig on Windows; the other presets have not been tried on a real module.

## Quick start

1. In the library open the folder **Mackie presets** (below the Play factory presets), select *Mackie Control: <your module>*, put the module on the instrument and press **Upload to Joué...**. The presets are locked; **Duplicate** makes an editable copy.
2. The program then switches Mackie Control on, opens its window and opens the MIDI port **Joue Mackie**. By hand: **View > Mackie Control...**, then **Connect**.
3. In the DAW add a Mackie Control controller and set both its MIDI input and its MIDI output to **Joue Mackie**. In Bitwig: **Settings > Controllers > Add controller > Mackie > Mackie Control**. Do this after step 2: the DAW lists the port only once it exists.
4. The window shows what the module sends and what the DAW answers; lines in amber come from the DAW.

**Ports.**

- **Windows:** install **loopMIDI** once (free for personal use: [tobias-erichsen.de/software/loopmidi.html](https://www.tobias-erichsen.de/software/loopmidi.html)), then restart Joué Revival. The program makes its port with the driver that loopMIDI installs: create no port in loopMIDI. The driver is not bundled; if it is missing, the window says so.
- **macOS, Linux:** nothing to install (the system's virtual ports; untried).
- To use ports that already exist (loopMIDI, IAC), choose **Use ports that already exist (loopMIDI, IAC...)** and pick **Send to** and **Listen to**.

## The presets

| Preset | Controls |
|---|---|
| Scaler, Play Keys | a mixing surface: master and 8 faders, solo, mute, arm, transport, pan, banks (see below) |
| Matrix | the two rows of pads nearest to you: mute 1 to 8; the two farther rows: solo 1 to 8 |
| Strips, Rounds | faders 1 to 4 and 5 to 8 (pitch bend, channels 1 to 8) |
| Bubbles | four buttons: play, stop, record, cycle |
| Grand Clavier, Play Piano | keys: arm, solo and mute of 8 tracks; slider: jog wheel; two buttons: bank left and right. Arm 1, arm 5 and the last key (select 1) are switched off: they send nothing while Mackie Control runs |
| Synth | keys from note 89: transport and navigation; four buttons: play, stop, record, cycle |
| Play Drum | pads 1 to 6: mute 1 to 6; pads 7 to 12: solo 1 to 6; buttons: play and record |
| Play Audio | play, record, the three preset buttons as select 1 to 3, effect as cycle |

There is no preset for the Fretboards and the Guitar (strings send only notes) or for the Pro Area. A Play module can only be written by a Pro board.

A module that runs one has a red **Mackie Control preset** label next to **Edit**. **To go back, upload the module's original preset again** (*Pro Scaler*, *Pro Matrix*...). Do the same before putting another Mackie preset on it: writing one Mackie preset over another does not give the same module as writing it over the original.

## The Scaler as a mixer

| Control | Does |
|---|---|
| Key 1 (leftmost) | master fader |
| Keys 2 to 9 | faders 1 to 8: the position of the finger along the key is the fader |
| Key 10, **FINE** | held, a fader moves a quarter as much |
| Key 11, **SHIFT** | held, keys 12 to 17 give their second function |
| Keys 12 to 17 | stop, rewind, fast forward, solo, mute, arm of the selected track. With shift: loop, punch, undo, redo, fader mode, click |
| Buttons 1 and 2 (right) | play and record |
| Bubble | left and right: pan of the selected track; up and down: previous and next bank of tracks |
| Strip | jog wheel |

The **selected track** is the last fader touched, or else the track the DAW has selected. A key has no motor, so a fader moved by the DAW can be somewhere else than the finger; the **fader mode** (**Fader mode:** in the window, or shift and key 16) says what to do: *absolute* goes where the finger is, *relative* starts from the DAW's value and follows the movement, *hybrid* waits until the finger reaches the DAW's value. The directions of the bubble, how far to push it and its repeat speed can be changed in the window. The Scaler's own **-** and **+** buttons shift the keys by an octave: the program copes, but better not press them.

## The other modules

For them the window draws **the module itself**: each control shows what it does (MUTE 3, SOLO 5, JOG...), is lit while pressed, and the DAW's lamps and faders are shown. The program forwards the module's messages unchanged (**Pass the module's messages through unchanged**, switched on by itself) and answers the DAW's handshake.

## Modules made in the designer

**Generate > Mackie Control...** in the [module designer](module-designer.md) makes pads with the Mackie notes: arm, solo, mute and select of 8 tracks, transport, banks, cursors. A module whose pads all send Mackie notes is recognised like a preset: Mackie Control switches on and the window draws it. There is no red label, since there is no original preset to go back to. Faders are not made there (a designer strip sends a controller, a Mackie fader is pitch bend): use the Strips or Rounds preset.

## Options

**Faders:** 8 (one Mackie unit, the default) or 16 (two units: the DAW needs a second unit, in Bitwig *Mackie Control + 1 Extender*, and the program makes a second port, **Joue Mackie 2**).

## If it does not work

| Symptom | Try |
|---|---|
| Nothing from the DAW in the log | connect in Joué Revival first, then add the controller in the DAW with **Joue Mackie** as both input and output |
| The window says the driver is missing (Windows) | install loopMIDI; the window has a button for its download page |
| The module does nothing | **View > Listen to the Play port (touches)** must be on; for the Scaler, **Translate** too |
| A key of a keyboard does nothing | arm 1, arm 5 and select 1 are switched off |
| Arm does nothing in the DAW | the track cannot be armed (a group, for instance) |

To report a problem: **Copy the log** in the window, then **Help > Copy a bug report** and **Help > Report a bug...** (see the README).

Not done yet: a table to change what each control does, and tests with DAWs other than Bitwig.
