# Parameters

> A list of all parameters in Floe

## Shorten parameter names

In your DAW, the names of Floe's parameters are long and descriptive, such as "Effect Distortion On". You can switch to abbreviated names if you need the names to show in a smaller width, for example, "Dist On". Enable this option on Floe's preferences panel. You might need to close and re-open your DAW for this change to take effect.

## Parameter table

For reference purposes, this section lists all of Floe's parameters in case you need to look up specific IDs or details. For more of a usage guide, consult the documentation pages on layers, effects, looping, etc.

| Module | Name | ID | Abbreviated Name | Description |
| --- | --- | --- | --- | --- |
| Layer 1 | Volume | 160 | L1 Volume | Layer volume |
| Layer 1 | Mute | 161 | L1 Mute | Mute this layer |
| Layer 1 | Solo | 162 | L1 Solo | Mute all other layers |
| Layer 1 | Pan | 163 | L1 Pan | Left/right balance |
| Layer 1 | Detune Cents | 164 | L1 Detune Cents | Layer pitch in cents; hold shift for finer adjustment |
| Layer 1 | Pitch Semitones | 165 | L1 Pitch Semitones | Layer pitch in semitones |
| Layer 1/Playback/Loop | Start | 167 | L1Lp Start | Loop-start |
| Layer 1/Playback/Loop | End | 168 | L1Lp End | Loop-end |
| Layer 1/Playback/Loop | Crossfade Size | 169 | L1Lp Crossfade Size | Crossfade length; this smooths the transition from the loop-end to the loop-start |
| Layer 1/Playback/Loop | Sample Start Offset | 171 | L1Lp Sample Start Offset | Change the starting point of the sample |
| Layer 1/Playback/Loop | Reverse On | 172 | L1Lp Reverse On | Play the sound in reverse |
| Layer 1/Main/Volume Envelope | On | 173 | L1Vol On | Enable/disable the volume envelope; when disabled, each sound will play out entirely |
| Layer 1/Main/Volume Envelope | Attack | 174 | L1Vol Attack | Volume fade-in length |
| Layer 1/Main/Volume Envelope | Decay | 175 | L1Vol Decay | Volume ramp-down length (after the attack) |
| Layer 1/Main/Volume Envelope | Sustain | 176 | L1Vol Sustain | Volume level to sustain (after decay) |
| Layer 1/Main/Volume Envelope | Release | 177 | L1Vol Release | Volume fade-out length (after the note is released) |
| Layer 1/Main/Filter | On | 178 | L1Filt On | Enable/disable the filter |
| Layer 1/Main/Filter | Legacy Cutoff Frequency | 179 | L1Filt Legacy Cutoff Frequency | Legacy cutoff parameter. Kept for backwards-compatibility with DAW automation |
| Layer 1/Main/Filter | Legacy Resonance | 180 | L1Filt Legacy Resonance | Legacy resonance parameter. Kept for backwards-compatibility with DAW automation |
| Layer 1/Main/Filter | Legacy Type | 181 | L1Filt Legacy Type | Legacy filter type parameter. Kept for backwards-compatibility with DAW automation |
| Layer 1/Main/Filter | Envelope Amount | 182 | L1Filt Envelope Amount | How strongly the envelope should control the filter cutoff |
| Layer 1/Main/Filter | Attack | 183 | L1Filt Attack | Length of initial ramp-up |
| Layer 1/Main/Filter | Decay | 184 | L1Filt Decay | Length ramp-down after attack |
| Layer 1/Main/Filter | Sustain | 185 | L1Filt Sustain | Level to sustain after decay has completed |
| Layer 1/Main/Filter | Release | 186 | L1Filt Release | Length of ramp-down after note is released |
| Layer 1/LFO | On | 187 | L1Lfo On | Enable/disable the Low Frequency Oscillator (LFO) |
| Layer 1/LFO | Legacy Shape | 188 | L1Lfo Legacy Shape | Legacy LFO shape parameter. Kept for backwards-compatibility with DAW automation |
| Layer 1/LFO | Mode | 189 | L1Lfo Mode | Oscillator phase mode. Retrigger: each voice has its own phase, Free: all voices that are playing simultaneously will have the same phase |
| Layer 1/LFO | Amount | 190 | L1Lfo Amount | Intensity of the LFO effect |
| Layer 1/LFO | Target (Legacy) | 191 | L1Lfo Target (Legacy) | Legacy LFO target parameter. Kept for backwards-compatibility with DAW automation |
| Layer 1/LFO | Time (Tempo Synced) | 192 | L1Lfo Time (Tempo Synced) | LFO rate (synced to the host) |
| Layer 1/LFO | Time (Hz) | 193 | L1Lfo Time (Hz) | LFO rate (in Hz) |
| Layer 1/LFO | Sync On | 194 | L1Lfo Sync On | Sync the LFO speed to the host |
| Layer 1/EQ | On | 195 | L1Eq On | Turn on or off the equaliser effect for this layer |
| Layer 1/EQ/Band 1 | Legacy Frequency | 196 | L1EqB1 Legacy Frequency | Legacy band 1 frequency parameter. Kept for backwards-compatibility with DAW automation |
| Layer 1/EQ/Band 1 | Legacy Resonance | 197 | L1EqB1 Legacy Resonance | Legacy resonance parameter. Kept for backwards-compatibility with DAW automation |
| Layer 1/EQ/Band 1 | Gain | 198 | L1EqB1 Gain | Band 1: volume gain at the frequency |
| Layer 1/EQ/Band 1 | Legacy Type | 199 | L1EqB1 Legacy Type | Legacy type parameter. Kept for backwards-compatibility with DAW automation |
| Layer 1/EQ/Band 2 | Legacy Frequency | 200 | L1EqB2 Legacy Frequency | Legacy band 2 frequency parameter. Kept for backwards-compatibility with DAW automation |
| Layer 1/EQ/Band 2 | Legacy Resonance | 201 | L1EqB2 Legacy Resonance | Legacy resonance parameter. Kept for backwards-compatibility with DAW automation |
| Layer 1/EQ/Band 2 | Gain | 202 | L1EqB2 Gain | Band 2: volume gain at the frequency |
| Layer 1/EQ/Band 2 | Legacy Type | 203 | L1EqB2 Legacy Type | Legacy type parameter. Kept for backwards-compatibility with DAW automation |
| Layer 1/Config | Legacy Velocity Mapping | 204 | L1 Legacy Velocity Mapping | Choose how MIDI velocity should affect the volume of this layer. There are 6 modes that can be selected for this parameter via the buttons on the GUI. By setting one layer to be quiet at high velocities and another layer to be quiet at low velocities you can create an instrument that sounds different based on how hard the notes are played. (0) Ignore velocity, always play full volume. (1) Loudest at high velocity, quietist at low velocity (2) Loudest at low velocity, quietist at high velocity (3) Loudest at high velocity, quietist at middle velocity and below (4) Loudest at middle velocity, quietist at both high and low velocities (5) Loudest at bottom velocity, quietist at middle velocity and above, |
| Layer 1/Config | Keytrack On | 205 | L1 Keytrack On | Tune the sound to match the key played; if disabled it will always play the sound at its root pitch |
| Layer 1/Config | Legacy Monophonic On | 206 | L1 Legacy Monophonic On | Only allow one voice of each sound to play at a time |
| Layer 1/Config | MIDI Transpose On | 208 | L1 MIDI Transpose On | Transpose the mapping of samples by the given semitone offset, meaning a higher/lower sample may be triggered instead of stretching/shrinking the audio by large amounts (only useful if the instrument is multi-sampled) |
| Layer 1/Playback/Loop | Loop Mode | 209 | L1Lp Loop Mode | The mode for looping the samples |
| Layer 1/Config | Key Range Low | 210 | L1 Key Range Low | The lowest key that will trigger this layer; if the key is lower than this, the layer will not play |
| Layer 1/Config | Key Range High | 211 | L1 Key Range High | The highest key that will trigger this layer; if the key is higher than this, the layer will not play |
| Layer 1/Config | Key Range Low Fade | 212 | L1 Key Range Low Fade | The length of the volume fade-in at the low end of the key range |
| Layer 1/Config | Key Range High Fade | 213 | L1 Key Range High Fade | The length of the volume fade-out at the high end of the key range |
| Layer 1/Config | Pitch Bend Range | 214 | L1 Pitch Bend Range | The pitch range in semitones of the MIDI pitch wheel |
| Layer 1/Config | Monophonic Mode | 215 | L1 Monophonic Mode | Control voice behavior when notes overlap. Off: multiple voices play simultaneously (polyphonic). Retrigger: new notes stop previous notes. Latch: first note plays until all keys are released, new notes are ignored |
| Layer 1/Playback | Play Mode | 216 | L1 Play Mode | How this layer plays its samples |
| Layer 1/Playback/Granular | Length | 217 | L1Grn Length | Duration of each grain snippet |
| Layer 1/Playback/Granular | Speed | 218 | L1Grn Speed | How fast the grain position moves through the sample |
| Layer 1/Playback/Granular | Position | 219 | L1Grn Position | Where in the sample grains are sourced from |
| Layer 1/Playback/Granular | Density | 220 | L1Grn Density | Controls how densely grains overlap, relative to the grain length. At the midpoint, grains play end-to-end. Lower values add gaps between grains for a sparse texture; higher values make grains overlap for a denser, richer sound |
| Layer 1/Playback/Granular | Spread | 221 | L1Grn Spread | Region around the playhead where grains can start from. Small values focus grains near the playhead, large values spread them across a wider area |
| Layer 1/Playback/Granular | Smooth | 222 | L1Grn Smooth | Crossfade between grains to remove clicks. Low is hard cuts, high is full overlap fade |
| Layer 1/Playback/Granular | Pan | 223 | L1Grn Pan | Randomise the stereo position of each grain. At 0% all grains play centred, at 100% grains can be panned anywhere from fully left to fully right |
| Layer 1/Playback/Granular | Detune | 224 | L1Grn Detune | Randomise the pitch of each grain. At 0% all grains play at the original pitch, at 100% grains can be detuned up to a semitone up or down |
| Layer 1/Playback/Granular | Direction | 225 | L1Grn Direction | Chance that grains spawn playing in the opposite direction to the main playhead. At 0% all grains play in the main direction, at 100% there's a 50/50 chance of each grain playing forwards or backwards |
| Layer 1/Playback/Granular | Harmony | 226 | L1Grn Harmony | Chance that grains spawn at one of the selected harmony intervals instead of the root pitch. Configure which intervals are active using the Intervals button |
| Layer 1/LFO | Legacy Shape V2 | 227 | L1Lfo Legacy Shape V2 | Legacy LFO shape parameter (v2). Kept for backwards-compatibility with DAW automation |
| Layer 1/LFO | Target | 228 | L1Lfo Target | The parameter that the LFO will modulate |
| Layer 1/Main/Filter | Resonance | 229 | L1Filt Resonance | The intensity of the volume peak at the cutoff frequency |
| Layer 1/Arp | Note Order | 230 | L1Arp Note Order | Order in which held notes are played |
| Layer 1/Arp | Trigger | 231 | L1Arp Trigger | Free: arpeggiator keeps running when new notes are pressed. Retrigger: arpeggiator restarts from step 1 |
| Layer 1/Arp | Rate | 232 | L1Arp Rate | Arpeggiator rate (synced to host tempo) |
| Layer 1/Arp | Length | 233 | L1Arp Length | Number of active steps in the arpeggiator pattern |
| Layer 1/Arp | Arpeggiator Mode | 234 | L1Arp Arpeggiator Mode | Played Notes: arpeggiates held notes. Fixed Notes: plays a recorded note sequence |
| Layer 1/Arp | Humanise | 235 | L1Arp Humanise | Add random timing variation to note starts and velocity. Higher values create looser, more human-like performance |
| Layer 1/Arp | Auto Rate | 236 | L1Arp Auto Rate | Let Floe decide the rate so that the loop plays near to its original recorded rate rather than having to add large silent gaps between slices. The rate that Floe chooses varies depending on your host tempo. |
| Layer 1/Arp | Polyrate | 237 | L1Arp Polyrate | Each octave plays at a different rate. Double means each octave up is 2x faster. 3:2 and 4:3 create polyrhythmic relationships between octaves |
| Layer 1/Arp | One Shot | 238 | L1Arp One Shot | When enabled, the arpeggiator plays through the sequence once and then stops instead of looping |
| Layer 1/EQ/Band 1 | Resonance | 239 | L1EqB1 Resonance | Band 1: sharpness of the peak |
| Layer 1/EQ/Band 2 | Resonance | 240 | L1EqB2 Resonance | Band 2: sharpness of the peak |
| Layer 1/Arp | Arpeggiator | 241 | L1Arp Arpeggiator | Enable/disable the arpeggiator |
| Layer 1/LFO | Shape | 242 | L1Lfo Shape | Oscillator shape, including random and percussive waveforms |
| Layer 1/EQ/Band 1 | Type | 244 | L1EqB1 Type | Band 1: type of EQ band |
| Layer 1/EQ/Band 2 | Type | 245 | L1EqB2 Type | Band 2: type of EQ band |
| Layer 1/EQ/Band 3 | Legacy Frequency | 246 | L1EqB3 Legacy Frequency | Legacy band 3 frequency parameter. Kept for backwards-compatibility with DAW automation |
| Layer 1/EQ/Band 3 | Resonance | 247 | L1EqB3 Resonance | Band 3: sharpness of the peak |
| Layer 1/EQ/Band 3 | Gain | 248 | L1EqB3 Gain | Band 3: volume gain at the frequency |
| Layer 1/EQ/Band 3 | Type | 249 | L1EqB3 Type | Band 3: type of EQ band |
| Layer 1/Main/Filter | Cutoff Frequency | 250 | L1Filt Cutoff Frequency | The frequency at which the filter should take effect |
| Layer 1/EQ/Band 1 | Frequency | 251 | L1EqB1 Frequency | Band 1: frequency of this band |
| Layer 1/EQ/Band 2 | Frequency | 252 | L1EqB2 Frequency | Band 2: frequency of this band |
| Layer 1/EQ/Band 3 | Frequency | 253 | L1EqB3 Frequency | Band 3: frequency of this band |
| Layer 1/Main/Filter | Type | 254 | L1Filt Type | Filter type |
| Layer 1 | Stereo Width | 255 | L1 Stereo Width | Layer stereo width: negative narrows toward mono, positive widens |
| Layer 2 | Volume | 320 | L2 Volume | Layer volume |
| Layer 2 | Mute | 321 | L2 Mute | Mute this layer |
| Layer 2 | Solo | 322 | L2 Solo | Mute all other layers |
| Layer 2 | Pan | 323 | L2 Pan | Left/right balance |
| Layer 2 | Detune Cents | 324 | L2 Detune Cents | Layer pitch in cents; hold shift for finer adjustment |
| Layer 2 | Pitch Semitones | 325 | L2 Pitch Semitones | Layer pitch in semitones |
| Layer 2/Playback/Loop | Start | 327 | L2Lp Start | Loop-start |
| Layer 2/Playback/Loop | End | 328 | L2Lp End | Loop-end |
| Layer 2/Playback/Loop | Crossfade Size | 329 | L2Lp Crossfade Size | Crossfade length; this smooths the transition from the loop-end to the loop-start |
| Layer 2/Playback/Loop | Sample Start Offset | 331 | L2Lp Sample Start Offset | Change the starting point of the sample |
| Layer 2/Playback/Loop | Reverse On | 332 | L2Lp Reverse On | Play the sound in reverse |
| Layer 2/Main/Volume Envelope | On | 333 | L2Vol On | Enable/disable the volume envelope; when disabled, each sound will play out entirely |
| Layer 2/Main/Volume Envelope | Attack | 334 | L2Vol Attack | Volume fade-in length |
| Layer 2/Main/Volume Envelope | Decay | 335 | L2Vol Decay | Volume ramp-down length (after the attack) |
| Layer 2/Main/Volume Envelope | Sustain | 336 | L2Vol Sustain | Volume level to sustain (after decay) |
| Layer 2/Main/Volume Envelope | Release | 337 | L2Vol Release | Volume fade-out length (after the note is released) |
| Layer 2/Main/Filter | On | 338 | L2Filt On | Enable/disable the filter |
| Layer 2/Main/Filter | Legacy Cutoff Frequency | 339 | L2Filt Legacy Cutoff Frequency | Legacy cutoff parameter. Kept for backwards-compatibility with DAW automation |
| Layer 2/Main/Filter | Legacy Resonance | 340 | L2Filt Legacy Resonance | Legacy resonance parameter. Kept for backwards-compatibility with DAW automation |
| Layer 2/Main/Filter | Legacy Type | 341 | L2Filt Legacy Type | Legacy filter type parameter. Kept for backwards-compatibility with DAW automation |
| Layer 2/Main/Filter | Envelope Amount | 342 | L2Filt Envelope Amount | How strongly the envelope should control the filter cutoff |
| Layer 2/Main/Filter | Attack | 343 | L2Filt Attack | Length of initial ramp-up |
| Layer 2/Main/Filter | Decay | 344 | L2Filt Decay | Length ramp-down after attack |
| Layer 2/Main/Filter | Sustain | 345 | L2Filt Sustain | Level to sustain after decay has completed |
| Layer 2/Main/Filter | Release | 346 | L2Filt Release | Length of ramp-down after note is released |
| Layer 2/LFO | On | 347 | L2Lfo On | Enable/disable the Low Frequency Oscillator (LFO) |
| Layer 2/LFO | Legacy Shape | 348 | L2Lfo Legacy Shape | Legacy LFO shape parameter. Kept for backwards-compatibility with DAW automation |
| Layer 2/LFO | Mode | 349 | L2Lfo Mode | Oscillator phase mode. Retrigger: each voice has its own phase, Free: all voices that are playing simultaneously will have the same phase |
| Layer 2/LFO | Amount | 350 | L2Lfo Amount | Intensity of the LFO effect |
| Layer 2/LFO | Target (Legacy) | 351 | L2Lfo Target (Legacy) | Legacy LFO target parameter. Kept for backwards-compatibility with DAW automation |
| Layer 2/LFO | Time (Tempo Synced) | 352 | L2Lfo Time (Tempo Synced) | LFO rate (synced to the host) |
| Layer 2/LFO | Time (Hz) | 353 | L2Lfo Time (Hz) | LFO rate (in Hz) |
| Layer 2/LFO | Sync On | 354 | L2Lfo Sync On | Sync the LFO speed to the host |
| Layer 2/EQ | On | 355 | L2Eq On | Turn on or off the equaliser effect for this layer |
| Layer 2/EQ/Band 1 | Legacy Frequency | 356 | L2EqB1 Legacy Frequency | Legacy band 1 frequency parameter. Kept for backwards-compatibility with DAW automation |
| Layer 2/EQ/Band 1 | Legacy Resonance | 357 | L2EqB1 Legacy Resonance | Legacy resonance parameter. Kept for backwards-compatibility with DAW automation |
| Layer 2/EQ/Band 1 | Gain | 358 | L2EqB1 Gain | Band 1: volume gain at the frequency |
| Layer 2/EQ/Band 1 | Legacy Type | 359 | L2EqB1 Legacy Type | Legacy type parameter. Kept for backwards-compatibility with DAW automation |
| Layer 2/EQ/Band 2 | Legacy Frequency | 360 | L2EqB2 Legacy Frequency | Legacy band 2 frequency parameter. Kept for backwards-compatibility with DAW automation |
| Layer 2/EQ/Band 2 | Legacy Resonance | 361 | L2EqB2 Legacy Resonance | Legacy resonance parameter. Kept for backwards-compatibility with DAW automation |
| Layer 2/EQ/Band 2 | Gain | 362 | L2EqB2 Gain | Band 2: volume gain at the frequency |
| Layer 2/EQ/Band 2 | Legacy Type | 363 | L2EqB2 Legacy Type | Legacy type parameter. Kept for backwards-compatibility with DAW automation |
| Layer 2/Config | Legacy Velocity Mapping | 364 | L2 Legacy Velocity Mapping | Choose how MIDI velocity should affect the volume of this layer. There are 6 modes that can be selected for this parameter via the buttons on the GUI. By setting one layer to be quiet at high velocities and another layer to be quiet at low velocities you can create an instrument that sounds different based on how hard the notes are played. (0) Ignore velocity, always play full volume. (1) Loudest at high velocity, quietist at low velocity (2) Loudest at low velocity, quietist at high velocity (3) Loudest at high velocity, quietist at middle velocity and below (4) Loudest at middle velocity, quietist at both high and low velocities (5) Loudest at bottom velocity, quietist at middle velocity and above, |
| Layer 2/Config | Keytrack On | 365 | L2 Keytrack On | Tune the sound to match the key played; if disabled it will always play the sound at its root pitch |
| Layer 2/Config | Legacy Monophonic On | 366 | L2 Legacy Monophonic On | Only allow one voice of each sound to play at a time |
| Layer 2/Config | MIDI Transpose On | 368 | L2 MIDI Transpose On | Transpose the mapping of samples by the given semitone offset, meaning a higher/lower sample may be triggered instead of stretching/shrinking the audio by large amounts (only useful if the instrument is multi-sampled) |
| Layer 2/Playback/Loop | Loop Mode | 369 | L2Lp Loop Mode | The mode for looping the samples |
| Layer 2/Config | Key Range Low | 370 | L2 Key Range Low | The lowest key that will trigger this layer; if the key is lower than this, the layer will not play |
| Layer 2/Config | Key Range High | 371 | L2 Key Range High | The highest key that will trigger this layer; if the key is higher than this, the layer will not play |
| Layer 2/Config | Key Range Low Fade | 372 | L2 Key Range Low Fade | The length of the volume fade-in at the low end of the key range |
| Layer 2/Config | Key Range High Fade | 373 | L2 Key Range High Fade | The length of the volume fade-out at the high end of the key range |
| Layer 2/Config | Pitch Bend Range | 374 | L2 Pitch Bend Range | The pitch range in semitones of the MIDI pitch wheel |
| Layer 2/Config | Monophonic Mode | 375 | L2 Monophonic Mode | Control voice behavior when notes overlap. Off: multiple voices play simultaneously (polyphonic). Retrigger: new notes stop previous notes. Latch: first note plays until all keys are released, new notes are ignored |
| Layer 2/Playback | Play Mode | 376 | L2 Play Mode | How this layer plays its samples |
| Layer 2/Playback/Granular | Length | 377 | L2Grn Length | Duration of each grain snippet |
| Layer 2/Playback/Granular | Speed | 378 | L2Grn Speed | How fast the grain position moves through the sample |
| Layer 2/Playback/Granular | Position | 379 | L2Grn Position | Where in the sample grains are sourced from |
| Layer 2/Playback/Granular | Density | 380 | L2Grn Density | Controls how densely grains overlap, relative to the grain length. At the midpoint, grains play end-to-end. Lower values add gaps between grains for a sparse texture; higher values make grains overlap for a denser, richer sound |
| Layer 2/Playback/Granular | Spread | 381 | L2Grn Spread | Region around the playhead where grains can start from. Small values focus grains near the playhead, large values spread them across a wider area |
| Layer 2/Playback/Granular | Smooth | 382 | L2Grn Smooth | Crossfade between grains to remove clicks. Low is hard cuts, high is full overlap fade |
| Layer 2/Playback/Granular | Pan | 383 | L2Grn Pan | Randomise the stereo position of each grain. At 0% all grains play centred, at 100% grains can be panned anywhere from fully left to fully right |
| Layer 2/Playback/Granular | Detune | 384 | L2Grn Detune | Randomise the pitch of each grain. At 0% all grains play at the original pitch, at 100% grains can be detuned up to a semitone up or down |
| Layer 2/Playback/Granular | Direction | 385 | L2Grn Direction | Chance that grains spawn playing in the opposite direction to the main playhead. At 0% all grains play in the main direction, at 100% there's a 50/50 chance of each grain playing forwards or backwards |
| Layer 2/Playback/Granular | Harmony | 386 | L2Grn Harmony | Chance that grains spawn at one of the selected harmony intervals instead of the root pitch. Configure which intervals are active using the Intervals button |
| Layer 2/LFO | Legacy Shape V2 | 387 | L2Lfo Legacy Shape V2 | Legacy LFO shape parameter (v2). Kept for backwards-compatibility with DAW automation |
| Layer 2/LFO | Target | 388 | L2Lfo Target | The parameter that the LFO will modulate |
| Layer 2/Main/Filter | Resonance | 389 | L2Filt Resonance | The intensity of the volume peak at the cutoff frequency |
| Layer 2/Arp | Note Order | 390 | L2Arp Note Order | Order in which held notes are played |
| Layer 2/Arp | Trigger | 391 | L2Arp Trigger | Free: arpeggiator keeps running when new notes are pressed. Retrigger: arpeggiator restarts from step 1 |
| Layer 2/Arp | Rate | 392 | L2Arp Rate | Arpeggiator rate (synced to host tempo) |
| Layer 2/Arp | Length | 393 | L2Arp Length | Number of active steps in the arpeggiator pattern |
| Layer 2/Arp | Arpeggiator Mode | 394 | L2Arp Arpeggiator Mode | Played Notes: arpeggiates held notes. Fixed Notes: plays a recorded note sequence |
| Layer 2/Arp | Humanise | 395 | L2Arp Humanise | Add random timing variation to note starts and velocity. Higher values create looser, more human-like performance |
| Layer 2/Arp | Auto Rate | 396 | L2Arp Auto Rate | Let Floe decide the rate so that the loop plays near to its original recorded rate rather than having to add large silent gaps between slices. The rate that Floe chooses varies depending on your host tempo. |
| Layer 2/Arp | Polyrate | 397 | L2Arp Polyrate | Each octave plays at a different rate. Double means each octave up is 2x faster. 3:2 and 4:3 create polyrhythmic relationships between octaves |
| Layer 2/Arp | One Shot | 398 | L2Arp One Shot | When enabled, the arpeggiator plays through the sequence once and then stops instead of looping |
| Layer 2/EQ/Band 1 | Resonance | 399 | L2EqB1 Resonance | Band 1: sharpness of the peak |
| Layer 2/EQ/Band 2 | Resonance | 400 | L2EqB2 Resonance | Band 2: sharpness of the peak |
| Layer 2/Arp | Arpeggiator | 401 | L2Arp Arpeggiator | Enable/disable the arpeggiator |
| Layer 2/LFO | Shape | 402 | L2Lfo Shape | Oscillator shape, including random and percussive waveforms |
| Layer 2/EQ/Band 1 | Type | 404 | L2EqB1 Type | Band 1: type of EQ band |
| Layer 2/EQ/Band 2 | Type | 405 | L2EqB2 Type | Band 2: type of EQ band |
| Layer 2/EQ/Band 3 | Legacy Frequency | 406 | L2EqB3 Legacy Frequency | Legacy band 3 frequency parameter. Kept for backwards-compatibility with DAW automation |
| Layer 2/EQ/Band 3 | Resonance | 407 | L2EqB3 Resonance | Band 3: sharpness of the peak |
| Layer 2/EQ/Band 3 | Gain | 408 | L2EqB3 Gain | Band 3: volume gain at the frequency |
| Layer 2/EQ/Band 3 | Type | 409 | L2EqB3 Type | Band 3: type of EQ band |
| Layer 2/Main/Filter | Cutoff Frequency | 410 | L2Filt Cutoff Frequency | The frequency at which the filter should take effect |
| Layer 2/EQ/Band 1 | Frequency | 411 | L2EqB1 Frequency | Band 1: frequency of this band |
| Layer 2/EQ/Band 2 | Frequency | 412 | L2EqB2 Frequency | Band 2: frequency of this band |
| Layer 2/EQ/Band 3 | Frequency | 413 | L2EqB3 Frequency | Band 3: frequency of this band |
| Layer 2/Main/Filter | Type | 414 | L2Filt Type | Filter type |
| Layer 2 | Stereo Width | 415 | L2 Stereo Width | Layer stereo width: negative narrows toward mono, positive widens |
| Layer 3 | Volume | 480 | L3 Volume | Layer volume |
| Layer 3 | Mute | 481 | L3 Mute | Mute this layer |
| Layer 3 | Solo | 482 | L3 Solo | Mute all other layers |
| Layer 3 | Pan | 483 | L3 Pan | Left/right balance |
| Layer 3 | Detune Cents | 484 | L3 Detune Cents | Layer pitch in cents; hold shift for finer adjustment |
| Layer 3 | Pitch Semitones | 485 | L3 Pitch Semitones | Layer pitch in semitones |
| Layer 3/Playback/Loop | Start | 487 | L3Lp Start | Loop-start |
| Layer 3/Playback/Loop | End | 488 | L3Lp End | Loop-end |
| Layer 3/Playback/Loop | Crossfade Size | 489 | L3Lp Crossfade Size | Crossfade length; this smooths the transition from the loop-end to the loop-start |
| Layer 3/Playback/Loop | Sample Start Offset | 491 | L3Lp Sample Start Offset | Change the starting point of the sample |
| Layer 3/Playback/Loop | Reverse On | 492 | L3Lp Reverse On | Play the sound in reverse |
| Layer 3/Main/Volume Envelope | On | 493 | L3Vol On | Enable/disable the volume envelope; when disabled, each sound will play out entirely |
| Layer 3/Main/Volume Envelope | Attack | 494 | L3Vol Attack | Volume fade-in length |
| Layer 3/Main/Volume Envelope | Decay | 495 | L3Vol Decay | Volume ramp-down length (after the attack) |
| Layer 3/Main/Volume Envelope | Sustain | 496 | L3Vol Sustain | Volume level to sustain (after decay) |
| Layer 3/Main/Volume Envelope | Release | 497 | L3Vol Release | Volume fade-out length (after the note is released) |
| Layer 3/Main/Filter | On | 498 | L3Filt On | Enable/disable the filter |
| Layer 3/Main/Filter | Legacy Cutoff Frequency | 499 | L3Filt Legacy Cutoff Frequency | Legacy cutoff parameter. Kept for backwards-compatibility with DAW automation |
| Layer 3/Main/Filter | Legacy Resonance | 500 | L3Filt Legacy Resonance | Legacy resonance parameter. Kept for backwards-compatibility with DAW automation |
| Layer 3/Main/Filter | Legacy Type | 501 | L3Filt Legacy Type | Legacy filter type parameter. Kept for backwards-compatibility with DAW automation |
| Layer 3/Main/Filter | Envelope Amount | 502 | L3Filt Envelope Amount | How strongly the envelope should control the filter cutoff |
| Layer 3/Main/Filter | Attack | 503 | L3Filt Attack | Length of initial ramp-up |
| Layer 3/Main/Filter | Decay | 504 | L3Filt Decay | Length ramp-down after attack |
| Layer 3/Main/Filter | Sustain | 505 | L3Filt Sustain | Level to sustain after decay has completed |
| Layer 3/Main/Filter | Release | 506 | L3Filt Release | Length of ramp-down after note is released |
| Layer 3/LFO | On | 507 | L3Lfo On | Enable/disable the Low Frequency Oscillator (LFO) |
| Layer 3/LFO | Legacy Shape | 508 | L3Lfo Legacy Shape | Legacy LFO shape parameter. Kept for backwards-compatibility with DAW automation |
| Layer 3/LFO | Mode | 509 | L3Lfo Mode | Oscillator phase mode. Retrigger: each voice has its own phase, Free: all voices that are playing simultaneously will have the same phase |
| Layer 3/LFO | Amount | 510 | L3Lfo Amount | Intensity of the LFO effect |
| Layer 3/LFO | Target (Legacy) | 511 | L3Lfo Target (Legacy) | Legacy LFO target parameter. Kept for backwards-compatibility with DAW automation |
| Layer 3/LFO | Time (Tempo Synced) | 512 | L3Lfo Time (Tempo Synced) | LFO rate (synced to the host) |
| Layer 3/LFO | Time (Hz) | 513 | L3Lfo Time (Hz) | LFO rate (in Hz) |
| Layer 3/LFO | Sync On | 514 | L3Lfo Sync On | Sync the LFO speed to the host |
| Layer 3/EQ | On | 515 | L3Eq On | Turn on or off the equaliser effect for this layer |
| Layer 3/EQ/Band 1 | Legacy Frequency | 516 | L3EqB1 Legacy Frequency | Legacy band 1 frequency parameter. Kept for backwards-compatibility with DAW automation |
| Layer 3/EQ/Band 1 | Legacy Resonance | 517 | L3EqB1 Legacy Resonance | Legacy resonance parameter. Kept for backwards-compatibility with DAW automation |
| Layer 3/EQ/Band 1 | Gain | 518 | L3EqB1 Gain | Band 1: volume gain at the frequency |
| Layer 3/EQ/Band 1 | Legacy Type | 519 | L3EqB1 Legacy Type | Legacy type parameter. Kept for backwards-compatibility with DAW automation |
| Layer 3/EQ/Band 2 | Legacy Frequency | 520 | L3EqB2 Legacy Frequency | Legacy band 2 frequency parameter. Kept for backwards-compatibility with DAW automation |
| Layer 3/EQ/Band 2 | Legacy Resonance | 521 | L3EqB2 Legacy Resonance | Legacy resonance parameter. Kept for backwards-compatibility with DAW automation |
| Layer 3/EQ/Band 2 | Gain | 522 | L3EqB2 Gain | Band 2: volume gain at the frequency |
| Layer 3/EQ/Band 2 | Legacy Type | 523 | L3EqB2 Legacy Type | Legacy type parameter. Kept for backwards-compatibility with DAW automation |
| Layer 3/Config | Legacy Velocity Mapping | 524 | L3 Legacy Velocity Mapping | Choose how MIDI velocity should affect the volume of this layer. There are 6 modes that can be selected for this parameter via the buttons on the GUI. By setting one layer to be quiet at high velocities and another layer to be quiet at low velocities you can create an instrument that sounds different based on how hard the notes are played. (0) Ignore velocity, always play full volume. (1) Loudest at high velocity, quietist at low velocity (2) Loudest at low velocity, quietist at high velocity (3) Loudest at high velocity, quietist at middle velocity and below (4) Loudest at middle velocity, quietist at both high and low velocities (5) Loudest at bottom velocity, quietist at middle velocity and above, |
| Layer 3/Config | Keytrack On | 525 | L3 Keytrack On | Tune the sound to match the key played; if disabled it will always play the sound at its root pitch |
| Layer 3/Config | Legacy Monophonic On | 526 | L3 Legacy Monophonic On | Only allow one voice of each sound to play at a time |
| Layer 3/Config | MIDI Transpose On | 528 | L3 MIDI Transpose On | Transpose the mapping of samples by the given semitone offset, meaning a higher/lower sample may be triggered instead of stretching/shrinking the audio by large amounts (only useful if the instrument is multi-sampled) |
| Layer 3/Playback/Loop | Loop Mode | 529 | L3Lp Loop Mode | The mode for looping the samples |
| Layer 3/Config | Key Range Low | 530 | L3 Key Range Low | The lowest key that will trigger this layer; if the key is lower than this, the layer will not play |
| Layer 3/Config | Key Range High | 531 | L3 Key Range High | The highest key that will trigger this layer; if the key is higher than this, the layer will not play |
| Layer 3/Config | Key Range Low Fade | 532 | L3 Key Range Low Fade | The length of the volume fade-in at the low end of the key range |
| Layer 3/Config | Key Range High Fade | 533 | L3 Key Range High Fade | The length of the volume fade-out at the high end of the key range |
| Layer 3/Config | Pitch Bend Range | 534 | L3 Pitch Bend Range | The pitch range in semitones of the MIDI pitch wheel |
| Layer 3/Config | Monophonic Mode | 535 | L3 Monophonic Mode | Control voice behavior when notes overlap. Off: multiple voices play simultaneously (polyphonic). Retrigger: new notes stop previous notes. Latch: first note plays until all keys are released, new notes are ignored |
| Layer 3/Playback | Play Mode | 536 | L3 Play Mode | How this layer plays its samples |
| Layer 3/Playback/Granular | Length | 537 | L3Grn Length | Duration of each grain snippet |
| Layer 3/Playback/Granular | Speed | 538 | L3Grn Speed | How fast the grain position moves through the sample |
| Layer 3/Playback/Granular | Position | 539 | L3Grn Position | Where in the sample grains are sourced from |
| Layer 3/Playback/Granular | Density | 540 | L3Grn Density | Controls how densely grains overlap, relative to the grain length. At the midpoint, grains play end-to-end. Lower values add gaps between grains for a sparse texture; higher values make grains overlap for a denser, richer sound |
| Layer 3/Playback/Granular | Spread | 541 | L3Grn Spread | Region around the playhead where grains can start from. Small values focus grains near the playhead, large values spread them across a wider area |
| Layer 3/Playback/Granular | Smooth | 542 | L3Grn Smooth | Crossfade between grains to remove clicks. Low is hard cuts, high is full overlap fade |
| Layer 3/Playback/Granular | Pan | 543 | L3Grn Pan | Randomise the stereo position of each grain. At 0% all grains play centred, at 100% grains can be panned anywhere from fully left to fully right |
| Layer 3/Playback/Granular | Detune | 544 | L3Grn Detune | Randomise the pitch of each grain. At 0% all grains play at the original pitch, at 100% grains can be detuned up to a semitone up or down |
| Layer 3/Playback/Granular | Direction | 545 | L3Grn Direction | Chance that grains spawn playing in the opposite direction to the main playhead. At 0% all grains play in the main direction, at 100% there's a 50/50 chance of each grain playing forwards or backwards |
| Layer 3/Playback/Granular | Harmony | 546 | L3Grn Harmony | Chance that grains spawn at one of the selected harmony intervals instead of the root pitch. Configure which intervals are active using the Intervals button |
| Layer 3/LFO | Legacy Shape V2 | 547 | L3Lfo Legacy Shape V2 | Legacy LFO shape parameter (v2). Kept for backwards-compatibility with DAW automation |
| Layer 3/LFO | Target | 548 | L3Lfo Target | The parameter that the LFO will modulate |
| Layer 3/Main/Filter | Resonance | 549 | L3Filt Resonance | The intensity of the volume peak at the cutoff frequency |
| Layer 3/Arp | Note Order | 550 | L3Arp Note Order | Order in which held notes are played |
| Layer 3/Arp | Trigger | 551 | L3Arp Trigger | Free: arpeggiator keeps running when new notes are pressed. Retrigger: arpeggiator restarts from step 1 |
| Layer 3/Arp | Rate | 552 | L3Arp Rate | Arpeggiator rate (synced to host tempo) |
| Layer 3/Arp | Length | 553 | L3Arp Length | Number of active steps in the arpeggiator pattern |
| Layer 3/Arp | Arpeggiator Mode | 554 | L3Arp Arpeggiator Mode | Played Notes: arpeggiates held notes. Fixed Notes: plays a recorded note sequence |
| Layer 3/Arp | Humanise | 555 | L3Arp Humanise | Add random timing variation to note starts and velocity. Higher values create looser, more human-like performance |
| Layer 3/Arp | Auto Rate | 556 | L3Arp Auto Rate | Let Floe decide the rate so that the loop plays near to its original recorded rate rather than having to add large silent gaps between slices. The rate that Floe chooses varies depending on your host tempo. |
| Layer 3/Arp | Polyrate | 557 | L3Arp Polyrate | Each octave plays at a different rate. Double means each octave up is 2x faster. 3:2 and 4:3 create polyrhythmic relationships between octaves |
| Layer 3/Arp | One Shot | 558 | L3Arp One Shot | When enabled, the arpeggiator plays through the sequence once and then stops instead of looping |
| Layer 3/EQ/Band 1 | Resonance | 559 | L3EqB1 Resonance | Band 1: sharpness of the peak |
| Layer 3/EQ/Band 2 | Resonance | 560 | L3EqB2 Resonance | Band 2: sharpness of the peak |
| Layer 3/Arp | Arpeggiator | 561 | L3Arp Arpeggiator | Enable/disable the arpeggiator |
| Layer 3/LFO | Shape | 562 | L3Lfo Shape | Oscillator shape, including random and percussive waveforms |
| Layer 3/EQ/Band 1 | Type | 564 | L3EqB1 Type | Band 1: type of EQ band |
| Layer 3/EQ/Band 2 | Type | 565 | L3EqB2 Type | Band 2: type of EQ band |
| Layer 3/EQ/Band 3 | Legacy Frequency | 566 | L3EqB3 Legacy Frequency | Legacy band 3 frequency parameter. Kept for backwards-compatibility with DAW automation |
| Layer 3/EQ/Band 3 | Resonance | 567 | L3EqB3 Resonance | Band 3: sharpness of the peak |
| Layer 3/EQ/Band 3 | Gain | 568 | L3EqB3 Gain | Band 3: volume gain at the frequency |
| Layer 3/EQ/Band 3 | Type | 569 | L3EqB3 Type | Band 3: type of EQ band |
| Layer 3/Main/Filter | Cutoff Frequency | 570 | L3Filt Cutoff Frequency | The frequency at which the filter should take effect |
| Layer 3/EQ/Band 1 | Frequency | 571 | L3EqB1 Frequency | Band 1: frequency of this band |
| Layer 3/EQ/Band 2 | Frequency | 572 | L3EqB2 Frequency | Band 2: frequency of this band |
| Layer 3/EQ/Band 3 | Frequency | 573 | L3EqB3 Frequency | Band 3: frequency of this band |
| Layer 3/Main/Filter | Type | 574 | L3Filt Type | Filter type |
| Layer 3 | Stereo Width | 575 | L3 Stereo Width | Layer stereo width: negative narrows toward mono, positive widens |
| Effect/Distortion | Type | 3 | Dist Type | Distortion algorithm type |
| Effect/Distortion | Drive | 4 | Dist Drive | Distortion amount |
| Effect/Distortion | On | 5 | Dist On | Enable/disable the distortion effect |
| Effect/Bitcrush | Bits | 6 | Bitc Bits | Audio resolution |
| Effect/Bitcrush | Sample Rate | 7 | Bitc Sample Rate | Sample rate |
| Effect/Bitcrush | Legacy Wet | 8 | Bitc Legacy Wet | Legacy processed-signal volume. Kept for backwards-compatibility with DAW automation |
| Effect/Bitcrush | Legacy Dry | 9 | Bitc Legacy Dry | Legacy unprocessed-signal volume. Kept for backwards-compatibility with DAW automation |
| Effect/Bitcrush | On | 10 | Bitc On | Enable/disable the bitcrush effect |
| Effect/Compressor | Legacy Threshold | 11 | Comp Legacy Threshold | Legacy threshold parameter. Kept for backwards-compatibility with DAW automation |
| Effect/Compressor | Legacy Ratio | 12 | Comp Legacy Ratio | Legacy ratio parameter. Kept for backwards-compatibility with DAW automation |
| Effect/Compressor | Gain | 13 | Comp Gain | Additional control for volume after compression |
| Effect/Compressor | Auto Gain | 14 | Comp Auto Gain | Automatically re-adjust the gain to stay consistent regardless of compression intensity |
| Effect/Compressor | On | 15 | Comp On | Enable/disable the compression effect |
| Effect/Filter | On | 16 | Filt On | Enable/disable the filter |
| Effect/Filter | Legacy Cutoff Frequency | 17 | Filt Legacy Cutoff Frequency | Legacy cutoff parameter. Kept for backwards-compatibility with DAW automation |
| Effect/Filter | Legacy Resonance | 18 | Filt Legacy Resonance | Legacy resonance parameter. Kept for backwards-compatibility with DAW automation |
| Effect/Filter | Legacy Gain | 19 | Filt Legacy Gain | Legacy gain parameter. Kept for backwards-compatibility with DAW automation |
| Effect/Filter | Legacy Type | 20 | Filt Legacy Type | Legacy type parameter. Kept for backwards-compatibility with DAW automation |
| Effect/StereoWiden | Width | 21 | Ster Width | Increase or decrease the stereo width |
| Effect/StereoWiden | On | 22 | Ster On | Turn the stereo widen effect on or off |
| Effect/Chorus | Rate | 23 | Chr Rate | Chorus modulation rate |
| Effect/Chorus | Legacy High-pass | 24 | Chr Legacy High-pass | Legacy high-pass parameter. Kept for backwards-compatibility with DAW automation |
| Effect/Chorus | Depth | 25 | Chr Depth | Chorus effect intensity |
| Effect/Chorus | Legacy Wet | 26 | Chr Legacy Wet | Legacy processed-signal volume. Kept for backwards-compatibility with DAW automation |
| Effect/Chorus | Legacy Dry | 27 | Chr Legacy Dry | Legacy unprocessed-signal volume. Kept for backwards-compatibility with DAW automation |
| Effect/Chorus | On | 28 | Chr On | Enable/disable the chorus effect |
| Effect/Filter | Resonance | 29 | Filt Resonance | The intensity of the volume peak at the cutoff frequency |
| Effect/Filter | Gain | 30 | Filt Gain | Volume gain of shelf/peak filter |
| Effect/StereoWiden | Mode | 31 | Ster Mode | Stereo widening algorithm: Balanced (constant-power M/S), Legacy (original behaviour, kept for old presets), or Bass Mono (mono below the crossover, widened above) |
| Effect/StereoWiden | Bass Mono | 32 | Ster Bass Mono | Frequencies below this point are summed to mono (Bass Mono mode only) |
| Effect/Compressor | Type | 33 | Comp Type | The compressor algorithm to use |
| Effect/Compressor | Attack | 34 | Comp Attack | How quickly the compressor responds to a rise in level |
| Effect/Compressor | Release | 35 | Comp Release | How quickly the compressor recovers after the level drops |
| Effect/Compressor | Mix | 36 | Comp Mix | Blend between the dry input and the compressed signal |
| Effect/Convolution Reverb | Legacy High-pass | 65 | Conv Legacy High-pass | Legacy high-pass parameter. Kept for backwards-compatibility with DAW automation |
| Effect/Convolution Reverb | Legacy Wet | 66 | Conv Legacy Wet | Legacy processed-signal volume. Kept for backwards-compatibility with DAW automation |
| Effect/Convolution Reverb | Legacy Dry | 67 | Conv Legacy Dry | Legacy unprocessed-signal volume. Kept for backwards-compatibility with DAW automation |
| Effect/Convolution Reverb | On | 68 | Conv On | Enable/disable the convolution reverb effect |
| Effect/Reverb | Decay Time | 69 | Rvrb Decay Time | Reverb decay time |
| Effect/Reverb | Pre Low Cutoff | 70 | Rvrb Pre Low Cutoff | Low-pass filter cutoff before reverb |
| Effect/Reverb | Pre High Cutoff | 71 | Rvrb Pre High Cutoff | High-pass filter cutoff before reverb |
| Effect/Reverb | Low Cutoff | 72 | Rvrb Low Cutoff | Low-pass filter cutoff after reverb |
| Effect/Reverb | Low Gain | 73 | Rvrb Low Gain | Low-pass filter gain |
| Effect/Reverb | High Cutoff | 74 | Rvrb High Cutoff | High-pass filter cutoff after reverb |
| Effect/Reverb | High Gain | 75 | Rvrb High Gain | High-pass filter gain |
| Effect/Reverb | Chorus Amount | 76 | Rvrb Chorus Amount | Chorus effect amount |
| Effect/Reverb | Chorus Frequency | 77 | Rvrb Chorus Frequency | Chorus effect frequency |
| Effect/Reverb | Size | 78 | Rvrb Size | Reverb size |
| Effect/Reverb | Delay | 79 | Rvrb Delay | Reverb delay |
| Effect/Reverb | Mix | 80 | Rvrb Mix | Processed signal volume |
| Effect/Reverb | On | 81 | Rvrb On | Enable/disable the reverb effect |
| Effect/Phaser | Feedback | 82 | Phs Feedback | Feedback amount |
| Effect/Phaser | Mod Rate | 83 | Phs Mod Rate | Speed at which the phaser filters modulate |
| Effect/Phaser | Center Frequency | 84 | Phs Center Frequency | Center frequency of the phaser filters |
| Effect/Phaser | Shape | 85 | Phs Shape | Shape of the phaser filter's peaks |
| Effect/Phaser | Mod Depth | 86 | Phs Mod Depth | The range over which the phaser filters modulate |
| Effect/Phaser | Stereo Amount | 87 | Phs Stereo Amount | Adds a stereo effect by offsetting the left and right filters |
| Effect/Phaser | Mix | 88 | Phs Mix | Mix between the wet and dry signals |
| Effect/Phaser | On | 89 | Phs On | Enable/disable the phaser effect |
| Effect/Delay | Mode | 90 | Dly Mode | Delay type |
| Effect/Delay | Filter Cutoff | 91 | Dly Filter Cutoff | High/low frequency reduction |
| Effect/Delay | Filter Spread | 92 | Dly Filter Spread | Width of the filter |
| Effect/Delay | Time Left (ms) | 93 | Dly Time Left (ms) | Left delay time (in milliseconds) |
| Effect/Delay | Legacy Time Right (ms) | 94 | Dly Legacy Time Right (ms) | Right delay time (in milliseconds) |
| Effect/Delay | Time Left (Tempo Synced) | 95 | Dly Time Left (Tempo Synced) | Left delay time (synced to the host tempo) |
| Effect/Delay | Time Right (Tempo Synced) | 96 | Dly Time Right (Tempo Synced) | Right delay time (synced to the host tempo) |
| Effect/Delay | On | 97 | Dly On | Synchronise timings to the host's BPM |
| Effect/Delay | Mix | 98 | Dly Mix | Level of processed signal |
| Effect/Delay | On | 99 | Dly On | Enable/disable the delay effect |
| Effect/Delay | Feedback | 100 | Dly Feedback | How much the signal repeats |
| Effect/Filter | Type | 105 | Filt Type | Filter type |
| Effect/Filter | Cutoff Frequency | 106 | Filt Cutoff Frequency | Frequency of filter effect |
| Effect/Chorus | High-pass | 107 | Chr High-pass | High-pass filter cutoff |
| Effect/Convolution Reverb | High-pass | 108 | Conv High-pass | Wet high-pass filter cutoff |
| Effect/Bitcrush | Mix | 109 | Bitc Mix | Blend between the dry input and the bitcrushed signal |
| Effect/Bitcrush | Output | 110 | Bitc Output | Output level after the mix |
| Effect/Chorus | Mix | 111 | Chr Mix | Blend between the dry input and the chorused signal |
| Effect/Chorus | Output | 112 | Chr Output | Output level after the mix |
| Effect/Convolution Reverb | Mix | 113 | Conv Mix | Blend between the dry input and the convolution reverb signal |
| Effect/Convolution Reverb | Output | 114 | Conv Output | Output level after the mix |
| Effect/Distortion | Mix | 115 | Dist Mix | Blend between the dry input and the distorted signal |
| Effect/StereoWiden | Mix | 116 | Ster Mix | Blend between the dry input and the stereo-widened signal |
| Effect/Filter | Mix | 117 | Filt Mix | Blend between the dry input and the filtered signal |
| Effect/Compressor | Threshold | 118 | Comp Threshold | The threshold that the audio has to pass above before the compression should start taking place |
| Effect/Compressor | Ratio | 119 | Comp Ratio | The intensity of compression (high ratios mean more compression) |
| Effect/EQ | On | 120 | Eq On | Enable/disable the equaliser effect |
| Effect/EQ | Mix | 121 | Eq Mix | Mix between the wet and dry signals |
| Effect/EQ/Band 1 | Type | 122 | EqB1 Type | Band 1: type of EQ band |
| Effect/EQ/Band 1 | Frequency | 123 | EqB1 Frequency | Band 1: frequency of this band |
| Effect/EQ/Band 1 | Resonance | 124 | EqB1 Resonance | Band 1: sharpness of the peak |
| Effect/EQ/Band 1 | Gain | 125 | EqB1 Gain | Band 1: volume gain at the frequency |
| Effect/EQ/Band 2 | Type | 126 | EqB2 Type | Band 2: type of EQ band |
| Effect/EQ/Band 2 | Frequency | 127 | EqB2 Frequency | Band 2: frequency of this band |
| Effect/EQ/Band 2 | Resonance | 128 | EqB2 Resonance | Band 2: sharpness of the peak |
| Effect/EQ/Band 2 | Gain | 129 | EqB2 Gain | Band 2: volume gain at the frequency |
| Effect/EQ/Band 3 | Type | 130 | EqB3 Type | Band 3: type of EQ band |
| Effect/EQ/Band 3 | Frequency | 131 | EqB3 Frequency | Band 3: frequency of this band |
| Effect/EQ/Band 3 | Resonance | 132 | EqB3 Resonance | Band 3: sharpness of the peak |
| Effect/EQ/Band 3 | Gain | 133 | EqB3 Gain | Band 3: volume gain at the frequency |
| Master | Volume | 0 | Mst Volume | Master volume |
| Master | Legacy Velocity To Volume Strength | 1 | Mst Legacy Velocity To Volume Strength | Legacy parameter. The amount that the MIDI velocity affects the volume of notes; 100% means notes will be silent when the velocity is very soft, and 0% means that notes will play full volume regardless of the velocity |
| Master | Timbre | 2 | Mst Timbre | The intstruments timbre. Not every instrument contains timbre information; instruments that do will be highlighted when you click on this knob. |
| Macro | 1 | 101 | 1 | A macro that can be assigned to any parameter in the plugin. The macro will affect all parameters that are assigned to it. |
| Macro | 2 | 102 | 2 | A macro that can be assigned to any parameter in the plugin. The macro will affect all parameters that are assigned to it. |
| Macro | 3 | 103 | 3 | A macro that can be assigned to any parameter in the plugin. The macro will affect all parameters that are assigned to it. |
| Macro | 4 | 104 | 4 | A macro that can be assigned to any parameter in the plugin. The macro will affect all parameters that are assigned to it. |
