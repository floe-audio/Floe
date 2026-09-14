// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/loop_behaviour.hpp"
#include "common_infrastructure/sample_library/sample_library.hpp"
#include "common_infrastructure/sample_library/server/sample_library_server.hpp"
#include "common_infrastructure/state/instrument.hpp"

namespace detail {
static LoopBehaviour::Value Behaviour(LoopBehaviourId id) {
    switch (id) {
        case LoopBehaviourId::NoLoop:
            return {
                .id = id,
                .mode = k_nullopt,
                .name = "No Loop",
                .short_name = "None",
                .description = "each sound plays straight through without looping",
                .editable = false,
            };
        case LoopBehaviourId::BuiltinLoopStandard:
            return {
                .id = id,
                .mode = sample_lib::LoopMode::Standard,
                .name = "Loop - Built-in Standard",
                .short_name = "Built-in Standard",
                .description =
                    "each sound repeats between loop points that come built into this Instrument, using 'standard' looping that wraps around from the end back to the start",
                .editable = false,
            };
        case LoopBehaviourId::BuiltinLoopPingPong:
            return {
                .id = id,
                .mode = sample_lib::LoopMode::PingPong,
                .name = "Loop - Built-in Ping-Pong",
                .short_name = "Built-in Ping-Pong",
                .description =
                    "each sound repeats between loop points that come built into this Instrument, using 'ping-pong' looping that bounces back and forth",
                .editable = false,
            };
        case LoopBehaviourId::CustomLoopStandard:
            return {
                .id = id,
                .mode = sample_lib::LoopMode::Standard,
                .name = "Loop - Custom Standard",
                .short_name = "Standard",
                .description =
                    "each sound repeats between the loop points you've set on the waveform, using 'standard' looping that wraps around from the end back to the start",
                .editable = true,
            };
        case LoopBehaviourId::CustomLoopPingPong:
            return {
                .id = id,
                .mode = sample_lib::LoopMode::PingPong,
                .name = "Loop - Custom Ping-Pong",
                .short_name = "Ping-pong",
                .description =
                    "each sound repeats between the loop points you've set on the waveform, using 'ping-pong' looping that bounces back and forth",
                .editable = true,
            };
        case LoopBehaviourId::MixedLoops:
            return {
                .id = id,
                .mode = k_nullopt,
                .name = "Mixed Loops",
                .short_name = "Mixed Loops",
                .description =
                    "each sound repeats between loop points that come built into this Instrument, some using 'standard' wrap-around looping and some 'ping-pong'",
                .editable = false,
            };
        case LoopBehaviourId::MixedNonLoopsAndLoops:
            return {
                .id = id,
                .mode = k_nullopt,
                .name = "Mixed Loops and Non-Loops",
                .short_name = "Mixed Loops and Non-Loops",
                .description =
                    "some sounds repeat between loop points that come built into this Instrument, while the rest play straight through",
                .editable = false,
            };
    }
}
} // namespace detail

PUBLIC LoopBehaviour ActualLoopBehaviour(Instrument const& inst,
                                         param_values::LoopMode desired_loop_mode,
                                         bool volume_envelope_on) {
    using namespace param_values;

    static constexpr String k_mixed_loop_non_loop =
        "Some of this Instrument's sounds have built-in loops and some don't.";
    static constexpr String k_no_builtin_loops = "This Instrument doesn't have any built-in loops.";
    static constexpr String k_all_non_customisable = "This Instrument doesn't allow custom loop points.";

    switch (inst.tag) {
        case InstrumentType::None:
            return {
                .value = detail::Behaviour(LoopBehaviourId::NoLoop),
                .reason = {},
                .is_desired = false,
            };

        case InstrumentType::WaveformSynth:
            // For waveform instruments, we only accept 'default' since a waveform doesn't use loop
            // functionality.
            return {
                detail::Behaviour(LoopBehaviourId::BuiltinLoopStandard),
                "Waveform Instruments always use their built-in loop.",
                false,
            };

        case InstrumentType::Sampler: {
            if (!volume_envelope_on) {
                return {
                    detail::Behaviour(LoopBehaviourId::NoLoop),
                    "The volume envelope is off, which disables looping.",
                    false,
                };
            }

            auto const& sampled_inst = inst.GetFromTag<InstrumentType::Sampler>()->instrument;
            auto const loop_overview = sampled_inst.loop_overview;

            if (sampled_inst.category == sample_lib::SamplerCategory::Empty) {
                return {
                    detail::Behaviour(LoopBehaviourId::NoLoop),
                    {},
                    false,
                };
            }

            switch (desired_loop_mode) {
                case LoopMode::InstrumentDefault: {
                    // We don't bother to differentiate between all the possible mixes of loop modes and
                    // non-loops, we just say 'mixed'. This is uncommon and I don't think that level of detail
                    // is useful.
                    if (loop_overview.has_loops && loop_overview.has_non_loops)
                        return {
                            detail::Behaviour(LoopBehaviourId::MixedNonLoopsAndLoops),
                            k_mixed_loop_non_loop,
                            true,
                        };

                    if (!loop_overview.has_loops)
                        return {
                            detail::Behaviour(LoopBehaviourId::NoLoop),
                            k_no_builtin_loops,
                            true,
                        };

                    ASSERT(!loop_overview.has_non_loops);

                    if (loop_overview.all_loops_mode) {
                        switch (*loop_overview.all_loops_mode) {
                            case sample_lib::LoopMode::Standard:
                                return {
                                    detail::Behaviour(LoopBehaviourId::BuiltinLoopStandard),
                                    {},
                                    true,
                                };

                            case sample_lib::LoopMode::PingPong:
                                return {
                                    detail::Behaviour(LoopBehaviourId::BuiltinLoopPingPong),
                                    {},
                                    true,
                                };

                            case sample_lib::LoopMode::Count: PanicIfReached(); break;
                        }
                    }

                    return {
                        detail::Behaviour(LoopBehaviourId::MixedLoops),
                        {},
                        true,
                    };
                }

                case LoopMode::BuiltInLoopStandard: {
                    if (loop_overview.has_loops && loop_overview.has_non_loops)
                        return {
                            detail::Behaviour(LoopBehaviourId::MixedNonLoopsAndLoops),
                            k_mixed_loop_non_loop,
                            false,
                        };

                    if (!loop_overview.has_loops)
                        return {
                            detail::Behaviour(LoopBehaviourId::NoLoop),
                            k_no_builtin_loops,
                            false,
                        };

                    ASSERT(!loop_overview.has_non_loops);

                    if (!loop_overview.all_loops_convertible_to_mode[ToInt(sample_lib::LoopMode::Standard)])
                        return {
                            detail::Behaviour(LoopBehaviourId::MixedLoops),
                            "Some of this Instrument's sounds can't use 'standard' wrap-around loops.",
                            false,
                        };

                    return {
                        detail::Behaviour(LoopBehaviourId::BuiltinLoopStandard),
                        {},
                        true,
                    };
                }

                case LoopMode::BuiltInLoopPingPong: {
                    if (loop_overview.has_loops && loop_overview.has_non_loops)
                        return {
                            detail::Behaviour(LoopBehaviourId::MixedNonLoopsAndLoops),
                            k_mixed_loop_non_loop,
                            false,
                        };

                    if (!loop_overview.has_loops)
                        return {
                            detail::Behaviour(LoopBehaviourId::NoLoop),
                            k_no_builtin_loops,
                            false,
                        };

                    ASSERT(!loop_overview.has_non_loops);

                    if (!loop_overview.all_loops_convertible_to_mode[ToInt(sample_lib::LoopMode::PingPong)])
                        return {
                            detail::Behaviour(LoopBehaviourId::MixedLoops),
                            "Some of this Instrument's sounds can't use 'ping-pong' loops.",
                            false,
                        };

                    return {
                        detail::Behaviour(LoopBehaviourId::BuiltinLoopPingPong),
                        {},
                        true,
                    };
                }

                case LoopMode::None: {
                    static constexpr String k_all_require_loops =
                        "This Instrument's sounds are set to always loop.";

                    if (loop_overview.all_regions_require_looping) {
                        if (loop_overview.all_loops_mode) {
                            switch (*loop_overview.all_loops_mode) {
                                case sample_lib::LoopMode::Standard:
                                    return {
                                        detail::Behaviour(LoopBehaviourId::BuiltinLoopStandard),
                                        k_all_require_loops,
                                        false,
                                    };

                                case sample_lib::LoopMode::PingPong:
                                    return {
                                        detail::Behaviour(LoopBehaviourId::BuiltinLoopPingPong),
                                        k_all_require_loops,
                                        false,
                                    };

                                case sample_lib::LoopMode::Count: PanicIfReached(); break;
                            }
                        }

                        return {
                            detail::Behaviour(LoopBehaviourId::MixedLoops),
                            k_all_require_loops,
                            false,
                        };
                    }

                    return {
                        detail::Behaviour(LoopBehaviourId::NoLoop),
                        {},
                        true,
                    };
                }

                case LoopMode::Standard: {
                    if (!loop_overview.user_defined_loops_allowed) {
                        if (loop_overview.has_loops && loop_overview.has_non_loops)
                            return {
                                detail::Behaviour(LoopBehaviourId::MixedNonLoopsAndLoops),
                                k_all_non_customisable,
                                false,
                            };

                        if (!loop_overview.has_loops && !loop_overview.all_regions_require_looping)
                            return {
                                detail::Behaviour(LoopBehaviourId::NoLoop),
                                k_all_non_customisable,
                                false,
                            };

                        ASSERT(!loop_overview.has_non_loops);

                        if (loop_overview.all_loops_mode) {
                            switch (*loop_overview.all_loops_mode) {
                                case sample_lib::LoopMode::Standard:
                                    return {
                                        detail::Behaviour(LoopBehaviourId::BuiltinLoopStandard),
                                        k_all_non_customisable,
                                        false,
                                    };

                                case sample_lib::LoopMode::PingPong:
                                    return {
                                        detail::Behaviour(LoopBehaviourId::BuiltinLoopPingPong),
                                        k_all_non_customisable,
                                        false,
                                    };

                                case sample_lib::LoopMode::Count: PanicIfReached(); break;
                            }
                        }

                        return {
                            detail::Behaviour(LoopBehaviourId::MixedLoops),
                            k_all_non_customisable,
                            false,
                        };
                    }

                    return {
                        detail::Behaviour(LoopBehaviourId::CustomLoopStandard),
                        {},
                        true,
                    };
                }
                case LoopMode::PingPong: {
                    if (!loop_overview.user_defined_loops_allowed) {
                        if (loop_overview.has_loops && loop_overview.has_non_loops)
                            return {
                                detail::Behaviour(LoopBehaviourId::MixedNonLoopsAndLoops),
                                k_all_non_customisable,
                                false,
                            };

                        if (loop_overview.has_non_loops && !loop_overview.all_regions_require_looping)
                            return {
                                detail::Behaviour(LoopBehaviourId::NoLoop),
                                k_all_non_customisable,
                                false,
                            };

                        ASSERT(loop_overview.has_loops);

                        if (loop_overview.all_loops_mode) {
                            switch (*loop_overview.all_loops_mode) {
                                case sample_lib::LoopMode::Standard:
                                    return {
                                        detail::Behaviour(LoopBehaviourId::BuiltinLoopStandard),
                                        k_all_non_customisable,
                                        false,
                                    };

                                case sample_lib::LoopMode::PingPong:
                                    return {
                                        detail::Behaviour(LoopBehaviourId::BuiltinLoopPingPong),
                                        k_all_non_customisable,
                                        false,
                                    };

                                case sample_lib::LoopMode::Count: PanicIfReached(); break;
                            }
                        }

                        return {
                            detail::Behaviour(LoopBehaviourId::MixedLoops),
                            k_all_non_customisable,
                            false,
                        };
                    }

                    return {
                        detail::Behaviour(LoopBehaviourId::CustomLoopPingPong),
                        {},
                        true,
                    };
                }

                case LoopMode::Count: break;
            }
        }
    }

    PanicIfReached();
    return {};
}

PUBLIC String LoopModeDescription(param_values::LoopMode mode) {
    switch (mode) {
        case param_values::LoopMode::InstrumentDefault:
            return "Let the Instrument decide: each sound loops (or doesn't) however the library author set it up.";
        case param_values::LoopMode::BuiltInLoopStandard:
            return "Use the Instrument's built-in loop points with 'standard' looping, which wraps around from the loop end back to the loop start, wherever possible.";
        case param_values::LoopMode::BuiltInLoopPingPong:
            return "Use the Instrument's built-in loop points with 'ping-pong' looping, which bounces back and forth between them, wherever possible.";
        case param_values::LoopMode::None: return "Don't loop. Each sound plays straight through.";
        case param_values::LoopMode::Standard:
            return "Set your own loop points on the waveform, with 'standard' looping that wraps around from the loop end back to the loop start.";
        case param_values::LoopMode::PingPong:
            return "Set your own loop points on the waveform, with 'ping-pong' looping that bounces back and forth between them.";
        case param_values::LoopMode::Count: break;
    }
    PanicIfReached();
    return {};
}
