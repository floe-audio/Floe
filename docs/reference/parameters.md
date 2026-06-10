# Parameters

> A list of all parameters in Floe

For reference purposes, this page lists all of Floe's parameters in case you need to look up specific IDs or details.

For more of a usage guide, consult the documentation pages on layers, effects, looping, etc.

| Module | Name | ID | Description |
| --- | --- | --- | --- |
| Layer 1 | Volume | 160 | Layer volume |
| Layer 1 | Mute | 161 | Mute this layer |
| Layer 1 | Solo | 162 | Mute all other layers |
| Layer 1 | Pan | 163 | Left/right balance |
| Layer 1 | Detune Cents | 164 | Layer pitch in cents; hold shift for finer adjustment |
| Layer 1 | Pitch Semitones | 165 | Layer pitch in semitones |
| Layer 1/Playback/Loop | Start | 167 | Loop-start |
| Layer 1/Playback/Loop | End | 168 | Loop-end |
| Layer 1/Playback/Loop | Crossfade Size | 169 | Crossfade length; this smooths the transition from the loop-end to the loop-start |
| Layer 1/Playback/Loop | Sample Start Offset | 171 | Change the starting point of the sample |
| Layer 1/Playback/Loop | Reverse On | 172 | Play the sound in reverse |
| Layer 1/Main/Volume Envelope | On | 173 | Enable/disable the volume envelope; when disabled, each sound will play out entirely |
| Layer 1/Main/Volume Envelope | Attack | 174 | Volume fade-in length |
| Layer 1/Main/Volume Envelope | Decay | 175 | Volume ramp-down length (after the attack) |
| Layer 1/Main/Volume Envelope | Sustain | 176 | Volume level to sustain (after decay) |
| Layer 1/Main/Volume Envelope | Release | 177 | Volume fade-out length (after the note is released) |
| Layer 1/Main/Filter | On | 178 | Enable/disable the filter |
| Layer 1/Main/Filter | Legacy Cutoff Frequency | 179 | Legacy cutoff parameter. Kept for backwards-compatibility with DAW automation |
| Layer 1/Main/Filter | Legacy Resonance | 180 | Legacy resonance parameter. Kept for backwards-compatibility with DAW automation |
| Layer 1/Main/Filter | Legacy Type | 181 | Legacy filter type parameter. Kept for backwards-compatibility with DAW automation |
| Layer 1/Main/Filter | Envelope Amount | 182 | How strongly the envelope should control the filter cutoff |
| Layer 1/Main/Filter | Attack | 183 | Length of initial ramp-up |
| Layer 1/Main/Filter | Decay | 184 | Length ramp-down after attack |
| Layer 1/Main/Filter | Sustain | 185 | Level to sustain after decay has completed |
| Layer 1/Main/Filter | Release | 186 | Length of ramp-down after note is released |
| Layer 1/LFO | On | 187 | Enable/disable the Low Frequency Oscillator (LFO) |
| Layer 1/LFO | Legacy Shape | 188 | Legacy LFO shape parameter. Kept for backwards-compatibility with DAW automation |
| Layer 1/LFO | Mode | 189 | Oscillator phase mode. Retrigger: each voice has its own phase, Free: all voices that are playing simultaneously will have the same phase |
| Layer 1/LFO | Amount | 190 | Intensity of the LFO effect |
| Layer 1/LFO | Target (Legacy) | 191 | Legacy LFO target parameter. Kept for backwards-compatibility with DAW automation |
| Layer 1/LFO | Time (Tempo Synced) | 192 | LFO rate (synced to the host) |
| Layer 1/LFO | Time (Hz) | 193 | LFO rate (in Hz) |
| Layer 1/LFO | Sync On | 194 | Sync the LFO speed to the host |
| Layer 1/EQ | On | 195 | Turn on or off the equaliser effect for this layer |
| Layer 1/EQ/Band 1 | Legacy Frequency | 196 | Legacy band 1 frequency parameter. Kept for backwards-compatibility with DAW automation |
| Layer 1/EQ/Band 1 | Legacy Resonance | 197 | Legacy resonance parameter. Kept for backwards-compatibility with DAW automation |
| Layer 1/EQ/Band 1 | Gain | 198 | Band 1: volume gain at the frequency |
| Layer 1/EQ/Band 1 | Legacy Type | 199 | Legacy type parameter. Kept for backwards-compatibility with DAW automation |
| Layer 1/EQ/Band 2 | Legacy Frequency | 200 | Legacy band 2 frequency parameter. Kept for backwards-compatibility with DAW automation |
| Layer 1/EQ/Band 2 | Legacy Resonance | 201 | Legacy resonance parameter. Kept for backwards-compatibility with DAW automation |
| Layer 1/EQ/Band 2 | Gain | 202 | Band 2: volume gain at the frequency |
| Layer 1/EQ/Band 2 | Legacy Type | 203 | Legacy type parameter. Kept for backwards-compatibility with DAW automation |
| Layer 1/Config | Legacy Velocity Mapping | 204 | Choose how MIDI velocity should affect the volume of this layer. There are 6 modes that can be selected for this parameter via the buttons on the GUI. By setting one layer to be quiet at high velocities and another layer to be quiet at low velocities you can create an instrument that sounds different based on how hard the notes are played. (0) Ignore velocity, always play full volume. (1) Loudest at high velocity, quietist at low velocity (2) Loudest at low velocity, quietist at high velocity (3) Loudest at high velocity, quietist at middle velocity and below (4) Loudest at middle velocity, quietist at both high and low velocities (5) Loudest at bottom velocity, quietist at middle velocity and above, |
| Layer 1/Config | Keytrack On | 205 | Tune the sound to match the key played; if disabled it will always play the sound at its root pitch |
| Layer 1/Config | Legacy Monophonic On | 206 | Only allow one voice of each sound to play at a time |
| Layer 1/Config | MIDI Transpose On | 208 | Transpose the mapping of samples by the given semitone offset, meaning a higher/lower sample may be triggered instead of stretching/shrinking the audio by large amounts (only useful if the instrument is multi-sampled) |
| Layer 1/Playback/Loop | Loop Mode | 209 | The mode for looping the samples |
| Layer 1/Config | Key Range Low | 210 | The lowest key that will trigger this layer; if the key is lower than this, the layer will not play |
| Layer 1/Config | Key Range High | 211 | The highest key that will trigger this layer; if the key is higher than this, the layer will not play |
| Layer 1/Config | Key Range Low Fade | 212 | The length of the volume fade-in at the low end of the key range |
| Layer 1/Config | Key Range High Fade | 213 | The length of the volume fade-out at the high end of the key range |
| Layer 1/Config | Pitch Bend Range | 214 | The pitch range in semitones of the MIDI pitch wheel |
| Layer 1/Config | Monophonic Mode | 215 | Control voice behavior when notes overlap. Off: multiple voices play simultaneously (polyphonic). Retrigger: new notes stop previous notes. Latch: first note plays until all keys are released, new notes are ignored |
| Layer 1/Playback | Play Mode | 216 | How this layer plays its samples |
| Layer 1/Playback/Granular | Granular Length | 217 | Duration of each grain snippet |
| Layer 1/Playback/Granular | Granular Speed | 218 | How fast the grain position moves through the sample |
| Layer 1/Playback/Granular | Granular Position | 219 | Where in the sample grains are sourced from |
| Layer 1/Playback/Granular | Granular Density | 220 | Controls how densely grains overlap, relative to the grain length. At the midpoint, grains play end-to-end. Lower values add gaps between grains for a sparse texture; higher values make grains overlap for a denser, richer sound |
| Layer 1/Playback/Granular | Granular Spread | 221 | Region around the playhead where grains can start from. Small values focus grains near the playhead, large values spread them across a wider area |
| Layer 1/Playback/Granular | Granular Smoothing | 222 | Crossfade between grains to remove clicks. Low is hard cuts, high is full overlap fade |
| Layer 1/Playback/Granular | Granular Random Pan | 223 | Randomise the stereo position of each grain. At 0% all grains play centred, at 100% grains can be panned anywhere from fully left to fully right |
| Layer 1/Playback/Granular | Granular Random Detune | 224 | Randomise the pitch of each grain. At 0% all grains play at the original pitch, at 100% grains can be detuned up to a semitone up or down |
| Layer 1/Playback/Granular | Granular Random Direction | 225 | Chance that grains spawn playing in the opposite direction to the main playhead. At 0% all grains play in the main direction, at 100% there's a 50/50 chance of each grain playing forwards or backwards |
| Layer 1/Playback/Granular | Granular Harmony | 226 | Chance that grains spawn at one of the selected harmony intervals instead of the root pitch. Configure which intervals are active using the Intervals button |
| Layer 1/LFO | Legacy Shape V2 | 227 | Legacy LFO shape parameter (v2). Kept for backwards-compatibility with DAW automation |
| Layer 1/LFO | Target | 228 | The parameter that the LFO will modulate |
| Layer 1/Main/Filter | Resonance | 229 | The intensity of the volume peak at the cutoff frequency |
| Layer 1/Arp | Note Order | 230 | Order in which held notes are played |
| Layer 1/Arp | Trigger | 231 | Free: arpeggiator keeps running when new notes are pressed. Retrigger: arpeggiator restarts from step 1 |
| Layer 1/Arp | Rate | 232 | Arpeggiator rate (synced to host tempo) |
| Layer 1/Arp | Length | 233 | Number of active steps in the arpeggiator pattern |
| Layer 1/Arp | Arpeggiator Mode | 234 | Played Notes: arpeggiates held notes. Fixed Notes: plays a recorded note sequence |
| Layer 1/Arp | Humanise | 235 | Add random timing variation to note starts and velocity. Higher values create looser, more human-like performance |
| Layer 1/Arp | Auto Rate | 236 | Automatically pick an arpeggiator rate based on the sliced instrument's loop length and host tempo. |
| Layer 1/Arp | Oct Polyrate | 237 | Each octave plays at a different rate. Double means each octave up is 2x faster. 3:2 and 4:3 create polyrhythmic relationships between octaves |
| Layer 1/Arp | One Shot | 238 | When enabled, the arpeggiator plays through the sequence once and then stops instead of looping |
| Layer 1/EQ/Band 1 | Resonance | 239 | Band 1: sharpness of the peak |
| Layer 1/EQ/Band 2 | Resonance | 240 | Band 2: sharpness of the peak |
| Layer 1/Arp | Arpeggiator | 241 | Enable/disable the arpeggiator |
| Layer 1/LFO | Shape | 242 | Oscillator shape, including random and percussive waveforms |
| Layer 1/EQ/Band 1 | Type | 244 | Band 1: type of EQ band |
| Layer 1/EQ/Band 2 | Type | 245 | Band 2: type of EQ band |
| Layer 1/EQ/Band 3 | Legacy Frequency | 246 | Legacy band 3 frequency parameter. Kept for backwards-compatibility with DAW automation |
| Layer 1/EQ/Band 3 | Resonance | 247 | Band 3: sharpness of the peak |
| Layer 1/EQ/Band 3 | Gain | 248 | Band 3: volume gain at the frequency |
| Layer 1/EQ/Band 3 | Type | 249 | Band 3: type of EQ band |
| Layer 1/Main/Filter | Cutoff Frequency | 250 | The frequency at which the filter should take effect |
| Layer 1/EQ/Band 1 | Frequency | 251 | Band 1: frequency of this band |
| Layer 1/EQ/Band 2 | Frequency | 252 | Band 2: frequency of this band |
| Layer 1/EQ/Band 3 | Frequency | 253 | Band 3: frequency of this band |
| Layer 1/Main/Filter | Type | 254 | Filter type |
| Layer 1 | Stereo Width | 255 | Layer stereo width: negative narrows toward mono, positive widens |
| Layer 2 | Volume | 320 | Layer volume |
| Layer 2 | Mute | 321 | Mute this layer |
| Layer 2 | Solo | 322 | Mute all other layers |
| Layer 2 | Pan | 323 | Left/right balance |
| Layer 2 | Detune Cents | 324 | Layer pitch in cents; hold shift for finer adjustment |
| Layer 2 | Pitch Semitones | 325 | Layer pitch in semitones |
| Layer 2/Playback/Loop | Start | 327 | Loop-start |
| Layer 2/Playback/Loop | End | 328 | Loop-end |
| Layer 2/Playback/Loop | Crossfade Size | 329 | Crossfade length; this smooths the transition from the loop-end to the loop-start |
| Layer 2/Playback/Loop | Sample Start Offset | 331 | Change the starting point of the sample |
| Layer 2/Playback/Loop | Reverse On | 332 | Play the sound in reverse |
| Layer 2/Main/Volume Envelope | On | 333 | Enable/disable the volume envelope; when disabled, each sound will play out entirely |
| Layer 2/Main/Volume Envelope | Attack | 334 | Volume fade-in length |
| Layer 2/Main/Volume Envelope | Decay | 335 | Volume ramp-down length (after the attack) |
| Layer 2/Main/Volume Envelope | Sustain | 336 | Volume level to sustain (after decay) |
| Layer 2/Main/Volume Envelope | Release | 337 | Volume fade-out length (after the note is released) |
| Layer 2/Main/Filter | On | 338 | Enable/disable the filter |
| Layer 2/Main/Filter | Legacy Cutoff Frequency | 339 | Legacy cutoff parameter. Kept for backwards-compatibility with DAW automation |
| Layer 2/Main/Filter | Legacy Resonance | 340 | Legacy resonance parameter. Kept for backwards-compatibility with DAW automation |
| Layer 2/Main/Filter | Legacy Type | 341 | Legacy filter type parameter. Kept for backwards-compatibility with DAW automation |
| Layer 2/Main/Filter | Envelope Amount | 342 | How strongly the envelope should control the filter cutoff |
| Layer 2/Main/Filter | Attack | 343 | Length of initial ramp-up |
| Layer 2/Main/Filter | Decay | 344 | Length ramp-down after attack |
| Layer 2/Main/Filter | Sustain | 345 | Level to sustain after decay has completed |
| Layer 2/Main/Filter | Release | 346 | Length of ramp-down after note is released |
| Layer 2/LFO | On | 347 | Enable/disable the Low Frequency Oscillator (LFO) |
| Layer 2/LFO | Legacy Shape | 348 | Legacy LFO shape parameter. Kept for backwards-compatibility with DAW automation |
| Layer 2/LFO | Mode | 349 | Oscillator phase mode. Retrigger: each voice has its own phase, Free: all voices that are playing simultaneously will have the same phase |
| Layer 2/LFO | Amount | 350 | Intensity of the LFO effect |
| Layer 2/LFO | Target (Legacy) | 351 | Legacy LFO target parameter. Kept for backwards-compatibility with DAW automation |
| Layer 2/LFO | Time (Tempo Synced) | 352 | LFO rate (synced to the host) |
| Layer 2/LFO | Time (Hz) | 353 | LFO rate (in Hz) |
| Layer 2/LFO | Sync On | 354 | Sync the LFO speed to the host |
| Layer 2/EQ | On | 355 | Turn on or off the equaliser effect for this layer |
| Layer 2/EQ/Band 1 | Legacy Frequency | 356 | Legacy band 1 frequency parameter. Kept for backwards-compatibility with DAW automation |
| Layer 2/EQ/Band 1 | Legacy Resonance | 357 | Legacy resonance parameter. Kept for backwards-compatibility with DAW automation |
| Layer 2/EQ/Band 1 | Gain | 358 | Band 1: volume gain at the frequency |
| Layer 2/EQ/Band 1 | Legacy Type | 359 | Legacy type parameter. Kept for backwards-compatibility with DAW automation |
| Layer 2/EQ/Band 2 | Legacy Frequency | 360 | Legacy band 2 frequency parameter. Kept for backwards-compatibility with DAW automation |
| Layer 2/EQ/Band 2 | Legacy Resonance | 361 | Legacy resonance parameter. Kept for backwards-compatibility with DAW automation |
| Layer 2/EQ/Band 2 | Gain | 362 | Band 2: volume gain at the frequency |
| Layer 2/EQ/Band 2 | Legacy Type | 363 | Legacy type parameter. Kept for backwards-compatibility with DAW automation |
| Layer 2/Config | Legacy Velocity Mapping | 364 | Choose how MIDI velocity should affect the volume of this layer. There are 6 modes that can be selected for this parameter via the buttons on the GUI. By setting one layer to be quiet at high velocities and another layer to be quiet at low velocities you can create an instrument that sounds different based on how hard the notes are played. (0) Ignore velocity, always play full volume. (1) Loudest at high velocity, quietist at low velocity (2) Loudest at low velocity, quietist at high velocity (3) Loudest at high velocity, quietist at middle velocity and below (4) Loudest at middle velocity, quietist at both high and low velocities (5) Loudest at bottom velocity, quietist at middle velocity and above, |
| Layer 2/Config | Keytrack On | 365 | Tune the sound to match the key played; if disabled it will always play the sound at its root pitch |
| Layer 2/Config | Legacy Monophonic On | 366 | Only allow one voice of each sound to play at a time |
| Layer 2/Config | MIDI Transpose On | 368 | Transpose the mapping of samples by the given semitone offset, meaning a higher/lower sample may be triggered instead of stretching/shrinking the audio by large amounts (only useful if the instrument is multi-sampled) |
| Layer 2/Playback/Loop | Loop Mode | 369 | The mode for looping the samples |
| Layer 2/Config | Key Range Low | 370 | The lowest key that will trigger this layer; if the key is lower than this, the layer will not play |
| Layer 2/Config | Key Range High | 371 | The highest key that will trigger this layer; if the key is higher than this, the layer will not play |
| Layer 2/Config | Key Range Low Fade | 372 | The length of the volume fade-in at the low end of the key range |
| Layer 2/Config | Key Range High Fade | 373 | The length of the volume fade-out at the high end of the key range |
| Layer 2/Config | Pitch Bend Range | 374 | The pitch range in semitones of the MIDI pitch wheel |
| Layer 2/Config | Monophonic Mode | 375 | Control voice behavior when notes overlap. Off: multiple voices play simultaneously (polyphonic). Retrigger: new notes stop previous notes. Latch: first note plays until all keys are released, new notes are ignored |
| Layer 2/Playback | Play Mode | 376 | How this layer plays its samples |
| Layer 2/Playback/Granular | Granular Length | 377 | Duration of each grain snippet |
| Layer 2/Playback/Granular | Granular Speed | 378 | How fast the grain position moves through the sample |
| Layer 2/Playback/Granular | Granular Position | 379 | Where in the sample grains are sourced from |
| Layer 2/Playback/Granular | Granular Density | 380 | Controls how densely grains overlap, relative to the grain length. At the midpoint, grains play end-to-end. Lower values add gaps between grains for a sparse texture; higher values make grains overlap for a denser, richer sound |
| Layer 2/Playback/Granular | Granular Spread | 381 | Region around the playhead where grains can start from. Small values focus grains near the playhead, large values spread them across a wider area |
| Layer 2/Playback/Granular | Granular Smoothing | 382 | Crossfade between grains to remove clicks. Low is hard cuts, high is full overlap fade |
| Layer 2/Playback/Granular | Granular Random Pan | 383 | Randomise the stereo position of each grain. At 0% all grains play centred, at 100% grains can be panned anywhere from fully left to fully right |
| Layer 2/Playback/Granular | Granular Random Detune | 384 | Randomise the pitch of each grain. At 0% all grains play at the original pitch, at 100% grains can be detuned up to a semitone up or down |
| Layer 2/Playback/Granular | Granular Random Direction | 385 | Chance that grains spawn playing in the opposite direction to the main playhead. At 0% all grains play in the main direction, at 100% there's a 50/50 chance of each grain playing forwards or backwards |
| Layer 2/Playback/Granular | Granular Harmony | 386 | Chance that grains spawn at one of the selected harmony intervals instead of the root pitch. Configure which intervals are active using the Intervals button |
| Layer 2/LFO | Legacy Shape V2 | 387 | Legacy LFO shape parameter (v2). Kept for backwards-compatibility with DAW automation |
| Layer 2/LFO | Target | 388 | The parameter that the LFO will modulate |
| Layer 2/Main/Filter | Resonance | 389 | The intensity of the volume peak at the cutoff frequency |
| Layer 2/Arp | Note Order | 390 | Order in which held notes are played |
| Layer 2/Arp | Trigger | 391 | Free: arpeggiator keeps running when new notes are pressed. Retrigger: arpeggiator restarts from step 1 |
| Layer 2/Arp | Rate | 392 | Arpeggiator rate (synced to host tempo) |
| Layer 2/Arp | Length | 393 | Number of active steps in the arpeggiator pattern |
| Layer 2/Arp | Arpeggiator Mode | 394 | Played Notes: arpeggiates held notes. Fixed Notes: plays a recorded note sequence |
| Layer 2/Arp | Humanise | 395 | Add random timing variation to note starts and velocity. Higher values create looser, more human-like performance |
| Layer 2/Arp | Auto Rate | 396 | Automatically pick an arpeggiator rate based on the sliced instrument's loop length and host tempo. |
| Layer 2/Arp | Oct Polyrate | 397 | Each octave plays at a different rate. Double means each octave up is 2x faster. 3:2 and 4:3 create polyrhythmic relationships between octaves |
| Layer 2/Arp | One Shot | 398 | When enabled, the arpeggiator plays through the sequence once and then stops instead of looping |
| Layer 2/EQ/Band 1 | Resonance | 399 | Band 1: sharpness of the peak |
| Layer 2/EQ/Band 2 | Resonance | 400 | Band 2: sharpness of the peak |
| Layer 2/Arp | Arpeggiator | 401 | Enable/disable the arpeggiator |
| Layer 2/LFO | Shape | 402 | Oscillator shape, including random and percussive waveforms |
| Layer 2/EQ/Band 1 | Type | 404 | Band 1: type of EQ band |
| Layer 2/EQ/Band 2 | Type | 405 | Band 2: type of EQ band |
| Layer 2/EQ/Band 3 | Legacy Frequency | 406 | Legacy band 3 frequency parameter. Kept for backwards-compatibility with DAW automation |
| Layer 2/EQ/Band 3 | Resonance | 407 | Band 3: sharpness of the peak |
| Layer 2/EQ/Band 3 | Gain | 408 | Band 3: volume gain at the frequency |
| Layer 2/EQ/Band 3 | Type | 409 | Band 3: type of EQ band |
| Layer 2/Main/Filter | Cutoff Frequency | 410 | The frequency at which the filter should take effect |
| Layer 2/EQ/Band 1 | Frequency | 411 | Band 1: frequency of this band |
| Layer 2/EQ/Band 2 | Frequency | 412 | Band 2: frequency of this band |
| Layer 2/EQ/Band 3 | Frequency | 413 | Band 3: frequency of this band |
| Layer 2/Main/Filter | Type | 414 | Filter type |
| Layer 2 | Stereo Width | 415 | Layer stereo width: negative narrows toward mono, positive widens |
| Layer 3 | Volume | 480 | Layer volume |
| Layer 3 | Mute | 481 | Mute this layer |
| Layer 3 | Solo | 482 | Mute all other layers |
| Layer 3 | Pan | 483 | Left/right balance |
| Layer 3 | Detune Cents | 484 | Layer pitch in cents; hold shift for finer adjustment |
| Layer 3 | Pitch Semitones | 485 | Layer pitch in semitones |
| Layer 3/Playback/Loop | Start | 487 | Loop-start |
| Layer 3/Playback/Loop | End | 488 | Loop-end |
| Layer 3/Playback/Loop | Crossfade Size | 489 | Crossfade length; this smooths the transition from the loop-end to the loop-start |
| Layer 3/Playback/Loop | Sample Start Offset | 491 | Change the starting point of the sample |
| Layer 3/Playback/Loop | Reverse On | 492 | Play the sound in reverse |
| Layer 3/Main/Volume Envelope | On | 493 | Enable/disable the volume envelope; when disabled, each sound will play out entirely |
| Layer 3/Main/Volume Envelope | Attack | 494 | Volume fade-in length |
| Layer 3/Main/Volume Envelope | Decay | 495 | Volume ramp-down length (after the attack) |
| Layer 3/Main/Volume Envelope | Sustain | 496 | Volume level to sustain (after decay) |
| Layer 3/Main/Volume Envelope | Release | 497 | Volume fade-out length (after the note is released) |
| Layer 3/Main/Filter | On | 498 | Enable/disable the filter |
| Layer 3/Main/Filter | Legacy Cutoff Frequency | 499 | Legacy cutoff parameter. Kept for backwards-compatibility with DAW automation |
| Layer 3/Main/Filter | Legacy Resonance | 500 | Legacy resonance parameter. Kept for backwards-compatibility with DAW automation |
| Layer 3/Main/Filter | Legacy Type | 501 | Legacy filter type parameter. Kept for backwards-compatibility with DAW automation |
| Layer 3/Main/Filter | Envelope Amount | 502 | How strongly the envelope should control the filter cutoff |
| Layer 3/Main/Filter | Attack | 503 | Length of initial ramp-up |
| Layer 3/Main/Filter | Decay | 504 | Length ramp-down after attack |
| Layer 3/Main/Filter | Sustain | 505 | Level to sustain after decay has completed |
| Layer 3/Main/Filter | Release | 506 | Length of ramp-down after note is released |
| Layer 3/LFO | On | 507 | Enable/disable the Low Frequency Oscillator (LFO) |
| Layer 3/LFO | Legacy Shape | 508 | Legacy LFO shape parameter. Kept for backwards-compatibility with DAW automation |
| Layer 3/LFO | Mode | 509 | Oscillator phase mode. Retrigger: each voice has its own phase, Free: all voices that are playing simultaneously will have the same phase |
| Layer 3/LFO | Amount | 510 | Intensity of the LFO effect |
| Layer 3/LFO | Target (Legacy) | 511 | Legacy LFO target parameter. Kept for backwards-compatibility with DAW automation |
| Layer 3/LFO | Time (Tempo Synced) | 512 | LFO rate (synced to the host) |
| Layer 3/LFO | Time (Hz) | 513 | LFO rate (in Hz) |
| Layer 3/LFO | Sync On | 514 | Sync the LFO speed to the host |
| Layer 3/EQ | On | 515 | Turn on or off the equaliser effect for this layer |
| Layer 3/EQ/Band 1 | Legacy Frequency | 516 | Legacy band 1 frequency parameter. Kept for backwards-compatibility with DAW automation |
| Layer 3/EQ/Band 1 | Legacy Resonance | 517 | Legacy resonance parameter. Kept for backwards-compatibility with DAW automation |
| Layer 3/EQ/Band 1 | Gain | 518 | Band 1: volume gain at the frequency |
| Layer 3/EQ/Band 1 | Legacy Type | 519 | Legacy type parameter. Kept for backwards-compatibility with DAW automation |
| Layer 3/EQ/Band 2 | Legacy Frequency | 520 | Legacy band 2 frequency parameter. Kept for backwards-compatibility with DAW automation |
| Layer 3/EQ/Band 2 | Legacy Resonance | 521 | Legacy resonance parameter. Kept for backwards-compatibility with DAW automation |
| Layer 3/EQ/Band 2 | Gain | 522 | Band 2: volume gain at the frequency |
| Layer 3/EQ/Band 2 | Legacy Type | 523 | Legacy type parameter. Kept for backwards-compatibility with DAW automation |
| Layer 3/Config | Legacy Velocity Mapping | 524 | Choose how MIDI velocity should affect the volume of this layer. There are 6 modes that can be selected for this parameter via the buttons on the GUI. By setting one layer to be quiet at high velocities and another layer to be quiet at low velocities you can create an instrument that sounds different based on how hard the notes are played. (0) Ignore velocity, always play full volume. (1) Loudest at high velocity, quietist at low velocity (2) Loudest at low velocity, quietist at high velocity (3) Loudest at high velocity, quietist at middle velocity and below (4) Loudest at middle velocity, quietist at both high and low velocities (5) Loudest at bottom velocity, quietist at middle velocity and above, |
| Layer 3/Config | Keytrack On | 525 | Tune the sound to match the key played; if disabled it will always play the sound at its root pitch |
| Layer 3/Config | Legacy Monophonic On | 526 | Only allow one voice of each sound to play at a time |
| Layer 3/Config | MIDI Transpose On | 528 | Transpose the mapping of samples by the given semitone offset, meaning a higher/lower sample may be triggered instead of stretching/shrinking the audio by large amounts (only useful if the instrument is multi-sampled) |
| Layer 3/Playback/Loop | Loop Mode | 529 | The mode for looping the samples |
| Layer 3/Config | Key Range Low | 530 | The lowest key that will trigger this layer; if the key is lower than this, the layer will not play |
| Layer 3/Config | Key Range High | 531 | The highest key that will trigger this layer; if the key is higher than this, the layer will not play |
| Layer 3/Config | Key Range Low Fade | 532 | The length of the volume fade-in at the low end of the key range |
| Layer 3/Config | Key Range High Fade | 533 | The length of the volume fade-out at the high end of the key range |
| Layer 3/Config | Pitch Bend Range | 534 | The pitch range in semitones of the MIDI pitch wheel |
| Layer 3/Config | Monophonic Mode | 535 | Control voice behavior when notes overlap. Off: multiple voices play simultaneously (polyphonic). Retrigger: new notes stop previous notes. Latch: first note plays until all keys are released, new notes are ignored |
| Layer 3/Playback | Play Mode | 536 | How this layer plays its samples |
| Layer 3/Playback/Granular | Granular Length | 537 | Duration of each grain snippet |
| Layer 3/Playback/Granular | Granular Speed | 538 | How fast the grain position moves through the sample |
| Layer 3/Playback/Granular | Granular Position | 539 | Where in the sample grains are sourced from |
| Layer 3/Playback/Granular | Granular Density | 540 | Controls how densely grains overlap, relative to the grain length. At the midpoint, grains play end-to-end. Lower values add gaps between grains for a sparse texture; higher values make grains overlap for a denser, richer sound |
| Layer 3/Playback/Granular | Granular Spread | 541 | Region around the playhead where grains can start from. Small values focus grains near the playhead, large values spread them across a wider area |
| Layer 3/Playback/Granular | Granular Smoothing | 542 | Crossfade between grains to remove clicks. Low is hard cuts, high is full overlap fade |
| Layer 3/Playback/Granular | Granular Random Pan | 543 | Randomise the stereo position of each grain. At 0% all grains play centred, at 100% grains can be panned anywhere from fully left to fully right |
| Layer 3/Playback/Granular | Granular Random Detune | 544 | Randomise the pitch of each grain. At 0% all grains play at the original pitch, at 100% grains can be detuned up to a semitone up or down |
| Layer 3/Playback/Granular | Granular Random Direction | 545 | Chance that grains spawn playing in the opposite direction to the main playhead. At 0% all grains play in the main direction, at 100% there's a 50/50 chance of each grain playing forwards or backwards |
| Layer 3/Playback/Granular | Granular Harmony | 546 | Chance that grains spawn at one of the selected harmony intervals instead of the root pitch. Configure which intervals are active using the Intervals button |
| Layer 3/LFO | Legacy Shape V2 | 547 | Legacy LFO shape parameter (v2). Kept for backwards-compatibility with DAW automation |
| Layer 3/LFO | Target | 548 | The parameter that the LFO will modulate |
| Layer 3/Main/Filter | Resonance | 549 | The intensity of the volume peak at the cutoff frequency |
| Layer 3/Arp | Note Order | 550 | Order in which held notes are played |
| Layer 3/Arp | Trigger | 551 | Free: arpeggiator keeps running when new notes are pressed. Retrigger: arpeggiator restarts from step 1 |
| Layer 3/Arp | Rate | 552 | Arpeggiator rate (synced to host tempo) |
| Layer 3/Arp | Length | 553 | Number of active steps in the arpeggiator pattern |
| Layer 3/Arp | Arpeggiator Mode | 554 | Played Notes: arpeggiates held notes. Fixed Notes: plays a recorded note sequence |
| Layer 3/Arp | Humanise | 555 | Add random timing variation to note starts and velocity. Higher values create looser, more human-like performance |
| Layer 3/Arp | Auto Rate | 556 | Automatically pick an arpeggiator rate based on the sliced instrument's loop length and host tempo. |
| Layer 3/Arp | Oct Polyrate | 557 | Each octave plays at a different rate. Double means each octave up is 2x faster. 3:2 and 4:3 create polyrhythmic relationships between octaves |
| Layer 3/Arp | One Shot | 558 | When enabled, the arpeggiator plays through the sequence once and then stops instead of looping |
| Layer 3/EQ/Band 1 | Resonance | 559 | Band 1: sharpness of the peak |
| Layer 3/EQ/Band 2 | Resonance | 560 | Band 2: sharpness of the peak |
| Layer 3/Arp | Arpeggiator | 561 | Enable/disable the arpeggiator |
| Layer 3/LFO | Shape | 562 | Oscillator shape, including random and percussive waveforms |
| Layer 3/EQ/Band 1 | Type | 564 | Band 1: type of EQ band |
| Layer 3/EQ/Band 2 | Type | 565 | Band 2: type of EQ band |
| Layer 3/EQ/Band 3 | Legacy Frequency | 566 | Legacy band 3 frequency parameter. Kept for backwards-compatibility with DAW automation |
| Layer 3/EQ/Band 3 | Resonance | 567 | Band 3: sharpness of the peak |
| Layer 3/EQ/Band 3 | Gain | 568 | Band 3: volume gain at the frequency |
| Layer 3/EQ/Band 3 | Type | 569 | Band 3: type of EQ band |
| Layer 3/Main/Filter | Cutoff Frequency | 570 | The frequency at which the filter should take effect |
| Layer 3/EQ/Band 1 | Frequency | 571 | Band 1: frequency of this band |
| Layer 3/EQ/Band 2 | Frequency | 572 | Band 2: frequency of this band |
| Layer 3/EQ/Band 3 | Frequency | 573 | Band 3: frequency of this band |
| Layer 3/Main/Filter | Type | 574 | Filter type |
| Layer 3 | Stereo Width | 575 | Layer stereo width: negative narrows toward mono, positive widens |
| Effect/Distortion | Type | 3 | Distortion algorithm type |
| Effect/Distortion | Drive | 4 | Distortion amount |
| Effect/Distortion | On | 5 | Enable/disable the distortion effect |
| Effect/Bitcrush | Bits | 6 | Audio resolution |
| Effect/Bitcrush | Sample Rate | 7 | Sample rate |
| Effect/Bitcrush | Legacy Wet | 8 | Legacy processed-signal volume. Kept for backwards-compatibility with DAW automation |
| Effect/Bitcrush | Legacy Dry | 9 | Legacy unprocessed-signal volume. Kept for backwards-compatibility with DAW automation |
| Effect/Bitcrush | On | 10 | Enable/disable the bitcrush effect |
| Effect/Compressor | Legacy Threshold | 11 | Legacy threshold parameter. Kept for backwards-compatibility with DAW automation |
| Effect/Compressor | Legacy Ratio | 12 | Legacy ratio parameter. Kept for backwards-compatibility with DAW automation |
| Effect/Compressor | Gain | 13 | Additional control for volume after compression |
| Effect/Compressor | Auto Gain | 14 | Automatically re-adjust the gain to stay consistent regardless of compression intensity |
| Effect/Compressor | On | 15 | Enable/disable the compression effect |
| Effect/Filter | On | 16 | Enable/disable the filter |
| Effect/Filter | Legacy Cutoff Frequency | 17 | Legacy cutoff parameter. Kept for backwards-compatibility with DAW automation |
| Effect/Filter | Legacy Resonance | 18 | Legacy resonance parameter. Kept for backwards-compatibility with DAW automation |
| Effect/Filter | Legacy Gain | 19 | Legacy gain parameter. Kept for backwards-compatibility with DAW automation |
| Effect/Filter | Legacy Type | 20 | Legacy type parameter. Kept for backwards-compatibility with DAW automation |
| Effect/StereoWiden | Width | 21 | Increase or decrease the stereo width |
| Effect/StereoWiden | On | 22 | Turn the stereo widen effect on or off |
| Effect/Chorus | Rate | 23 | Chorus modulation rate |
| Effect/Chorus | Legacy High-pass | 24 | Legacy high-pass parameter. Kept for backwards-compatibility with DAW automation |
| Effect/Chorus | Depth | 25 | Chorus effect intensity |
| Effect/Chorus | Legacy Wet | 26 | Legacy processed-signal volume. Kept for backwards-compatibility with DAW automation |
| Effect/Chorus | Legacy Dry | 27 | Legacy unprocessed-signal volume. Kept for backwards-compatibility with DAW automation |
| Effect/Chorus | On | 28 | Enable/disable the chorus effect |
| Effect/Filter | Resonance | 29 | The intensity of the volume peak at the cutoff frequency |
| Effect/Filter | Gain | 30 | Volume gain of shelf/peak filter |
| Effect/StereoWiden | Mode | 31 | Stereo widening algorithm: Balanced (constant-power M/S), Legacy (original behaviour, kept for old presets), or Bass Mono (mono below the crossover, widened above) |
| Effect/StereoWiden | Bass Mono | 32 | Frequencies below this point are summed to mono (Bass Mono mode only) |
| Effect/Compressor | Type | 33 | The compressor algorithm to use |
| Effect/Compressor | Attack | 34 | How quickly the compressor responds to a rise in level |
| Effect/Compressor | Release | 35 | How quickly the compressor recovers after the level drops |
| Effect/Compressor | Mix | 36 | Blend between the dry input and the compressed signal |
| Effect/Convolution Reverb | Legacy High-pass | 65 | Legacy high-pass parameter. Kept for backwards-compatibility with DAW automation |
| Effect/Convolution Reverb | Legacy Wet | 66 | Legacy processed-signal volume. Kept for backwards-compatibility with DAW automation |
| Effect/Convolution Reverb | Legacy Dry | 67 | Legacy unprocessed-signal volume. Kept for backwards-compatibility with DAW automation |
| Effect/Convolution Reverb | On | 68 | Enable/disable the convolution reverb effect |
| Effect/Reverb | Decay Time | 69 | Reverb decay time |
| Effect/Reverb | Pre Low Cutoff | 70 | Low-pass filter cutoff before reverb |
| Effect/Reverb | Pre High Cutoff | 71 | High-pass filter cutoff before reverb |
| Effect/Reverb | Low Cutoff | 72 | Low-pass filter cutoff after reverb |
| Effect/Reverb | Low Gain | 73 | Low-pass filter gain |
| Effect/Reverb | High Cutoff | 74 | High-pass filter cutoff after reverb |
| Effect/Reverb | High Gain | 75 | High-pass filter gain |
| Effect/Reverb | Chorus Amount | 76 | Chorus effect amount |
| Effect/Reverb | Chorus Frequency | 77 | Chorus effect frequency |
| Effect/Reverb | Size | 78 | Reverb size |
| Effect/Reverb | Delay | 79 | Reverb delay |
| Effect/Reverb | Mix | 80 | Processed signal volume |
| Effect/Reverb | On | 81 | Enable/disable the reverb effect |
| Effect/Phaser | Feedback | 82 | Feedback amount |
| Effect/Phaser | Mod Rate | 83 | Speed at which the phaser filters modulate |
| Effect/Phaser | Center Frequency | 84 | Center frequency of the phaser filters |
| Effect/Phaser | Shape | 85 | Shape of the phaser filter's peaks |
| Effect/Phaser | Mod Depth | 86 | The range over which the phaser filters modulate |
| Effect/Phaser | Stereo Amount | 87 | Adds a stereo effect by offsetting the left and right filters |
| Effect/Phaser | Mix | 88 | Mix between the wet and dry signals |
| Effect/Phaser | On | 89 | Enable/disable the phaser effect |
| Effect/Delay | Mode | 90 | Delay type |
| Effect/Delay | Filter Cutoff | 91 | High/low frequency reduction |
| Effect/Delay | Filter Spread | 92 | Width of the filter |
| Effect/Delay | Time Left (ms) | 93 | Left delay time (in milliseconds) |
| Effect/Delay | Legacy Time Right (ms) | 94 | Right delay time (in milliseconds) |
| Effect/Delay | Time Left (Tempo Synced) | 95 | Left delay time (synced to the host tempo) |
| Effect/Delay | Time Right (Tempo Synced) | 96 | Right delay time (synced to the host tempo) |
| Effect/Delay | On | 97 | Synchronise timings to the host's BPM |
| Effect/Delay | Mix | 98 | Level of processed signal |
| Effect/Delay | On | 99 | Enable/disable the delay effect |
| Effect/Delay | Feedback | 100 | How much the signal repeats |
| Effect/Filter | Type | 105 | Filter type |
| Effect/Filter | Cutoff Frequency | 106 | Frequency of filter effect |
| Effect/Chorus | High-pass | 107 | High-pass filter cutoff |
| Effect/Convolution Reverb | High-pass | 108 | Wet high-pass filter cutoff |
| Effect/Bitcrush | Mix | 109 | Blend between the dry input and the bitcrushed signal |
| Effect/Bitcrush | Output | 110 | Output level after the mix |
| Effect/Chorus | Mix | 111 | Blend between the dry input and the chorused signal |
| Effect/Chorus | Output | 112 | Output level after the mix |
| Effect/Convolution Reverb | Mix | 113 | Blend between the dry input and the convolution reverb signal |
| Effect/Convolution Reverb | Output | 114 | Output level after the mix |
| Effect/Distortion | Mix | 115 | Blend between the dry input and the distorted signal |
| Effect/StereoWiden | Mix | 116 | Blend between the dry input and the stereo-widened signal |
| Effect/Filter | Mix | 117 | Blend between the dry input and the filtered signal |
| Effect/Compressor | Threshold | 118 | The threshold that the audio has to pass above before the compression should start taking place |
| Effect/Compressor | Ratio | 119 | The intensity of compression (high ratios mean more compression) |
| Effect/EQ | On | 120 | Enable/disable the equaliser effect |
| Effect/EQ | Mix | 121 | Mix between the wet and dry signals |
| Effect/EQ/Band 1 | Type | 122 | Band 1: type of EQ band |
| Effect/EQ/Band 1 | Frequency | 123 | Band 1: frequency of this band |
| Effect/EQ/Band 1 | Resonance | 124 | Band 1: sharpness of the peak |
| Effect/EQ/Band 1 | Gain | 125 | Band 1: volume gain at the frequency |
| Effect/EQ/Band 2 | Type | 126 | Band 2: type of EQ band |
| Effect/EQ/Band 2 | Frequency | 127 | Band 2: frequency of this band |
| Effect/EQ/Band 2 | Resonance | 128 | Band 2: sharpness of the peak |
| Effect/EQ/Band 2 | Gain | 129 | Band 2: volume gain at the frequency |
| Effect/EQ/Band 3 | Type | 130 | Band 3: type of EQ band |
| Effect/EQ/Band 3 | Frequency | 131 | Band 3: frequency of this band |
| Effect/EQ/Band 3 | Resonance | 132 | Band 3: sharpness of the peak |
| Effect/EQ/Band 3 | Gain | 133 | Band 3: volume gain at the frequency |
| Master | Volume | 0 | Master volume |
| Master | Velocity To Volume Strength | 1 | The amount that the MIDI velocity affects the volume of notes; 100% means notes will be silent when the velocity is very soft, and 0% means that notes will play full volume regardless of the velocity |
| Master | Timbre | 2 | The intstruments timbre. Not every instrument contains timbre information; instruments that do will be highlighted when you click on this knob. |
| Macro | Macro 1 | 101 | A macro that can be assigned to any parameter in the plugin. The macro will affect all parameters that are assigned to it. |
| Macro | Macro 2 | 102 | A macro that can be assigned to any parameter in the plugin. The macro will affect all parameters that are assigned to it. |
| Macro | Macro 3 | 103 | A macro that can be assigned to any parameter in the plugin. The macro will affect all parameters that are assigned to it. |
| Macro | Macro 4 | 104 | A macro that can be assigned to any parameter in the plugin. The macro will affect all parameters that are assigned to it. |
