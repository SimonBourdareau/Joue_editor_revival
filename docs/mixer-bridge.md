# Mackie Control (preview)

The Scaler has seventeen keys, two buttons, a bubble and a strip: enough to be a small mixing surface. The program turns what the Scaler sends into **Mackie Control** messages, the protocol that Bitwig, Ableton Live, Logic, Reaper and others understand for faders, transport and track names.

**Status: preview.** The Scaler mixer preset, the translation and the connection to a DAW through two MIDI ports exist, and are tested here with synthetic messages and simulated ports. **No real DAW and no Scaler with the preset has been tried with it yet.** The Mackie Control window shows what is sent and what the DAW answers.

## The Scaler mixer preset

`presets/scaler-mixer.xml` is a library file with one preset, *Mackie Control: Scaler*. Import it with **File > Import presets...**, select it in the folder it creates, and **Upload to Joué...** with the Scaler on slot 1. Uploading writes into the Scaler's own tag: it makes the two buttons note buttons (the octave buttons are replaced), gives the keys a custom mapping of notes 36 to 52, and gives every key, the bubble and the strip their own controller numbers. The numbers did not change between versions of Mackie Control, so a Scaler that already carries the preset does not need to be uploaded again when Mackie Control changes what it does with them. **Restore the previous layout...** puts the Scaler back as it was. A Joué Play board cannot write to modules.

| Scaler | Sends | Mackie Control sends to the DAW |
|---|---|---|
| Key 1 (leftmost) | note 36, controller 14 (position along the key) | master fader: touch note 112, pitch bend on channel 9 |
| Keys 2 to 9 | notes 37 to 44, controllers 15 to 22 | faders 1 to 8: touch notes 104 to 111, pitch bend on channels 1 to 8 |
| Key 10, **FINE** | note 45 | held while a fader is touched, the fader moves only a quarter of the finger's movement (the fraction can be set) |
| Key 11, **SHIFT** | note 46 | held, keys 12 to 17 give their second function |
| Keys 12 to 17 | notes 47 to 52 | stop (93), rewind (91), fast forward (92), then solo, mute and record arm of the selected track (notes 8, 16 and 0 plus the track). With shift: cycle or loop (86), drop or punch (87), undo (81), redo (undo with shift held), the fader mode (nothing is sent) and click (89) |
| Button 1 and button 2 (the two on the right) | notes 94, 95 | play and record: notes 94 and 95, velocity 127 pressed and 0 released |
| Strip | controller 83 (absolute) | jog wheel: controller 60, values 1 to 63 to the right, 65 to 127 to the left |
| Bubble | controllers 80 (X), 81 (Y), 82 (pressure) | left and right: the pan of the selected track (the V-pot, controllers 16 to 23, two ticks a step, quickly repeated); up and down: bank left and right, so up goes to the previous tracks and down to the next. Each can be set to something else, or to nothing |

Some details. The Y control of the keys has 128 steps (the module does not store a 14-bit one), spread over the 16384 of a Mackie fader; the DAW applies its own curve to turn a fader position into dB. A key is touched from its note on to its note off. The bubble is let go of when its pressure falls to 0, or after 0.7 second without a message. The strip starts again from where the finger lands after 0.3 second without a message. Every other message from the keys is dropped while Mackie Control translates.

## Switching on by itself

The program recognises a Scaler that carries the mixer preset, because its notes and controller numbers are those of the preset and no other Scaler has them. As soon as such a module is on the instrument, **Translate** is switched on and the Mackie Control window opens; when the module is taken off, the translation is switched off again, and everything it holds is released. Two options in the window turn this off: **Switch on by itself when a mixer module is put on the instrument** and **and open this window**. A Scaler with its own layout is left alone.

## The Mackie Control window

**View > Mackie Control...** opens it. **Translate** switches the translation on; it needs **View > Listen to the Play port (touches)** on. The window draws the master on the left and then as many faders as the DAW has tracks (the DAW names them: eight at most, since a Mackie surface has eight strips), with their touches and select keys, the bubble, the strip, play and record, and a log of what arrives from the Scaler and what would go to the DAW. When a DAW answers (later), it also shows the track names, the position of the faders as the DAW has them (an orange line), the meters, the record arm, solo and mute lamps (R, S, M), the selected track, and the text that the DAW writes under each fader, which is usually its value in dB.

**The Scaler's own - and + buttons** (the two on the left) are its octave buttons: they shift the notes of the keys by 12 and cannot be made to send anything. The program recognises the shift from the position of the key that is touched and goes on working, but it is better not to press them.

**Fader modes.** The Scaler's faders are keys, and a key has no motor, so a fader that has been moved by the DAW (a bank change, an automation) can be somewhere else than the finger. *Absolute* sends the finger's position: the fader jumps. *Relative* starts each touch from the DAW's value and follows the movement of the finger from there. *Hybrid* waits until the finger reaches the DAW's value, then follows it. The mode is chosen in the window or with shift and the mode key (it cycles). Relative and hybrid need the DAW to report its fader positions, which a Mackie DAW does for its tracks; until it has, the position is used as it is. Holding the fine key during a touch makes any mode move slowly, and letting it go does not make the fader jump.

**The selected track.** Solo, mute, arm and the pan act on the last fader you touched; if none has been touched, on the track the DAW has selected; moving the faders along the tracks forgets the touch. The window shows it.

**The bubble** acts only while it is pressed (its pressure is above 0), and one direction at a time: a push is kept until the finger comes back to the centre, so a finger that rests near an edge cannot jump between two directions. It repeats once after 0.7 second and then every half second while it is pushed; **how far to push the bubble** and the speed of the repeat can be set in the window, and the bubble can be set to do nothing. When the finger leaves, the bubble slides its values back to 0; that is ignored. This was made from a recording of a real bubble.

**Banks.** A Mackie surface has eight strips. When the DAW has more tracks, the two keys on the right (and the bubble) push the eight faders along the tracks, one track at a time by default, and the DAW then sends the names and positions of the tracks that came in. With fewer than eight tracks there is nothing to move, and the DAW ignores bank commands. **Left and right** of the bubble select the previous or next track by default; the window can instead make them move one channel or one bank. **Try a command on the DAW** sends each Mackie command by hand (bank, channel, cursor, select, play, stop, record), which shows what your DAW answers to without touching the Scaler. **First track** sends enough bank and channel moves to the left to bring the faders back to the first track, which is useful after trying commands by hand. **Copy the log** puts the log on the clipboard, to paste it in a message when something does not work.

## Sixteen faders

A Mackie unit has eight strips, so one unit gives eight faders. For sixteen, the DAW needs a second Mackie unit (an extender), each unit having its own pair of MIDI ports. The window then has **Faders: 16 faders (two Mackie units)**.

- **The keys.** Key 1 is still the master. Keys 2 to 9 are faders 1 to 8, on the first unit, and keys 10 to 17 are faders 9 to 16, on the second unit. No key is left for selecting tracks or for moving them, so the bubble, left and right, moves the faders along the tracks (one channel at a time, or one bank), and the buttons are still play and record.
- **The window** shows the master on the left and then as many strips as the DAW names on the two units together.
- **Which half is first.** Which of the two units holds the first eight tracks depends on how the DAW places them: if the Scaler's left half shows the tracks 9 to 16, tick **The second unit holds the first eight tracks**.
- **In Bitwig**, add **Mackie > Mackie Control + 1 Extender**: it needs two pairs of MIDI ports, one for each unit. Make four loopMIDI ports, for instance `Joue to DAW 1`, `DAW 1 to Joue`, `Joue to DAW 2`, `DAW 2 to Joue`, give the first pair to the first unit and the second pair to the extender, and in the window choose the same four ports (**Send to** and **Listen to**, then the same two for the second unit). Not tried with a real Bitwig: the names of the choices in Bitwig may differ.

**Crossed ports.** If the DAW's two units are on the opposite pairs of ports from what you chose, the first unit's tracks are reported by the second pair and the faders act on the wrong block. When the DAW says which unit is which (device 0x14 for the main unit, 0x15 for the extender) the program swaps the two pairs by itself and says so in the log; otherwise tick **The DAW's main unit is on the second pair of ports**. The window shows which unit the DAW says it is on each pair.

**Pass-through.** **Pass the module's messages through unchanged** makes Mackie Control forward what the module sends, for the direct Mackie presets of the other modules (see mackie-presets.md); it still answers the handshake and shows the DAW's tracks.

**What the window tells you.** In this mode it says when the two ports of the second unit are not chosen, when nothing has come from a unit four seconds after the connection (the DAW is not using that pair of ports), and when both units show the same tracks (the DAW mirrors them instead of following).

**If one fader acts on two banks.** Each key goes to one unit only, and the log says which: **to the DAW** is the first unit, **to the second unit** the second. If you move one key and the DAW moves tracks in both banks, the two units are reaching the same place in the DAW. The program refuses to connect when the second unit is given the same ports as the first, or when a port is used both to send and to listen. Otherwise look in the DAW: its extender must use the second pair of ports, not the first, and must be placed after the main unit (and not mirror it). To see it without touching the Scaler, press **Select 1 (second unit)**: the DAW should select its ninth track, and **Select 1** its first. If both select the first track, the DAW is reading the two pairs of ports as the same unit.

## Connecting to a DAW

By default the program makes a MIDI port of its own, named `Joue Mackie` (and `Joue Mackie 2` for a second unit), which the DAW then sees as an input and an output. **Untried with a real DAW**: what follows is the setup I expect to work.

- **Windows.** The program uses the driver that **loopMIDI** installs (Tobias Erichsen's free virtual MIDI driver): install loopMIDI once, and do not create any port in it. The program loads that driver when it needs it and does not include it; check the loopMIDI licence for your use. If the driver is missing the window says so and has a button for its download page.
- **macOS** uses CoreMIDI virtual endpoints, and **Linux** a port of the ALSA sequencer (connect it with `aconnect` or in the DAW's port list). Neither has been tried.
- **Existing ports.** **Use ports that already exist** keeps the old way: two loopback ports (loopMIDI on Windows, the IAC driver on macOS), one for each direction, chosen as **Send to** and **Listen to**.

**With Bitwig**

1. In Joué Revival: **View > Mackie Control...**, press **Connect** (the program connects by itself afterwards, when a mixer module is put on the instrument). The port `Joue Mackie` now exists.
2. In Bitwig: **Settings > Controllers > Add controller > Mackie > Mackie Control**. Set both its MIDI input and its MIDI output to `Joue Mackie`. Do this after step 1: Bitwig lists the port only once it exists.
3. Put the Scaler with the mixer preset on the instrument, or tick **Translate** by hand.
4. Watch the log: lines in amber come **from the DAW**. A DAW that asks "device query" and then sends the names of its tracks is talking to us.

If something does not work, the log is what I need: the first lines after you press Connect, and what the DAW sends (**Copy the log**).

## Modules made in the module designer

A module made in the module designer whose pads all send Mackie Control notes (use **Generate > Mackie Control...**) is a Mackie Control module too: when it is on the instrument, Mackie Control switches on, opens its window, which draws the module with what each pad does, and opens the port. There is no original preset to go back to, so the red mark is not shown for it. See the [module designer guide](module-designer.md) and the [Mackie Control presets](mackie-presets.md).

## Reporting a problem

Press **Copy the log** in the window after trying, and use **Help > Copy a bug report** and **Help > Report a bug...**: the report holds the version, the device information, the modules, the detection log and the state of Mackie Control. See the README for the steps.

## What comes next

1. The Mackie handshake and the answers to the DAW's questions are written (device query, connection reply, version) but untested; programs differ in what they require.
2. Tests with the Mackie Control script of Bitwig and of Ableton Live, and with the ports that the program makes itself on each system.
3. A table to change what each key does, with a Learn button.
