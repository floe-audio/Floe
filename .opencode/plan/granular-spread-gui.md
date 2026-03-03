# Plan: Precise Granular Spread Region Visualisation

## Goal

Draw the grain spread highlight region on the waveform GUI for both GranularFixed and GranularPlayback modes. The visualisation must precisely match the DSP's grain spawn boundaries — including showing two disjoint rectangles when the spread wraps around a loop boundary.

## Context

The spread parameter controls where grains can spawn relative to the main playhead position. The spread extends **onwards** from the playhead in `frame_pos` space (always increasing). When the playhead is inside a loop, grains that would spawn past the loop end wrap around to the loop start via modulo, creating a potential second spawn region.

### Current DSP behaviour (voices.cpp `AddGranularSampleDataOntoBuffer`)

The spawn region in `frame_pos` space is `[playhead.frame_pos, playhead.frame_pos + spread_fraction * num_frames]`.

- **GranularFixed**: No loops. Clamp to `[0, num_frames - 1]`. Single contiguous region.
- **GranularPlayback, in-loop** (`main_loop && only_use_frames_within_loop`): Modulo wrap into `[loop_start, loop_end)`. May produce two disjoint regions if the spread crosses `loop_end`.
- **GranularPlayback, pre-loop or no loop**: Clamp to `[0, num_frames - 1]`. Single contiguous region.

## Changes

### 1. Extend `VoiceWaveformMarkerForGui` (src/plugin/processor/voices.hpp)

Add spread region data to the existing marker struct. Two regions are needed because loop wrapping can create two disjoint segments.

```cpp
struct VoiceWaveformMarkerForGui {
    u16 position {};
    u16 intensity {};
    u8 layer_index {};

    // Grain spread region(s) in audio-data 0-1 space (using RealFramePos convention).
    // region_1 is the primary region. region_2 is only used when the spread wraps
    // around a loop boundary, creating a second disjoint segment.
    // Both start and end are 0 when inactive.
    struct SpreadRegion {
        u16 start {};
        u16 end {};
    };
    SpreadRegion spread_region_1 {};
    SpreadRegion spread_region_2 {};
};
```

Convention: `start` and `end` are in audio-data 0-1 space (as if returned by `RealFramePos / num_frames`), packed into u16. `start <= end` always (the `inverse_data_lookup` conversion swaps the endpoints so they're ordered correctly in audio-data space). When a region is inactive, both `start` and `end` are 0.

### 2. Compute spread bounds on the audio thread (src/plugin/processor/voices.cpp)

Add a static helper function that computes the spread region bounds. This function encapsulates the same boundary logic used during grain spawning, so the DSP behaviour is defined in one place.

```cpp
struct GrainSpreadBounds {
    struct Region {
        f64 start; // frame_pos space
        f64 end;   // frame_pos space
    };
    Region region_1 {};
    Region region_2 {};
    bool has_region_2 = false;
};

static GrainSpreadBounds ComputeGrainSpreadBounds(f64 playhead_frame_pos,
                                                   f32 spread_param,
                                                   Optional<PlayHead::Loop> const& loop,
                                                   u32 num_frames,
                                                   bool is_fixed) {
    auto const spread_size = (f64)GrainSpreadParamToFraction(spread_param) * (f64)num_frames;

    GrainSpreadBounds bounds {};

    if (is_fixed) {
        // GranularFixed: no loops, clamp to sample bounds.
        bounds.region_1.start = playhead_frame_pos;
        bounds.region_1.end = Min(playhead_frame_pos + spread_size, (f64)(num_frames - 1));
        return bounds;
    }

    auto const region_end_fp = playhead_frame_pos + spread_size;

    if (loop && loop->only_use_frames_within_loop) {
        auto const loop_start = (f64)loop->start;
        auto const loop_end = (f64)loop->end;
        auto const loop_size = loop_end - loop_start;

        if (loop_size <= 0) {
            bounds.region_1.start = playhead_frame_pos;
            bounds.region_1.end = playhead_frame_pos;
            return bounds;
        }

        if (spread_size >= loop_size) {
            // Spread covers the entire loop.
            bounds.region_1.start = loop_start;
            bounds.region_1.end = loop_end;
            return bounds;
        }

        if (region_end_fp <= loop_end) {
            // No wrapping needed — spread fits within loop.
            bounds.region_1.start = playhead_frame_pos;
            bounds.region_1.end = region_end_fp;
        } else {
            // Spread wraps around loop end -> two disjoint regions.
            auto const wrapped_end = loop_start + (region_end_fp - loop_end);

            bounds.region_1.start = playhead_frame_pos;
            bounds.region_1.end = loop_end;
            bounds.region_2.start = loop_start;
            bounds.region_2.end = wrapped_end;
            bounds.has_region_2 = true;
        }
    } else {
        // Pre-loop or no loop: clamp to sample bounds.
        bounds.region_1.start = playhead_frame_pos;
        bounds.region_1.end = Min(region_end_fp, (f64)(num_frames - 1));
    }

    return bounds;
}
```

Then, in `VoiceProcessor::Process` where voice markers are published (around line 537-553), compute and publish the spread bounds:

```cpp
// After computing position_for_gui, compute spread bounds for granular modes.
VoiceWaveformMarkerForGui::SpreadRegion spread_1 {};
VoiceWaveformMarkerForGui::SpreadRegion spread_2 {};

if (IsGranular(voice.controller->play_mode)) {
    for (auto const& s : voice.sound_sources) {
        if (!s.is_active || s.source_data.tag != InstrumentType::Sampler) continue;
        auto const& sampler = s.source_data.Get<VoiceSoundSource::SampleSource>();
        if (sampler.region->trigger.trigger_event == sample_lib::TriggerEvent::NoteOff) continue;

        auto const nf = sampler.data->num_frames;
        auto const is_fixed = voice.controller->play_mode == param_values::PlayMode::GranularFixed;
        auto const bounds = ComputeGrainSpreadBounds(
            sampler.playhead.frame_pos,
            voice.controller->granular.spread,
            sampler.playhead.loop,
            nf,
            is_fixed);

        // Convert from frame_pos space to audio-data 0-1 space.
        auto to_audio_01 = [&](f64 fp) -> f64 {
            auto const fi = (u32)Clamp(fp, 0.0, (f64)(nf - 1));
            auto const real = sampler.playhead.inverse_data_lookup ? ((nf - 1) - fi) : fi;
            return (f64)real / (f64)nf;
        };

        auto const s1 = to_audio_01(bounds.region_1.start);
        auto const e1 = to_audio_01(bounds.region_1.end);
        spread_1.start = (u16)(Min(s1, e1) * k_max_u16);
        spread_1.end = (u16)(Max(s1, e1) * k_max_u16);

        if (bounds.has_region_2) {
            auto const s2 = to_audio_01(bounds.region_2.start);
            auto const e2 = to_audio_01(bounds.region_2.end);
            spread_2.start = (u16)(Min(s2, e2) * k_max_u16);
            spread_2.end = (u16)(Max(s2, e2) * k_max_u16);
        }
        break; // Use the first active sampler source.
    }
}

voice.pool.voice_waveform_markers_for_gui.Write()[voice.index] = {
    .position = (u16)(Clamp01(position_for_gui) * (f64)k_max_u16),
    .intensity = (u16)(Clamp01(voice.current_gain) * k_max_u16),
    .layer_index = voice.controller->layer_index,
    .spread_region_1 = spread_1,
    .spread_region_2 = spread_2,
};
```

### 3. Refactor GUI spread drawing (src/plugin/gui/controls/gui_waveform.cpp)

#### Extract a helper function

```cpp
static void DrawSpreadRegionRect(imgui::DrawList& draw_list,
                                 Rect window_r,
                                 f32 viewport_w,
                                 f32 start_01,  // audio-data 0-1 space
                                 f32 end_01,    // audio-data 0-1 space
                                 bool reverse,
                                 u32 col) {
    if (start_01 >= end_01) return;

    // Convert from audio-data space to visual space.
    f32 visual_start = reverse ? (1.0f - end_01) : start_01;
    f32 visual_end = reverse ? (1.0f - start_01) : end_01;

    f32 const left = Max(window_r.x + visual_start * viewport_w, window_r.x);
    f32 const right = Min(window_r.x + visual_end * viewport_w, window_r.Right());
    if (left >= right) return;

    draw_list.AddRectFilled(f32x2 {left, window_r.y},
                            f32x2 {right, window_r.Bottom()},
                            col);
}
```

#### GranularFixed spread indicator

Replace the existing `#if EXPERIMENTAL_GRANULAR` block. For GranularFixed, the spread region should be visible even with no notes playing (it's a static configuration), so compute it from params as a fallback. When voices are active, the audio-thread-computed regions from voice markers would also be drawn (via the shared code below), but the param-based fallback ensures visibility during configuration.

```cpp
#if EXPERIMENTAL_GRANULAR
if (features.show_grain_position_indicator) {
    auto const grain_pos = params.LinearValue(layer.index, LayerParamIndex::GranularPosition);
    auto const grain_spread = params.LinearValue(layer.index, LayerParamIndex::GranularSpread);
    auto const reverse = params.BoolValue(layer.index, LayerParamIndex::Reverse);
    auto const col = LiveCol(UiColMap::WaveformRegionOverlay);

    f32 const spread_size = GrainSpreadParamToFraction(grain_spread);
    f32 const start = grain_pos;
    f32 const end = Min(grain_pos + spread_size, 1.0f);
    DrawSpreadRegionRect(*g.imgui.draw_list, window_r, viewport_r.w, start, end, reverse, col);
}
#endif
```

#### GranularPlayback spread regions (and GranularFixed when voices are active)

Draw the spread regions from voice marker data for any granular mode. This goes in the grain markers section (drawn below controls and voice cursors).

**Important**: The voice cursor section also calls `.Consume()` on `voice_waveform_markers_for_gui`. Since `Consume()` swaps the buffer, calling it twice would lose data. We need to call `.Consume()` once earlier and store the reference, then use it in both the spread region drawing and the voice cursor drawing.

```cpp
// Spread regions from voice markers (for all granular modes when voices are active).
if (options.play_mode.HasValue() && IsGranular(*options.play_mode) &&
    g.engine.processor.voice_pool.num_active_voices.Load(LoadMemoryOrder::Relaxed)) {
    // NOTE: uses the same consumed waveform markers reference as voice cursors below.
    bool const reverse = params.BoolValue(layer.index, LayerParamIndex::Reverse);
    auto const col = LiveCol(UiColMap::WaveformRegionOverlay);

    for (auto const voice_index : Range(k_num_voices)) {
        auto const& marker = voice_waveform_markers[voice_index];
        if (!marker.intensity || marker.layer_index != layer.index) continue;

        auto const r1_start = (f32)marker.spread_region_1.start / (f32)UINT16_MAX;
        auto const r1_end = (f32)marker.spread_region_1.end / (f32)UINT16_MAX;
        DrawSpreadRegionRect(*g.imgui.draw_list, window_r, viewport_r.w,
                             r1_start, r1_end, reverse, col);

        auto const r2_start = (f32)marker.spread_region_2.start / (f32)UINT16_MAX;
        auto const r2_end = (f32)marker.spread_region_2.end / (f32)UINT16_MAX;
        DrawSpreadRegionRect(*g.imgui.draw_list, window_r, viewport_r.w,
                             r2_start, r2_end, reverse, col);
    }
    GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::Animate);
}
```

### 4. Consolidate `.Consume()` call

The `voice_waveform_markers_for_gui.Consume()` call needs to happen once, before both the spread region drawing and the voice cursor drawing. Move the `.Consume()` to before the grain markers section and store the reference:

```cpp
// Consume voice waveform markers once for use by both spread regions and voice cursors.
auto& voice_waveform_markers =
    g.engine.processor.voice_pool.voice_waveform_markers_for_gui.Consume().data;
```

Then use `voice_waveform_markers` in both the spread region loop and the voice cursor loop (removing the duplicate `.Consume()` from the voice cursor section).

### 5. Drawing order

The final drawing order in `DoWaveformElement`:

1. Waveform image (with loop region colouring if applicable)
2. Grain markers (thin lines for individual grains)
3. **Spread regions** (translucent overlay rectangles — from voice markers for GranularPlayback, from params for GranularFixed)
4. Waveform controls (loop handles, offset handle, crossfade handle)
5. Granular position indicator (GranularFixed — the position line, from the `#if EXPERIMENTAL_GRANULAR` block)
6. Voice cursors (main playhead lines)
7. Macro destination regions

### 6. Summary of files changed

| File | Change |
|---|---|
| `src/plugin/processor/voices.hpp` | Add `SpreadRegion` fields to `VoiceWaveformMarkerForGui` |
| `src/plugin/processor/voices.cpp` | Add `ComputeGrainSpreadBounds` helper; publish spread bounds in `VoiceProcessor::Process` |
| `src/plugin/processor/granular.hpp` | No changes needed (existing `GrainSpreadParamToFraction` is reused) |
| `src/plugin/gui/controls/gui_waveform.cpp` | Extract `DrawSpreadRegionRect` helper; refactor GranularFixed indicator to use it; add spread region drawing from voice markers; consolidate `.Consume()` call |

### 7. Edge cases to handle

- **No active voices in GranularFixed mode**: The spread region is still visible (computed from params, not voice markers). This is the current behaviour.
- **No active voices in GranularPlayback mode**: No spread region is drawn (there's no playhead position to anchor it to). This is correct.
- **Spread larger than the loop**: The entire loop is highlighted as a single region (spread_size >= loop_size case).
- **Spread exactly at loop boundary**: `region_end_fp == loop_end` → no wrapping, single region.
- **Playback ended** (voice marker intensity is 0): Skip drawing — handled by the `if (!marker.intensity)` check.
- **Multiple voices on the same layer**: Each voice gets its own spread region overlay. The translucent overlays will visually stack, which correctly conveys that multiple voices are spawning grains in potentially different regions.
