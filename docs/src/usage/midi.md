<!--
SPDX-FileCopyrightText: 2025 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

# MIDI

## Map parameters to MIDI CC

All automatable parameters in Floe can be assigned to a MIDI CC. This allows you to control Floe from a MIDI controller for example.

To do this, right-click on the parameter you want to assign, and select 'MIDI Learn'. Then move the control on your MIDI controller that you want to assign to that parameter. This will create a mapping between the parameter and the MIDI CC. 

You can remove this mapping by right-clicking on the parameter and selecting 'Remove MIDI Learn'.

This mapping is saved with your DAW project. But it's not permanent. It only applies to the current instance of Floe in your DAW, and it won't be the applied if you load a new instance of Floe.

Preset files do not save MIDI CC mappings. So you can load presets and your MIDI CC mappings will remain.

![MIDI Learn](../images/midi-learn.png)

## Make MIDI CC mapping more permanent

You can make the MIDI CC mapping more permanent by right clicking on a 'MIDI learned' parameter and selecting 'Always set MIDI CC to this when Floe opens'. As the name suggests, when you open Floe, the MIDI CC mapping will be added.

## Default MIDI CC mappings

Floe sets some default MIDI CC mappings for you. You can turn this off in the preferences. The default mappings are:
==default-cc-mappings==

## Sustain Pedal

Floe can be controlled with a sustain pedal. A sustain pedal is a special kind of MIDI controller that sends MIDI CC-64 messages. These messages represent an on or off state.

When Floe receives a sustain pedal ON message, all notes that are currently held will sustain until a corresponding sustain pedal OFF message is received. The notes will persist even if the notes are released from the keyboard. Only releasing the sustain pedal will trigger them to stop. This is a common behaviour for synths and samplers alike. It roughly simulates the behaviour of a real piano sustain pedal.
