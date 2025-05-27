// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <IconsFontAwesome5.h>

#include "foundation/foundation.hpp"

enum class TagSelectionModeAllowed {
    Single, // Only one tag can be selected in this category.
    Multiple, // Multiple tags can be selected in this category.
};

enum class TagCategory {
    SoundSource,
    RealInstrument,
    NumberOfPlayers,
    Material,
    ReverbType,
    MoodPositive,
    MoodNegative,
    MoodMixed,
    MoodThematic,
    Pitch,
    SoundTypeLong,
    SoundTypeShort,
    SoundTypeSequence,
    SoundTypeRole,
    TimbreModulation,
    TimbreRealTone,
    TimbreSynthTechnique,
    TimbreFrequency,
    Genre,
    Count,
};

enum class TagType : u16 {
    Acoustic,
    FieldRecording,
    FoundSounds,
    Hybrid,
    Synthesized,
    Vocal,

    ElectricBass,
    DoubleBass,
    Brass,
    Cello,
    Choir,
    Drums,
    Folk,
    Guitar,
    Keys,
    Organ,
    Percussion,
    Piano,
    PluckedStrings,
    Strings,
    StruckStrings,
    Violin,
    Wind,
    TonalPercussion,

    Solo,
    Ensemble,

    Wood,
    Metal,
    Glass,
    Plastic,
    Rubber,
    Stone,
    Ice,

    SmallRoom,
    LargeRoom,
    Chamber,
    Studio,
    Hall,
    Church,
    Cathedral,
    Unusual,
    OpenAir,

    Bright,
    Dreamy,
    Ethereal,
    Hopeful,
    Nostalgic,
    Peaceful,
    Playful,

    Aggressive,
    Chaotic,
    Dark,
    Disturbing,
    Eerie,
    Melancholic,
    Menacing,
    Tense,

    Bittersweet,
    Hypnotic,
    Mysterious,
    Quirky,
    Subdued,

    Dramatic,
    Dystopian,
    Epic,
    Experimental,
    Fantasy,
    Heroic,
    Noir,
    Romantic,
    SciFi,
    Western,

    MultiPitched,
    NonPitched,
    Dissonant,

    Pad,
    Texture,
    Soundscape,
    Underscore,
    Noise,

    Hit,
    // Keys,
    Oneshot,
    // Percussion,
    Pluck,
    Stab,

    Arp,
    Sequence,
    Loop,

    Lead,
    Bass,
    Riser,
    Downer,
    SoundFx,
    Transition,

    Pulsing,
    Evolving,
    Glitched,
    Grainy,

    Brassy,
    Breathy,
    StringsLike,
    Reedy,

    Analog,
    Fm,
    Granular,
    PhysicalModelling,

    Airy,
    CircuitBent,
    Cold,
    Digital,
    Distorted,
    Fuzzy,
    Glassy,
    Gritty,
    Harsh,
    LoFi,
    Lush,
    Metallic,
    Muddy,
    Muffled,
    Nasal,
    Noisy,
    Pure,
    Resonant,
    Saturated,
    Smooth,
    Thin,
    Warm,

    Eighties,
    EDM,
    IDM,
    Ambient,
    Blues,
    Chillout,
    Chiptune,
    Cinematic,
    Disco,
    Downtempo,
    DrumAndBass,
    Dubstep,
    Electronic,
    // Folk,
    Funk,
    FutureBass,
    Glitch,
    HipHop,
    House,
    Industrial,
    Jazz,
    // LoFi,
    // Metal,
    Orchestral,
    Pop,
    Rock,
    Synthwave,
    Techno,
    Trance,
    Trap,
    Vaporwave,
    World,
};

struct Tag {
    TagType tag;
    String description;
};

struct TagCategoryInfo {
    String name;
    TagSelectionModeAllowed selection_mode;
    String question;
    String recommendation;
    Span<Tag const> tags;
    String emoji;
    String font_awesome_icon;
};

PUBLIC String TagText(TagType t) {
    switch (t) {
        case TagType::Acoustic: return "acoustic"_s;
        case TagType::FieldRecording: return "field recording"_s;
        case TagType::FoundSounds: return "found sounds"_s;
        case TagType::Hybrid: return "hybrid"_s;
        case TagType::Synthesized: return "synthesized"_s;
        case TagType::Vocal: return "vocal"_s;

        case TagType::ElectricBass: return "electric bass"_s;
        case TagType::DoubleBass: return "double bass"_s;
        case TagType::Brass: return "brass"_s;
        case TagType::Cello: return "cello"_s;
        case TagType::Choir: return "choir"_s;
        case TagType::Drums: return "drums"_s;
        case TagType::Folk: return "folk"_s;
        case TagType::Guitar: return "guitar"_s;
        case TagType::Keys: return "keys"_s;
        case TagType::Organ: return "organ"_s;
        case TagType::Percussion: return "percussion"_s;
        case TagType::Piano: return "piano"_s;
        case TagType::PluckedStrings: return "plucked strings"_s;
        case TagType::Strings: return "strings"_s;
        case TagType::StruckStrings: return "struck strings"_s;
        case TagType::Violin: return "violin"_s;
        case TagType::Wind: return "wind"_s;
        case TagType::TonalPercussion: return "tonal percussion"_s;

        case TagType::Solo: return "solo"_s;
        case TagType::Ensemble: return "ensemble"_s;

        case TagType::Wood: return "wood"_s;
        case TagType::Metal: return "metal"_s;
        case TagType::Glass: return "glass"_s;
        case TagType::Plastic: return "plastic"_s;
        case TagType::Rubber: return "rubber"_s;
        case TagType::Stone: return "stone"_s;
        case TagType::Ice: return "ice"_s;

        // Reverb types
        case TagType::SmallRoom: return "small room"_s;
        case TagType::LargeRoom: return "large room"_s;
        case TagType::Chamber: return "chamber"_s;
        case TagType::Studio: return "studio"_s;
        case TagType::Hall: return "hall"_s;
        case TagType::Church: return "church"_s;
        case TagType::Cathedral: return "cathedral"_s;
        case TagType::Unusual: return "unusual"_s;
        case TagType::OpenAir: return "open air"_s;

        // Moods
        case TagType::Bright: return "bright"_s;
        case TagType::Dreamy: return "dreamy"_s;
        case TagType::Ethereal: return "ethereal"_s;
        case TagType::Hopeful: return "hopeful"_s;
        case TagType::Nostalgic: return "nostalgic"_s;
        case TagType::Peaceful: return "peaceful"_s;
        case TagType::Playful: return "playful"_s;

        case TagType::Aggressive: return "aggressive"_s;
        case TagType::Chaotic: return "chaotic"_s;
        case TagType::Dark: return "dark"_s;
        case TagType::Disturbing: return "disturbing"_s;
        case TagType::Eerie: return "eerie"_s;
        case TagType::Melancholic: return "melancholic"_s;
        case TagType::Menacing: return "menacing"_s;
        case TagType::Tense: return "tense"_s;

        // Mixed moods
        case TagType::Bittersweet: return "bittersweet"_s;
        case TagType::Hypnotic: return "hypnotic"_s;
        case TagType::Mysterious: return "mysterious"_s;
        case TagType::Quirky: return "quirky"_s;
        case TagType::Subdued: return "subdued"_s;

        // Thematic moods
        case TagType::Dramatic: return "dramatic"_s;
        case TagType::Dystopian: return "dystopian"_s;
        case TagType::Epic: return "epic"_s;
        case TagType::Experimental: return "experimental"_s;
        case TagType::Fantasy: return "fantasy"_s;
        case TagType::Heroic: return "heroic"_s;
        case TagType::Noir: return "noir"_s;
        case TagType::Romantic: return "romantic"_s;
        case TagType::SciFi: return "sci-fi"_s;
        case TagType::Western: return "western"_s;

        // Pitch
        case TagType::MultiPitched: return "multi-pitched"_s;
        case TagType::NonPitched: return "non-pitched"_s;
        case TagType::Dissonant: return "dissonant"_s;

        // Sound types (long duration)
        case TagType::Pad: return "pad"_s;
        case TagType::Texture: return "texture"_s;
        case TagType::Soundscape: return "soundscape"_s;
        case TagType::Underscore: return "underscore"_s;
        case TagType::Noise: return "noise"_s;

        // Sound types (short duration)
        case TagType::Hit: return "hit"_s;
        // Keys,
        case TagType::Oneshot: return "oneshot"_s;
        // Percussion,
        case TagType::Pluck: return "pluck"_s;
        case TagType::Stab: return "stab"_s;

        // Sound types (sequence or pattern)
        case TagType::Arp: return "arp"_s;
        case TagType::Sequence: return "sequence"_s;
        case TagType::Loop: return "loop"_s;

        // Sound types (role in a track)
        case TagType::Lead: return "lead"_s;
        case TagType::Bass: return "bass"_s;
        case TagType::Riser: return "riser"_s;
        case TagType::Downer: return "downer"_s;
        case TagType::SoundFx: return "sound fx"_s;
        case TagType::Transition: return "transition"_s;

        // Timbre modulation
        case TagType::Pulsing: return "pulsing"_s;
        case TagType::Evolving: return "evolving"_s;
        case TagType::Glitched: return "glitched"_s;
        case TagType::Grainy: return "grainy"_s;

        // Timbre (real instrument tone)
        case TagType::Brassy: return "brassy"_s;
        case TagType::Breathy: return "breathy"_s;
        case TagType::StringsLike: return "strings-like"_s;
        case TagType::Reedy: return "reedy"_s;

        // Timbre (synthesis technique)
        case TagType::Analog: return "analog"_s;
        case TagType::Fm: return "FM"_s;
        case TagType::Granular: return "granular"_s;
        case TagType::PhysicalModelling: return "physical modelling"_s;

        // Timbre (frequency)
        case TagType::Airy: return "airy"_s;
        case TagType::CircuitBent: return "circuit bent"_s;
        case TagType::Cold: return "cold"_s;
        case TagType::Digital: return "digital"_s;
        case TagType::Distorted: return "distorted"_s;
        case TagType::Fuzzy: return "fuzzy"_s;
        case TagType::Glassy: return "glassy"_s;
        case TagType::Gritty: return "gritty"_s;
        case TagType::Harsh: return "harsh"_s;
        case TagType::LoFi: return "lo-fi"_s;
        case TagType::Lush: return "lush"_s;
        case TagType::Metallic: return "metallic"_s;
        case TagType::Muddy: return "muddy"_s;
        case TagType::Muffled: return "muffled"_s;
        case TagType::Nasal: return "nasal"_s;
        case TagType::Noisy: return "noisy"_s; // Note that this is not the same as noise.
        case TagType::Pure: return "pure"_s;
        case TagType::Resonant: return "resonant"_s;
        case TagType::Saturated: return "saturated"_s;
        case TagType::Smooth: return "smooth"_s;
        case TagType::Thin: return "thin"_s;
        case TagType::Warm: return "warm"_s;

        // Genres
        case TagType::Eighties: return "80s"_s;
        case TagType::EDM: return "EDM"_s;
        case TagType::IDM: return "IDM"_s;
        case TagType::Ambient: return "ambient"_s;
        case TagType::Blues: return "blues"_s;
        case TagType::Chillout: return "chillout"_s;
        case TagType::Chiptune: return "chiptune"_s;
        case TagType::Cinematic: return "cinematic"_s;
        case TagType::Disco: return "disco"_s;
        case TagType::Downtempo: return "downtempo"_s;
        case TagType::DrumAndBass: return "drum & bass"_s;
        case TagType::Dubstep: return "dubstep"_s;
        case TagType::Electronic: return "electronic"_s;
        case TagType::Funk: return "funk"_s;
        case TagType::FutureBass: return "future bass"_s;
        case TagType::Glitch: return "glitch"_s;
        case TagType::HipHop: return "hip-hop"_s;
        case TagType::House: return "house"_s;
        case TagType::Industrial: return "industrial"_s;
        case TagType::Jazz: return "jazz"_s;
        case TagType::Orchestral: return "orchestral"_s;
        case TagType::Pop: return "pop"_s;
        case TagType::Rock: return "rock"_s;
        case TagType::Synthwave: return "synthwave"_s;
        case TagType::Techno: return "techno"_s;
        case TagType::Trance: return "trance"_s;
        case TagType::Trap: return "trap"_s;
        case TagType::Vaporwave: return "vaporwave"_s;
        case TagType::World: return "world"_s;
    }
}

PUBLIC TagCategoryInfo Tags(TagCategory category) {
    using enum TagType;
    switch (category) {
        case TagCategory::SoundSource: {
            static constexpr auto k_tags = ArrayT<Tag>({
                {Acoustic, "Originating from a real instrument"},
                {FieldRecording, "Environmental or location recordings"},
                {FoundSounds, "Real objects not traditionally used for music"},
                {Hybrid, "Combines acoustic/vocal sounds with processed/synthesized elements"},
                {Synthesized, "Generated by algorithms or circuits"},
                {Vocal, "Originating from a human voice"},
            });
            return {
                .name = "Sound source",
                .selection_mode = TagSelectionModeAllowed::Single,
                .question = "Where did the sound come from?",
                .recommendation =
                    "It's strongly recommended to specify a sound-source for instruments and impulse responses. This is sometimes not applicable for presets.",
                .tags = k_tags,
                .emoji = "🔊",
                .font_awesome_icon = ICON_FA_VOLUME_UP,
            };
        }
        case TagCategory::RealInstrument: {
            static constexpr auto k_tags = ArrayT<Tag>({
                {ElectricBass, ""},
                {DoubleBass, ""},
                {Brass, ""},
                {Cello, ""},
                {Choir, ""},
                {Drums, ""},
                {Folk, ""},
                {Guitar, ""},
                {Keys, ""},
                {Organ, ""},
                {Percussion, ""},
                {Piano, ""},
                {PluckedStrings, "Plucked strings such as guitar, harp, mandolin"},
                {Strings, "Bowed strings such as violin, viola, cello, double bass"},
                {StruckStrings, "Struck strings such as hammered dulcimer, santur"},
                {Violin, ""},
                {Wind, ""},
                {TonalPercussion, ""},
            });
            return {
                .name = "Real instrument category",
                .selection_mode = TagSelectionModeAllowed::Single,
                .question = "Does it fit in a real instrument category?",
                .recommendation =
                    "These are mostly relevant for acoustic or hybrid sounds, but can be used for synthesized sounds that emulate these instruments.",
                .tags = k_tags,
                .emoji = "🎻",
                .font_awesome_icon = ICON_FA_GUITAR,
            };
        }
        case TagCategory::NumberOfPlayers: {
            static constexpr auto k_tags = ArrayT<Tag>({
                {Solo, "Single player"},
                {Ensemble, "Multiple players"},
            });
            return {
                .name = "Number of players",
                .selection_mode = TagSelectionModeAllowed::Single,
                .question = "How many players are involved?",
                .recommendation =
                    "This is typically used for acoustic sounds. Synthesized sounds usually don't have this tag.",
                .tags = k_tags,
                .emoji = "👥",
                .font_awesome_icon = ICON_FA_USERS,
            };
        }
        case TagCategory::Material: {
            static constexpr auto k_tags = ArrayT<Tag>({
                {Wood, ""},
                {Metal, ""},
                {Glass, ""},
                {Plastic, ""},
                {Rubber, ""},
                {Stone, ""},
                {Ice, ""},
            });
            return {
                .name = "Material",
                .selection_mode = TagSelectionModeAllowed::Multiple,
                .question = "Is it made from a specific material?",
                .recommendation =
                    "This is typically used for non-standard instruments in the `acoustic` or `found sounds` categories.",
                .tags = k_tags,
                .emoji = "🪵",
                .font_awesome_icon = ICON_FA_TREE,
            };
        }
        case TagCategory::ReverbType: {
            static constexpr auto k_tags = ArrayT<Tag>({
                {SmallRoom, ""},
                {LargeRoom, ""},
                {Chamber, ""},
                {Studio, ""},
                {Hall, ""},
                {Church, ""},
                {Cathedral, ""},
                {Unusual, ""},
                {OpenAir, ""},
            });
            return {
                .name = "Reverb type",
                .selection_mode = TagSelectionModeAllowed::Single,
                .question = "What reverb type is it?",
                .recommendation =
                    "Strongly recommended for impulse responses. Not applicable for instruments or presets.",
                .tags = k_tags,
                .emoji = "🏛️",
                .font_awesome_icon = ICON_FA_LANDMARK,
            };
        }
        case TagCategory::MoodPositive: {
            static constexpr auto k_tags = ArrayT<Tag>({
                {Bright, "Positive, uplifting, clear"},
                {Dreamy, ""},
                {Ethereal, ""},
                {Hopeful, ""},
                {Nostalgic, ""},
                {Peaceful, ""},
                {Playful, ""},
            });
            return {
                .name = "Mood (positive)",
                .selection_mode = TagSelectionModeAllowed::Multiple,
                .question = "What mood does the item evoke?",
                .recommendation =
                    "It's usually recommended to specify moods for synthesised instruments and presets. Not typically used for well-known acoustic instruments.",
                .tags = k_tags,
                .emoji = "🙂",
                .font_awesome_icon = ICON_FA_SMILE,
            };
        }
        case TagCategory::MoodNegative: {
            static constexpr auto k_tags = ArrayT<Tag>({
                {Aggressive, ""},
                {Chaotic, ""},
                {Dark, "Unhappy, dim, unpleasant"},
                {Disturbing, ""},
                {Eerie, ""},
                {Melancholic, ""},
                {Menacing, ""},
                {Tense, ""},
            });
            return {
                .name = "Mood (negative)",
                .selection_mode = TagSelectionModeAllowed::Multiple,
                .question = "What mood does the item evoke?",
                .recommendation =
                    "It's usually recommended to specify moods for synthesised instruments and presets. Not typically used for well-known acoustic instruments.",
                .tags = k_tags,
                .emoji = "😟",
                .font_awesome_icon = ICON_FA_FROWN,
            };
        }
        case TagCategory::MoodMixed: {
            static constexpr auto k_tags = ArrayT<Tag>({
                {Bittersweet, ""},
                {Hypnotic, ""},
                {Mysterious, ""},
                {Quirky, ""},
                {Subdued, ""},
            });
            return {
                .name = "Mood (mixed)",
                .selection_mode = TagSelectionModeAllowed::Multiple,
                .question = "What mood does the item evoke?",
                .recommendation =
                    "It's usually recommended to specify moods for synthesised instruments and presets. Not typically used for well-known acoustic instruments.",
                .tags = k_tags,
                .emoji = "😐",
                .font_awesome_icon = ICON_FA_MEH,
            };
        }
        case TagCategory::MoodThematic: {
            static constexpr auto k_tags = ArrayT<Tag>({
                {Dramatic, ""},
                {Dystopian, ""},
                {Epic, ""},
                {Experimental, ""},
                {Fantasy, ""},
                {Heroic, ""},
                {Noir, ""},
                {Romantic, ""},
                {SciFi, ""},
                {Western, ""},
            });
            return {
                .name = "Mood (thematic)",
                .selection_mode = TagSelectionModeAllowed::Multiple,
                .question = "What mood does the item evoke?",
                .recommendation =
                    "It's usually recommended to specify moods for synthesised instruments and presets. Not typically used for well-known acoustic instruments.",
                .tags = k_tags,
                .emoji = "🎭",
                .font_awesome_icon = ICON_FA_THEATER_MASKS,
            };
        }
        case TagCategory::Pitch: {
            static constexpr auto k_tags = ArrayT<Tag>({
                {MultiPitched, "Contains multiple distinct notes"},
                {NonPitched, "Lacks identifiable musical pitch"},
                {Dissonant, "Contains harmonically clashing pitches"},
            });
            return {
                .name = "Pitch",
                .selection_mode = TagSelectionModeAllowed::Multiple,
                .question = "What are its pitch characteristics?",
                .recommendation =
                    "Useful for non-typical sounds that have notable pitch characteristics. A sound is assumed to possess a musical pitch unless otherwise stated.",
                .tags = k_tags,
                .emoji = "🎶",
                .font_awesome_icon = ICON_FA_MUSIC,
            };
        }
        case TagCategory::SoundTypeLong: {
            static constexpr auto k_tags = ArrayT<Tag>({
                {Pad, "Sustained, pitched background harmonic element"},
                {Texture, "Sustained atmospheric element, typically less of a musical pitch than `pad`"},
                {Soundscape, "Complex and rich sonic environment"},
                {Underscore, "Background tones or sequences for underneath dialogue"},
                {Noise, "Non-pitched and constant, similar to white noise"},
            });
            return {
                .name = "Sound type (long duration)",
                .selection_mode = TagSelectionModeAllowed::Single,
                .question = "What type of sound is it?",
                .recommendation = "",
                .tags = k_tags,
                .emoji = "🎹",
                .font_awesome_icon = ICON_FA_EXPAND,
            };
        }
        case TagCategory::SoundTypeShort: {
            static constexpr auto k_tags = ArrayT<Tag>({
                {Hit, "Single impactful sound with pitch and percussive elements"},
                {Keys, "Piano or keyboard-like"},
                {Oneshot, "Non-looping single sound, typically not for playing chromatically"},
                {Percussion, "Rhythmic element, typically non-pitched"},
                {Pluck, "Short melodic notes"},
                {Stab, "Extra-short melodic notes"},
            });
            return {
                .name = "Sound type (short duration)",
                .selection_mode = TagSelectionModeAllowed::Single,
                .question = "What type of sound is it?",
                .recommendation = "",
                .tags = k_tags,
                .emoji = "↔️",
                .font_awesome_icon = ICON_FA_COMPRESS,
            };
        }
        case TagCategory::SoundTypeSequence: {
            static constexpr auto k_tags = ArrayT<Tag>({
                {Arp, "Arpeggiated pattern of notes"},
                {Sequence, "Sequenced pattern timbre changes"},
                {Loop, "Sampled repeating phrase"},
            });
            return {
                .name = "Sound type (sequence or pattern)",
                .selection_mode = TagSelectionModeAllowed::Single,
                .question = "What type of sound is it?",
                .recommendation = "",
                .tags = k_tags,
                .emoji = "🔁",
                .font_awesome_icon = ICON_FA_SYNC_ALT,
            };
        }
        case TagCategory::SoundTypeRole: {
            static constexpr auto k_tags = ArrayT<Tag>({
                {Lead, "Foreground melodic element"},
                {Bass, "Low-frequency foundation"},
                {Riser, "Building tension element"},
                {Downer, "Descending tension element"},
                {SoundFx, "Special effect sound"},
                {Transition, "Section change element"},
            });
            return {
                .name = "Sound type (role in a track)",
                .selection_mode = TagSelectionModeAllowed::Single,
                .question = "What type of sound is it?",
                .recommendation = "",
                .tags = k_tags,
                .emoji = "🎛️",
                .font_awesome_icon = ICON_FA_LAYER_GROUP,
            };
        }
        case TagCategory::TimbreModulation: {
            static constexpr auto k_tags = ArrayT<Tag>({
                {Pulsing, "Rhythmic modulation"},
                {Evolving, "Changing over time"},
                {Glitched, "Digital error artefacts"},
                {Grainy, "Fine textural irregularities"},
            });
            return {
                .name = "Timbre (modulation)",
                .selection_mode = TagSelectionModeAllowed::Multiple,
                .question = "What is its timbre like?",
                .recommendation = "",
                .tags = k_tags,
                .emoji = "🚂",
                .font_awesome_icon = ICON_FA_WAVE_SQUARE,
            };
        }
        case TagCategory::TimbreRealTone: {
            static constexpr auto k_tags = ArrayT<Tag>({
                {Brassy, "Like brass instruments"},
                {Breathy, "Containing air noise, like wind instruments"},
                {StringsLike, "Characteristic resonance of string instruments"},
                {Reedy, "Characteristic of reed instruments"},
            });
            return {
                .name = "Timbre (real instrument tone)",
                .selection_mode = TagSelectionModeAllowed::Multiple,
                .question = "What is its timbre like?",
                .recommendation = "",
                .tags = k_tags,
                .emoji = "🎷",
                .font_awesome_icon = ICON_FA_DRUM_STEELPAN,
            };
        }
        case TagCategory::TimbreSynthTechnique: {
            static constexpr auto k_tags = ArrayT<Tag>({
                {Analog, "Warm, imprecise, vintage character"},
                {Fm, "Frequency modulation synthesis"},
                {Granular, "Granular synthesis"},
                {PhysicalModelling, "Simulating real-world physics"},
            });
            return {
                .name = "Timbre (synthesis technique)",
                .selection_mode = TagSelectionModeAllowed::Multiple,
                .question = "What is its timbre like?",
                .recommendation = "",
                .tags = k_tags,
                .emoji = "🎚️",
                .font_awesome_icon = ICON_FA_SLIDERS_H,
            };
        }
        case TagCategory::TimbreFrequency: {
            static constexpr auto k_tags = ArrayT<Tag>({
                {Airy, "Open, spacious, light"},
                {CircuitBent, "Characteristic of modified electronic devices"},
                {Cold, "Thin, clinical, lacking warmth"},
                {Digital, "Clean, precise, computer-generated character"},
                {Distorted, "Overdriven, crushed, non-linear"},
                {Fuzzy, "Indistinct, soft-edged, unclear"},
                {Glassy, "Clear, fragile, transparent"},
                {Gritty, "Rough, textured, unpolished"},
                {Harsh, "Abrasive, aggressive high frequencies"},
                {LoFi, "Intentionally degraded quality"},
                {Lush, "Full, rich, densely layered"},
                {Metallic, "Resonant, hard, bright, like metal"},
                {Muddy, "Unclear low-mid frequencies"},
                {Muffled, "Dampened high frequencies"},
                {Nasal, "Strong mid-range resonance"},
                {Noisy,
                 "Contains noise components, imperfect. If the sound is 100% noise use `noise` instead."},
                {Pure, "Free from noise, clean sine-like quality"},
                {Resonant, "Strong resonant peaks in frequency"},
                {Saturated, "Subtly distorted, harmonically enhanced"},
                {Smooth, "Even, consistent, without sharp edges"},
                {Thin, "Lacking in fullness, narrow frequency range"},
                {Warm, "Rich in harmonics, pleasant mid-range"},
            });
            return {
                .name = "Timbre (frequency)",
                .selection_mode = TagSelectionModeAllowed::Multiple,
                .question = "What is its timbre like?",
                .recommendation = "",
                .tags = k_tags,
                .emoji = "💎",
                .font_awesome_icon = ICON_FA_GEM,
            };
        }
        case TagCategory::Genre: {
            static constexpr auto k_tags = ArrayT<Tag>({
                {Eighties, ""},    {EDM, ""},       {IDM, ""},        {Ambient, ""},    {Blues, ""},
                {Chillout, ""},    {Chiptune, ""},  {Cinematic, ""},  {Disco, ""},      {Downtempo, ""},
                {DrumAndBass, ""}, {Dubstep, ""},   {Electronic, ""}, {Folk, ""},       {Funk, ""},
                {FutureBass, ""},  {Glitch, ""},    {HipHop, ""},     {House, ""},      {Industrial, ""},
                {Jazz, ""},        {LoFi, ""},      {Metal, ""},      {Orchestral, ""}, {Pop, ""},
                {Rock, ""},        {Synthwave, ""}, {Techno, ""},     {Trance, ""},     {Trap, ""},
                {Vaporwave, ""},   {World, ""},
            });
            return {
                .name = "Genre",
                .selection_mode = TagSelectionModeAllowed::Multiple,
                .question = "What genres might this item fit best into?",
                .recommendation =
                    "Specifying at least one genre is recommended for all presets and instruments.",
                .tags = k_tags,
                .emoji = "🎵",
                .font_awesome_icon = ICON_FA_MUSIC,
            };
        }
        case TagCategory::Count: break;
    }
    return {};
}
