# Effects Page

> How to use Floe's effects rack, and details of each effect

![Floe's window with the EFFECTS page tab highlighted](https://assets.floe.audio/website/screenshots/page-effects.73801094172d2026.png)

The _Effects_ page is where you colour the whole sound. The three [layers](/docs/usage/layers) are mixed together and then passed through a single rack of effects, from top to bottom. Use them lightly to add some space or warmth, or push much further with heavy distortion, filtering and modulation to take the sound somewhere new. There's 12 effects available. Each can be used once in the rack, in whatever order you like.

![Floe's Effects page with its parts numbered](https://assets.floe.audio/website/screenshots/effects.39fc87a1ccda5330.png)12345

1.  **Switchboard** — every effect listed in order, each on its own card. Tick a card's checkbox to add that effect to the rack.
2.  **Rack** — the effects that are in use, with their controls.
3.  **Effect name** — drag it to move the effect up or down the chain. Right-click for more options.
4.  **Mix, remove and bypass** — _Mix_ sets how much of the effect you hear, removes the effect from the rack, and bypasses it.
5.  **All-effect buttons** — these act on the whole rack at once: randomise , bypass all , and remove all .

### Adding and removing effects

The switchboard (1) lists every effect. Those in the rack are shown brightly with a ticked checkbox; the rest are dimmed.

-   **Add an effect** by ticking its checkbox. Doing so will add it to the rack, and the rack will scroll to show it.
-   **Remove an effect** by unticking its checkbox, or clicking next to its _Mix_ knob (4). Removing an effect doesn't reset its controls, it will be the same if you re-add it later.
-   **Jump to an effect** by clicking its card anywhere other than the checkbox. The rack will scroll to show it, with a brief flash so you can spot it.

### Bypassing effects

Bypassing an effect lets the sound pass straight through it while it stays in the rack, ready to switch back on. It's the easiest way to hear exactly what an effect is contributing. A bypassed effect is greyed out and uses no CPU.

Click next to an effect's _Mix_ knob (4), or the coloured light at the left of its switchboard card, to bypass it. Click again to switch it back on.

The button at the top right of the page (5) bypasses every effect in the rack at once, so you can hear your layers without any processing. Click it again to switch them all back on.

### Changing the order

The effects run from top to bottom, and the order can make a big difference: a reverb after a distortion gives a clean tail on a gritty sound, while a reverb before it distorts the tail too.

Drag an effect's name (3) up or down the rack to move it. You can also drag the cards in the switchboard: hover over a card and a grip appears to show it can be moved. Because the switchboard also shows the effects that aren't in the rack, after some reordering the effects in use can end up scattered among the unused ones. Right-click the switchboard and choose _Tidy FX Order_ to group the effects in the rack at the top, without changing their order.

### Mix

Every effect has a _Mix_ knob (4). It blends the sound going into the effect with the effect's output. At 100% you hear only the effect's output; turn it down to bring back more of the unprocessed sound. It's a quick way to set an effect's strength without changing its other settings: a touch of reverb, gentle distortion, or parallel compression, say.

### The all-effect buttons

The buttons at the top right of the page (5) act on the whole rack:

-   **Randomise** — randomly puts each effect in or takes it out of the rack, and shuffles the order. The effects' settings aren't changed. It's a quick way to stumble on combinations you'd never have reached for. Use _Undo_ from the top panel if you don't like the result. For bigger changes, including new settings, try the [Random Variation](/docs/usage/randomisation) strip on the _Perform_ page.
-   **Bypass all** — deactivates all effects.
-   **Remove all** — removes every effect from the rack.

### Copying, pasting and resetting

Right-click an effect's name in the rack or its card in the switchboard for a menu to:

-   **Copy and paste one effect** — copy an effect's settings, then paste them onto the same effect on another preset.
-   **Copy and paste the whole rack** — copy every effect, its order and its settings, then paste them all at once to a new preset.
-   **Reset** — return the effect's settings to their defaults.

Copying works between presets: copy from one, load another, and paste. Right-click an empty part of the switchboard or rack for the whole-rack options, and to paste a single copied effect.

## Distortion

![The Distortion effect at its default settings](https://assets.floe.audio/website/screenshots/effect-distortion.a583b9946e316265.png)

Distortion adds harmonics to a sound, anything from gentle tape warmth to outright destruction. Choose a character from the _Type_ menu, then set how hard you push it with the _Drive_ knob. That's the whole workflow; everything else is there to shape the result. The display on the left passes a sine wave through your current settings, so you can see how each type and setting reshapes it.

It runs with 4× oversampling and is anti-aliased to stay clean and musical. It also has built-in gain compensation to keep the level steady as you turn up _Drive_ or _Punish_. For the Legacy types, you opt in to this with the _Auto Gain_ switch; for the other types it's always on.

-   **Type** — the character of the distortion. See below.
-   **Drive** — how hard the sound is pushed into the distortion, and the main control for how much distortion you hear. Each step of the knob adds a similar amount. For the Mangle types it also moves the effect itself, so the character changes as well as the intensity.
-   **Punish** — stacks extra distortion stages after the first, each driven harder than the last, like an amp cascading its gain stages. Turn it up for a denser, grittier, more compressed drive that holds its character as notes decay.
-   **Tilt EQ** — shifts the tone going into the distortion. Turn it up to saturate the highs for a brighter result, or down to push the lows for a warmer, fatter drive.
-   **Gain** — a flat level adjustment to the distorted sound, before _Mix_ blends it with the unprocessed sound. It's separate from the automatic gain compensation.
-   **Auto Gain** — for the Legacy types, holds the loudness steady as you turn up _Drive_. For the other types it's always on.

The types are grouped into two families in the menu. _Bite_ types saturate and thicken the sound, from clean warmth to hard clipping:

-   **Tape** — warm and smooth. Rounds off the loud peaks while quieter passages and tails stay clean.
-   **Valve** — rich, even harmonics that stay musical however hard you push, instead of collapsing into a harsh square.
-   **Overdrive** — pedal-style grit with a threshold. A note cleans up as it decays, and playing harder brings the bite in.
-   **Hard Clip** — clean, aggressive brickwall clipping.
-   **Octave** — adds a hard octave-up edge, then saturates. It squares up on hot notes and follows their dynamics.

_Mangle_ types are more extreme, tearing the sound apart and adding new tones rather than just thickening it:

-   **Bitcrush** — lo-fi digital crunch, made by lowering the effective sample rate. More _Drive_ means coarser and grittier.
-   **Ring Mod** — multiplies the sound by a low oscillator for clangorous, metallic, bell-like tones. _Drive_ raises the oscillator's pitch and deepens the effect.
-   **Sine Fold** — folds the wave through a sine curve for interesting, chaotic harmonics.
-   **Warp** — an asymmetric shaper that bends the wave lopsidedly into a gritty, off-kilter drive.
-   **Wavefolder** — folds the peaks back on themselves for bright, metallic harmonics.

The Legacy types are the original distortion algorithms, kept so that older presets and projects sound the same. They load with _Auto Gain_ off to keep their original level; turn it on to give them the same steady loudness as the other types.

## Bit Crush

![The Bit Crush effect at its default settings](https://assets.floe.audio/website/screenshots/effect-bit-crush.eb0ae0d0c3717f57.png)

Bit Crush is a lo-fi effect that degrades the sound in two ways: lowering the sample rate for ringing, metallic aliasing, and lowering the bit depth for gritty noise. The display shows a preview of the crushing applied to a rising test signal. Both controls start at full quality, so turn them down to hear the effect.

-   **Bits** — lowers the bit depth, adding a gritty digital noise that's most obvious in quiet passages and tails. At the lowest settings quiet sounds crush to silence, so feed the effect a strong signal.
-   **Samp Rate** — lowers the sample rate by holding each sample for longer. Lower rates fold high frequencies back down into ringing, metallic aliasing: the sound of early samplers and retro hardware.
-   **Output** — the level after the _Mix_ blend.

## Compressor

![The Compressor effect at its default settings](https://assets.floe.audio/website/screenshots/effect-compressor.5a5d0c835f3f6c23.png)

The compressor shapes the dynamics of the sound, evening out the difference between its loud and quiet parts. It has two types, each with its own character.

-   **Type** — _Modern_ is precise, and gives you the _Attack_ and _Release_ knobs. _Vintage_ has a bit more character, and sets its attack and release for you.
-   **Threshold** — the level the sound has to rise above before it's compressed.
-   **Ratio** — how strongly the sound is compressed once it's above the threshold. Higher ratios compress more, and above about 2:1 the pumping becomes much more noticeable.
-   **Attack** and **Release** (_Modern_ only) — how quickly the compressor responds to a rise in level, and how quickly it recovers after the level drops.
-   **Gain** — adjusts the level after compression.
-   **Auto Gain** (_Vintage_ only) — automatically adjusts the level so the sound stays consistently loud however hard it's compressed.

Three meters show what the compressor is doing: _In_ is the incoming level, with a marker at the threshold; _GR_ (gain reduction) is how much it's turning the sound down; and _Out_ is the final level.

## Filter

![The Filter effect at its default settings](https://assets.floe.audio/website/screenshots/effect-filter.6e299e9ebedef899.png)

The Filter cuts away part of the frequency range, or boosts or dips it. It's a single filter across the whole mix; each layer also has its own filter for shaping notes individually (see [Layers](/docs/usage/main-tab#filter)).

-   **Type** — the shape of the filter, such as low-pass, high-pass, band-pass, notch, peak or shelf. The low-pass and high-pass types come in 12 and 24 dB per octave slopes.
-   **Cutoff** — where the filter does its work: where the low-pass and high-pass filters roll off from, the centre of the band for band-pass and notch, or the corner for the shelves.
-   **Reso** — resonance, which emphasises the frequencies right at the cutoff, adding a peak there on the low-pass and high-pass filters. For band-pass and notch it narrows the band, for peak it tightens the boost, and for the shelves it steepens the corner.
-   **Gain** — how far the peak and shelf types boost or cut. It only appears for those types.

## Stereo Widen

![The Stereo Widen effect at its default settings](https://assets.floe.audio/website/screenshots/effect-stereo-widen.689f9c85db9c28f0.png)

Stereo Widen narrows the sound towards mono or spreads it out wider. Each layer also has its own _Stereo_ control, if you want to widen just one of them.

-   **Mode** — how the widening is done. Hover over the options in the menu for a description of each.
    -   _Balanced_ keeps the loudness the same wherever _Width_ is set. It's the one to reach for by default.
    -   _Bass Mono_ widens in the same way, but keeps everything below the _Bass Mono_ frequency in the centre.
    -   _Legacy_ is the widening from older versions of Floe, kept so that existing presets sound as they always did. It gets louder as _Width_ goes up.
-   **Width** — negative values pull the sound towards mono, positive values push it wider, and 0% leaves it as it is. It works by rebalancing the mid (what both channels share) against the side (what differs between them). At the far left only the mid is left; at the far right only the side is left, so anything dead centre disappears.
-   **Bass Mono** (_Bass Mono_ mode only) — the frequency below which the sound is kept in the centre. Holding the low end in the middle keeps kicks and basslines solid, and stops them losing power on mono systems. The split uses a 24 dB per octave Linkwitz-Riley crossover, so the two bands recombine without a dip around the crossover point.

## Chorus

![The Chorus effect at its default settings](https://assets.floe.audio/website/screenshots/effect-chorus.3bdf4b0fca3fc2fe.png)

The Chorus thickens the sound by layering it with delayed copies that drift in pitch. Gentle settings add a subtle shimmer and movement, while deeper settings give an obvious, tape-like wobble.

-   **Rate** — how quickly the pitch drifts up and down. Low settings give a gentle swelling movement, while turning it up gives a quicker, shimmering warble.
-   **Depth** — how far the pitch drifts away from the original. Small amounts thicken the sound, while larger amounts give an obvious wobble. At zero, the delayed copies are still there, adding a static colouration.
-   **High-pass** — removes low frequencies from the chorused copies, leaving the unprocessed sound untouched. Raise it to keep the bass tight and free from movement.
-   **Output** — the level after the _Mix_ blend.

## Reverb

![The Reverb effect at its default settings](https://assets.floe.audio/website/screenshots/effect-reverb.3ca4505d77aaecb2.png)

The Reverb simulates the reflections of a real space, from a small, tight room to a vast hall that takes many seconds to fade. It can also modulate its tail for a shimmering, moving sound.

-   **Pre LP** and **Pre HP** — low-pass and high-pass filters on the sound going into the reverb. _Pre LP_ rolls off the highs for a darker, more distant space. _Pre HP_ rolls off the lows, keeping the tail clear so the bass stays tight and centred. The unprocessed sound keeps its full range.
-   **Lo-Shelf** and **Lo-Gain** — cut the reverb's tail below the _Lo-Shelf_ frequency by the _Lo-Gain_ amount. The cut builds up as the tail recirculates, so the low end thins out as the reverb fades: handy for stopping long tails clouding the bass. Turn _Lo-Gain_ down to hear it work.
-   **Hi-Shelf** and **Hi-Gain** — the same for the highs, so they die away first, just as soft furnishings absorb them in a real room.
-   **Decay** — how long the tail takes to fade away once a sound stops. It holds steady whatever the _Size_, so you can choose the length of the reverb and the character of the space separately.
-   **Size** — how large the space feels. Small values pack the reflections together for a boxy, coloured character; large values spread them out for something open and airy.
-   **Predelay** — holds the reverb back for a moment so the unprocessed sound is heard on its own first. Short amounts keep the sound upfront and defined, while longer amounts push the walls further away.
-   **Mod Rate** and **Depth** — make the reflections drift, smearing the resonances that build up in the tail. A little _Depth_ stops long tails ringing; more gives an obvious shimmer and movement. _Mod Rate_ sets how quickly the drift moves: keep it low for a gentle, breathing wash, or turn it up for a quicker, more distinct warble.

## Delay

![The Delay effect at its default settings](https://assets.floe.audio/website/screenshots/effect-delay.e6e66ebc5fbfc868.png)

The Delay is a stereo echo. You can set the left and right times separately, either freely or synced to tempo, bounce the repeats between the speakers with the ping-pong modes, and thin them out with a filter as they fade.

-   **Time L** and **Time R** — the gap between the repeats in the left and right channels. With _Tempo Sync_ off, they're set freely in milliseconds. Very short times stop sounding like separate echoes and become a metallic, resonant tone instead.
-   **Tempo Sync** — sets the times as note lengths that follow your DAW's tempo, keeping the repeats in time with your track.
-   **Feedback** — how long the echoes trail on for. Low amounts give a handful of repeats, while high amounts carry on long after the note has gone. It works by feeding each repeat back in to make the next one.
-   **Mode** — how the repeats move around the stereo image:
    -   _Mono_ — both channels repeat at the _Time L_ setting, so the echoes sit where the original sound sits in the stereo image. _Time R_ is unused.
    -   _Stereo_ — each channel has its own delay, so setting _Time L_ and _Time R_ apart spreads the repeats across the stereo image.
    -   _Ping-pong_ — the sound is summed to the centre, and the repeats then bounce from one side to the other, with _Time L_ and _Time R_ setting how long each half of the bounce lasts.
    -   _Mid ping-pong_ — the repeats bounce from side to side as in _Ping-pong_, but the sound keeps its stereo image on the way in. Choose _Ping-pong_ for the clearest left-right bounce, especially from a wide sound, and _Mid ping-pong_ when you want the echoes to keep some of the original's width.
-   **Filter** and **Spread** — a band-pass filter the repeats run through, leaving the unprocessed sound untouched. _Filter_ sets the centre of the band and _Spread_ its width. Wide open, the repeats stay close to the original sound; narrowed, they squeeze towards the _Filter_ frequency. The filter sits inside the feedback loop, so every repeat is filtered again and the echoes thin out as they fade.

## Convolution Reverb

![The Convolution Reverb effect at its default settings](https://assets.floe.audio/website/screenshots/effect-convolution-reverb.80934c27d68d6c02.png)

The Convolution Reverb gets all its character from an impulse response (IR): a recording of how a space or object responds to sound. Floe's IRs are chosen for character. Some are realistic spaces, but most are stranger: textures and resonances that can transform a sound, making this as much a sound-design tool as a reverb.

-   **Impulse** — click the IR's name to choose one in the [IR browser](/docs/usage/browsers). The arrows beside it, and , step to the previous or next IR, and jumps to a random one. All three follow the filters you've set in the IR browser. unloads the IR. There's no reverb until one is loaded.
-   **High-pass** — rolls off the low end of the reverb to avoid muddy bass, before the _Mix_ blend.
-   **Output** — the level after the _Mix_ blend.

Floe always has its own built-in IRs, and some sample libraries add more.

## Phaser

![The Phaser effect at its default settings](https://assets.floe.audio/website/screenshots/effect-phaser.440718747662cf50.png)

The Phaser sweeps a series of peaks and notches through the sound, giving it the classic swooshing, jet-like motion. Gentle settings add a subtle sense of movement to sustained sounds, while faster or more resonant ones become an unmistakable whoosh.

-   **Freq** — the centre of the sweep. Keep it low for a deep, throaty movement, or raise it to place the phasing amongst the brighter harmonics.
-   **Feedback** — sharpens the peaks and deepens the notches. Small amounts keep it a gentle swoosh, while higher amounts give the sweep a resonant, whistling edge.
-   **Shape** — how many peaks and notches there are. Turned down, there's a single broad one for a soft, vowel-like sweep; turned up, there are more of them for a richer, more obvious phasing.
-   **Rate** — how quickly the peaks sweep up and down. Low settings give a gentle, drifting movement, while turning it up makes the sweep a warble.
-   **Depth** — how far the peaks travel either side of _Freq_. Small amounts give a narrow, shimmering movement, while larger amounts sweep across the whole spectrum. With _Depth_ all the way down, the peaks hold still, leaving a fixed colouration that you can sweep by hand with _Freq_.
-   **Stereo** — offsets the left and right sweeps from each other so the movement drifts across the stereo image. At zero both channels sweep together; at maximum they sweep in opposite directions, for a wide, swirling feel.

## EQ

![The EQ effect at its default settings](https://assets.floe.audio/website/screenshots/effect-eq.65bb6b6d6747ec42.png)

The EQ is a three-band equaliser for lifting or taming parts of the frequency range, from broad tonal shaping to surgical cuts. Each layer also has its own EQ (see [Layers](/docs/usage/eq-tab)).

The graph shows the combined effect of all three bands. Drag a band's handle to move it, scroll over it to change its resonance, or right-click it to change its type or copy and paste the whole band. The three bands have identical controls:

-   **Type** — a peak or shelf for boosting and cutting, a notch for removing a narrow slice, or a low-pass or high-pass filter for rolling off one end of the spectrum.
-   **Freq** — where the band does its work: the centre of a peak or notch, the corner of a shelf, or the cutoff of a pass filter.
-   **Reso** — how tightly the band is focused. For peaks and notches it's the width, from broad and gentle to tight and focused. The shelves use it to steepen their corner, and the pass filters to add a peak at the cutoff.
-   **Gain** — how far the band boosts or cuts. Only the peak and shelf types use it.

## Limiter

![The Limiter effect at its default settings](https://assets.floe.audio/website/screenshots/effect-limiter.411ca416265c7bf3.png)

The limiter is a brickwall limiter: it holds your output firmly below a maximum level, whether you just want to catch stray peaks or push the overall level up without clipping. It's usually best placed last in the rack, so it catches everything above it.

-   **Gain** — drives the sound harder into the limiter. The harder you push, the more it's limited, making the sound louder and denser.
-   **Ceiling** — the level the output is never allowed to go above. Leave a small safety margin below 0 dB, giving headroom for whatever comes after Floe.

Three meters show what the limiter is doing: _In_ is the incoming level, _GR_ (gain reduction) is how much it's turning the sound down, and _Out_ is the final level.

It's a true-peak limiter, so the _Ceiling_ is a reliable maximum for the output. It looks a fraction of a millisecond ahead in the audio so it can react before a peak arrives. The delay is so small (0.5 ms) that Floe absorbs it rather than asking your DAW to compensate for it, so you don't need to do anything about it.

Thanks to that short lookahead, the limiter is fast and transparent for moderate amounts of limiting. Push it hard and there's less room for the gain change to ramp smoothly, so you may start to hear it colour or slightly distort the sound. If that happens, back off the _Gain_ or raise the _Ceiling_ a little.
