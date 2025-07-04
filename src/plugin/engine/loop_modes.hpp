// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/sample_library/sample_library.hpp"
#include "common_infrastructure/state/instrument.hpp"

#include "sample_lib_server/sample_library_server.hpp"

enum class LoopBehaviourId : u8 {
    NoLoop,
    BuiltinLoopStandard,
    BuiltinLoopPingPong,
    CustomLoopStandard,
    CustomLoopPingPong,
    MixedLoops,
    MixedNonLoopsAndLoops,
};

struct LoopBehaviour {
    struct Value {
        LoopBehaviourId id;
        Optional<sample_lib::LoopMode> mode;
        String name;
        String short_name;
        String description;
        bool editable;
    };
    Value value;
    String reason;
    bool is_desired;
};

namespace detail {
static LoopBehaviour::Value Behaviour(LoopBehaviourId id) {
    switch (id) {
        case LoopBehaviourId::NoLoop:
            return {
                .id = id,
                .mode = k_nullopt,
                .name = "Loop - Built-in Standard",
                .short_name = "No Loop",
                .description = "No looping will be applied to this instrument.",
                .editable = false,
            };
        case LoopBehaviourId::BuiltinLoopStandard:
            return {
                .id = id,
                .mode = sample_lib::LoopMode::Standard,
                .name = "Loop - Built-in Standard",
                .short_name = "Loop b/standard",
                .description =
                    "Every region in this instrument will use built-in loops in standard wrap-around mode.",
                .editable = false,
            };
        case LoopBehaviourId::BuiltinLoopPingPong:
            return {
                .id = id,
                .mode = sample_lib::LoopMode::PingPong,
                .name = "Loop - Built-in Ping-Pong",
                .short_name = "Loop b/ping-pong",
                .description = "Every region in this instrument will use built-in loops in ping-pong mode.",
                .editable = false,
            };
        case LoopBehaviourId::CustomLoopStandard:
            return {
                .id = id,
                .mode = sample_lib::LoopMode::Standard,
                .name = "Loop - Custom Standard",
                .short_name = "Loop standard",
                .description =
                    "Custom loop points will be applied to this instrument and use standard wrap-around mode.",
                .editable = true,
            };
        case LoopBehaviourId::CustomLoopPingPong:
            return {
                .id = id,
                .mode = sample_lib::LoopMode::PingPong,
                .name = "Loop - Custom Ping-Pong",
                .short_name = "Loop ping-pong",
                .description =
                    "Custom loop points will be applied to this instrument and use ping-pong mode.",
                .editable = true,
            };
        case LoopBehaviourId::MixedLoops:
            return {
                .id = id,
                .mode = k_nullopt,
                .name = "Mixed Loops",
                .short_name = "Mixed Loops",
                .description =
                    "All regions use built-in loops, but some are standard and some are ping-pong.",
                .editable = false,
            };
        case LoopBehaviourId::MixedNonLoopsAndLoops:
            return {
                .id = id,
                .mode = k_nullopt,
                .name = "Mixed Loops and Non-Loops",
                .short_name = "Mixed Loops and Non-Loops",
                .description = "Some regions have built-in loops, some don't.",
                .editable = false,
            };
    }
}
} // namespace detail

PUBLIC LoopBehaviour ActualLoopBehaviour(Instrument const& inst,
                                         param_values::LoopMode desired_loop_mode,
                                         bool volume_envelope_on) {
    using namespace param_values;

    static constexpr String k_mixed_loop_non_loop = "Some regions have built-in loops, some don't.";
    static constexpr String k_no_builtin_loops = "It does not contain built-in loops.";
    static constexpr String k_all_non_customisable = "Its built-in loops cannot be customised.";

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
                "Waveform instruments always use built-in loops.",
                false,
            };

        case InstrumentType::Sampler: {
            if (!volume_envelope_on) {
                return {
                    detail::Behaviour(LoopBehaviourId::NoLoop),
                    "The volume envelope is off.",
                    false,
                };
            }

            auto const& sampled_inst = inst.GetFromTag<InstrumentType::Sampler>()->instrument;
            auto const loop_overview = sampled_inst.loop_overview;

            if (sampled_inst.regions.size == 0) {
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

                    static constexpr String k_default_behaviour =
                        "This is the default behaviour for this instrument.";

                    if (loop_overview.all_loops_mode) {
                        switch (*loop_overview.all_loops_mode) {
                            case sample_lib::LoopMode::Standard:
                                return {
                                    detail::Behaviour(LoopBehaviourId::BuiltinLoopStandard),
                                    k_default_behaviour,
                                    true,
                                };

                            case sample_lib::LoopMode::PingPong:
                                return {
                                    detail::Behaviour(LoopBehaviourId::BuiltinLoopPingPong),
                                    k_default_behaviour,
                                    true,
                                };

                            case sample_lib::LoopMode::Count: PanicIfReached(); break;
                        }
                    }

                    return {
                        detail::Behaviour(LoopBehaviourId::MixedLoops),
                        k_default_behaviour,
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
                            "Some regions cannot use standard wrap-around loops.",
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
                            "Some regions cannot use ping-pong loops.",
                            false,
                        };

                    return {
                        detail::Behaviour(LoopBehaviourId::BuiltinLoopPingPong),
                        {},
                        true,
                    };
                }

                case LoopMode::None: {
                    static constexpr String k_all_require_loops = "It contains regions that require looping.";

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
            return "Let the instrument decide which regions loop and whether they ping pong or not";
        case param_values::LoopMode::BuiltInLoopStandard:
            return "Let the instrument decide which regions loop, but request standard wrap-around looping mode where possible";
        case param_values::LoopMode::BuiltInLoopPingPong:
            return "Let the instrument decide which regions loop, but request ping-pong looping mode where possible";
        case param_values::LoopMode::None: return "No looping will be applied to this instrument";
        case param_values::LoopMode::Standard:
            return "Set custom loop points for the instrument, using standard wrap-around mode";
        case param_values::LoopMode::PingPong:
            return "Set custom loop points for the instrument, using ping-pong mode";
        case param_values::LoopMode::Count: break;
    }
    PanicIfReached();
    return {};
}

PUBLIC bool IsBuiltinLoop(LoopBehaviourId id) {
    switch (id) {
        case LoopBehaviourId::BuiltinLoopStandard:
        case LoopBehaviourId::BuiltinLoopPingPong: return true;
        case LoopBehaviourId::NoLoop:
        case LoopBehaviourId::CustomLoopStandard:
        case LoopBehaviourId::CustomLoopPingPong:
        case LoopBehaviourId::MixedLoops:
        case LoopBehaviourId::MixedNonLoopsAndLoops: return false;
    }
    PanicIfReached();
    return false;
}
