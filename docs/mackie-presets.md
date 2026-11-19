# Mackie Control presets for the modules

`presets/mackie-presets.xml` and `presets/scaler-mixer.xml` hold one Mackie Control preset for each kind of module, all named **Mackie Control: <module>**. They are built into the library: the folder **Mackie presets** is right below the Play presets and is open at every start. They are locked: **Duplicate** makes an editable copy. Select one, put the module on the instrument and upload it.

## What each one does

| Preset | What its controls are |
|---|---|
| Mackie Control: Scaler | the mixing surface of the Scaler: master and 8 faders, fine, shift, stop, rewind, fast forward, solo, mute, arm, loop, punch, undo, redo, play, record, pan and bank (see mixer-bridge.md) |
| Mackie Control: Play Keys | the same, for the Play Keys |
| Mackie Control: Matrix | the two rows of pads nearest to you (the lower ones in the picture) are mute 1 to 8, the two farther rows solo 1 to 8 |
| Mackie Control: Strips | faders 1 to 4 (pitch bend, channels 1 to 4) |
| Mackie Control: Rounds | faders 5 to 8 (pitch bend, channels 5 to 8): with the Strips, eight faders |
| Mackie Control: Bubbles | the four buttons are play, stop, record and cycle |
| Mackie Control: Grand Clavier, Mackie Control: Play Piano | the keys are record arm, solo and mute of 8 tracks (arm 1, arm 5 and the last key, select 1, are switched off, see below); the slider is the jog wheel, as on the Scaler; the two buttons are bank left and right |
| Mackie Control: Synth | the keys are transport and navigation from the note 89 (click, rewind, fast forward, stop, play, record, the cursors...); the four buttons are play, stop, record and cycle |
| Mackie Control: Play Drum | pads 1 to 6 are mute 1 to 6, pads 7 to 12 solo 1 to 6; the two buttons are play and record |
| Mackie Control: Play Audio | play, record, the three preset buttons as select 1 to 3, the effect button as cycle |

Notes are on channel 1 with velocity 127 and the buttons press and release like Mackie buttons. The numbers are those of the Mackie Control protocol: arm 0 to 7, solo 8 to 15, mute 16 to 23, select 24 to 31, bank left and right 46 and 47, cycle 86, rewind 91, fast forward 92, stop 93, play 94, record 95.

Together, Strips, Rounds and Matrix on the three slots are a surface of eight faders with mute and solo.

## The keyboards: switched-off keys and the slider

On the Grand Clavier and the Play Piano, Mackie Control switches off three keys: arm 1 and arm 5, since arm does not suit every kind of track, and select 1. While Mackie Control runs, a switched-off key sends nothing and is drawn dark in its window; without Mackie Control the keys still send their notes. The slider sends its controller 83 and Mackie Control turns it into the jog wheel (controller 60), exactly as it does for the Scaler's strip; it is labelled JOG in the window.

## The window and the port

When a module that runs a Mackie Control preset is put on the instrument, the program switches Mackie Control on, **opens the Mackie Control window** and **opens the MIDI port** `Joue Mackie` that the DAW sees. For the Scaler and the Play Keys the window is the mixing surface described in mixer-bridge.md. For the other modules it is **the module itself**: each control carries what it does (MUTE 3, SOLO 5, FADER 2...), is lit while it is pressed, and shows the lamps and the faders that the DAW reports (a track that is muted, soloed or armed, the playing state). What the module sends is passed on to the DAW unchanged, with the answers to the DAW's handshake. This can be switched off in the window.

## Which module runs one, and going back

In the live instrument, a module that runs a Mackie Control preset has a red **Mackie Control preset** mark next to **Edit**; it opens a red popup, once when the module is first seen and then when you click the mark, that says which preset it runs. **To go back to the original preset, load it again**: select the original preset of that module (*Pro Scaler*, *Pro Matrix*...) in the library and upload it. Do this before putting another Mackie preset on the module too: writing a Mackie preset over a module that already runs one does not always give the module that preset describes (the notes of the keys or pads may come back to their usual values).

## What was checked

The presets for the **Scaler**, the **Pro Matrix** and the **Pro Grand Clavier** were written into real module data and read back exactly. The others were checked only against the program's own tables (every setting is known for that module and has an allowed value): they have **not** been tried on a real module, and the window says so. Please report what you find.

## Not included

The Fretboards and the Guitar: a string sends only notes, so there is nothing useful for a mixer. The Pro Area has no direct Mackie equivalent. The Play modules can only be written by a Pro board.
