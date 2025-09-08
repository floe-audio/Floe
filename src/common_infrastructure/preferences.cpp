// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "preferences.hpp"

#include "os/filesystem.hpp"
#include "os/misc.hpp"
#include "tests/framework.hpp"
#include "utils/json/json_reader.hpp"
#include "utils/logger/logger.hpp"

#include "common_errors.hpp"
#include "descriptors/param_descriptors.hpp"
#include "error_reporting.hpp"

namespace prefs {

static bool IsKeyValid(String key) {
    if (key.size == 0) return false;
    if (key.size > k_max_key_size) return false;
    return true;
}

static bool IsKeyValid(Key const& key) {
    switch (key.tag) {
        case KeyType::GlobalString: return IsKeyValid(key.Get<String>());
        case KeyType::GlobalInt: return true;
        case KeyType::Sectioned: {
            auto const k = key.Get<SectionedKey>();
            if (!IsKeyValid(k.section)) return false;
            switch (k.key.tag) {
                case KeyValueType::String: return IsKeyValid(k.key.Get<String>());
                case KeyValueType::Int: return true;
            }
        }
    }
    PanicIfReached();
    return false;
}

u64 HashKey(Key const& key) {
    switch (key.tag) {
        case KeyType::GlobalString: return Hash(key.Get<String>());
        case KeyType::GlobalInt: return Hash(key.Get<s64>());
        case KeyType::Sectioned: {
            auto const k = key.Get<SectionedKey>();
            auto hash = HashInit();
            HashUpdate(hash, k.section);
            switch (k.key.tag) {
                case KeyValueType::String: HashUpdate(hash, k.key.Get<String>()); break;
                case KeyValueType::Int: HashUpdate(hash, k.key.Get<s64>()); break;
            }
            return hash;
        }
    }
    PanicIfReached();
    return 0;
}

ErrorCodeOr<void> CustomValueToString(Writer writer, ValueUnion const& key, fmt::FormatOptions options) {
    switch (key.tag) {
        case ValueType::String: return ValueToString(writer, key.Get<String>(), options);
        case ValueType::Int: return ValueToString(writer, key.Get<s64>(), options);
        case ValueType::Bool: return ValueToString(writer, key.Get<bool>(), options);
    }
    return k_success;
}

ErrorCodeOr<void> CustomValueToString(Writer writer, Key const& key, fmt::FormatOptions options) {
    switch (key.tag) {
        case KeyType::GlobalString: return ValueToString(writer, key.Get<String>(), options);
        case KeyType::GlobalInt: return ValueToString(writer, key.Get<s64>(), options);
        case KeyType::Sectioned: {
            auto const k = key.Get<SectionedKey>();
            DynamicArrayBounded<char, k_max_key_size> buffer;
            String key_str {};
            switch (k.key.tag) {
                case KeyValueType::String: key_str = k.key.Get<String>(); break;
                case KeyValueType::Int:
                    static_assert(k_max_key_size >= 32);
                    auto const size = fmt::IntToString(k.key.Get<s64>(), buffer.data, {});
                    key_str = String {buffer.data, size};
                    break;
            }

            auto const size = 1 + k.section.size + 2 + key_str.size;
            TRY(PadToRequiredWidthIfNeeded(writer, options, size));
            TRY(writer.WriteChars("["));
            TRY(ValueToString(writer, k.section, options));
            TRY(writer.WriteChars("]."));
            TRY(ValueToString(writer, key_str, options));
            return k_success;
        }
    }
}

PreferencesTable ParsePreferencesFile(String file_data, ArenaAllocator& arena) {
    if (file_data.size == 0) return {};
    ASSERT(file_data.size < k_max_file_size);
    ASSERT(arena.ContainsPointer((u8 const*)file_data.data));

    DynamicHashTable<Key, Value*, HashKey> table {arena, 24};

    Optional<String> section {};

    for (auto line : SplitIterator {.whole = file_data, .token = '\n', .skip_consecutive = true}) {
        line = WhitespaceStrippedStart(line);
        if (line.size == 0 || line[0] == ';') continue;

        if (line[0] == '[') {
            auto maybe_section = WhitespaceStrippedEnd(line);
            if (Last(maybe_section) == ']') {
                maybe_section.RemovePrefix(1);
                maybe_section.RemoveSuffix(1);
                if (IsKeyValid(maybe_section)) {
                    section = maybe_section;
                    continue;
                }
            }
        }

        auto const equals = Find(line, '=');
        if (!equals) continue;

        auto key = WhitespaceStrippedEnd(line.SubSpan(0, *equals));
        if (!IsKeyValid(key)) {
            LogWarning(ModuleName::Preferences, "invalid key");
            continue;
        }

        auto const value_str = WhitespaceStripped(line.SubSpan(*equals + 1));
        if (value_str.size == 0) continue;

        usize num_chars_read = {};

        Value* value = nullptr;
        if (IsEqualToCaseInsensitiveAscii(value_str, "true"_s))
            value = arena.New<Value>(true);
        else if (IsEqualToCaseInsensitiveAscii(value_str, "false"_s))
            value = arena.New<Value>(false);
        else if (auto const v = ParseInt(value_str, ParseIntBase::Decimal, &num_chars_read, false);
                 v && num_chars_read == value_str.size)
            value = arena.New<Value>(*v);
        else
            // We don't need to clone the string here because we've asserted it's already in the arena.
            value = arena.New<Value>(value_str);

        auto const key_int = ParseInt(key, ParseIntBase::Decimal);

        Key full_key {key};
        if (section) {
            full_key = SectionedKey {*section, key};
            if (key_int) full_key = SectionedKey {*section, *key_int};
        } else {
            if (key_int) full_key = (s64)*key_int;
        }

        if (auto v = table.Find(full_key)) {
            bool already_present = false;
            for (auto existing_value = *v; existing_value; existing_value = existing_value->next)
                if (*existing_value == *value) {
                    already_present = true;
                    break;
                }
            if (!already_present) SinglyLinkedListPrepend(*v, value);
        } else
            table.Insert(full_key, value);
    }

    return table.ToOwnedTable();
}

PreferencesTable ParseLegacyPreferencesFile(String file_data, ArenaAllocator& arena) {
    ASSERT(file_data.size < k_max_file_size);
    ASSERT(arena.ContainsPointer((u8 const*)file_data.data));

    struct Parser {
        bool HandleEvent(json::EventHandlerStack& handler_stack, json::Event const& event) {
            Optional<String> folder {};
            if (SetIfMatchingRef(event, "presets_folder", folder)) {
                // The old format only allowed for a single presets folder so we can just set it directly.
                if (path::IsAbsolute(*folder) && IsValidUtf8(*folder))
                    table.InsertGrowIfNeeded(arena, key::k_extra_presets_folder, arena.New<Value>(*folder));
                return true;
            }
            if (json::SetIfMatchingArray(
                    handler_stack,
                    event,
                    "libraries",
                    [this](json::EventHandlerStack& handler_stack, json::Event const& event) {
                        return HandleLibraries(handler_stack, event);
                    }))
                return true;
            if (json::SetIfMatchingObject(
                    handler_stack,
                    event,
                    "default_ccs",
                    [this](json::EventHandlerStack& handler_stack, json::Event const& event) {
                        return HandleLegacyCcToParamMapping(handler_stack, event);
                    }))
                return true;
            if (json::SetIfMatchingObject(
                    handler_stack,
                    event,
                    "gui_settings",
                    [this](json::EventHandlerStack& handler_stack, json::Event const& event) {
                        return HandleGui(handler_stack, event);
                    }))
                return true;
            return false;
        }

        bool HandleLibraries(json::EventHandlerStack& handler_stack, json::Event const& event) {
            if (json::SetIfMatchingObject(
                    handler_stack,
                    event,
                    "",
                    [this](json::EventHandlerStack&, json::Event const& event) {
                        String library_path {};
                        if (json::SetIfMatchingRef(event, "path", library_path)) {
                            if (auto const dir = path::Directory(library_path);
                                dir && path::IsAbsolute(*dir)) {
                                auto existing = table.Find(key::k_extra_libraries_folder);
                                if (existing) {
                                    // skip if already exists
                                    for (auto v = *existing; v; v = v->next)
                                        if (auto const path = v->TryGet<String>();
                                            path && path::Equal(*path, *dir))
                                            return true;

                                    SinglyLinkedListPrepend(*existing, arena.New<Value>(*dir));
                                } else
                                    table.InsertGrowIfNeeded(arena,
                                                             key::k_extra_libraries_folder,
                                                             arena.New<Value>(*dir));
                            }
                            return true;
                        }
                        return false;
                    })) {
                return true;
            }
            return false;
        }

        bool HandleLegacyCcToParamMapping(json::EventHandlerStack& handler_stack, json::Event const& event) {
            if (event.type == json::EventType::ArrayStart) {
                if (auto o = ParseInt(event.key, ParseIntBase::Decimal); o.HasValue()) {
                    auto num = o.Value();
                    if (num > 0 && num <= 127) {
                        cc_num = (u8)num;

                        if (json::SetIfMatchingArray(
                                handler_stack,
                                event,
                                event.key,
                                [&](json::EventHandlerStack&, json::Event const& event) {
                                    if (event.type == json::EventType::String) {
                                        if (auto const result = ParamFromLegacyId(event.string);
                                            result && result->tag == ParamExistance::StillExists) {
                                            auto const key =
                                                SectionedKey {key::section::k_cc_to_param_id_map_section,
                                                              (s64)cc_num};
                                            auto const value =
                                                (s64)k_param_descriptors
                                                    [ToInt(result->GetFromTag<ParamExistance::StillExists>())]
                                                        .id;

                                            auto const existing = table.Find(key);
                                            if (existing) {
                                                for (auto v = *existing; v; v = v->next)
                                                    if (*v == value) return true;
                                                SinglyLinkedListPrepend(*existing, arena.New<Value>(value));
                                            } else {
                                                bool const inserted =
                                                    table.InsertGrowIfNeeded(arena,
                                                                             key,
                                                                             arena.New<Value>(value));
                                                ASSERT(inserted);
                                            }
                                        }
                                        return true;
                                    }
                                    return false;
                                })) {
                            return true;
                        }
                    }
                } else {
                    PanicIfReached();
                }
            }

            return false;
        }

        bool HandleGui(json::EventHandlerStack&, json::Event const& event) {
            // Let's ignore the old UI size because it's not in pixels - it might be logical units on macOS.
            // We can't easily convert that here without information about the screen and the main-thread.
            // Floe has reasonable default for size, and is easily resizable anyway.
            if constexpr (false) {
                u16 gui_size_index {};
                if (SetIfMatching(event, "GUISize", gui_size_index)) {
                    // We used to set the window size based on an index in an array.
                    constexpr s64 k_window_width_presets[] = {580, 690, 800, 910, 1020, 1130, 1240};
                    gui_size_index = Min(gui_size_index, (u16)(ArraySize(k_window_width_presets) - 1));
                    table.InsertGrowIfNeeded(arena,
                                             key::k_window_width,
                                             arena.New<Value>(k_window_width_presets[gui_size_index]));
                    return true;
                }
            }

            s64 s64_value {};
            if (SetIfMatching(event, "KeyboardOctave", s64_value)) {
                table.InsertGrowIfNeeded(arena, key::k_gui_keyboard_octave, arena.New<Value>(s64_value));
                return true;
            }

            bool bool_value {};
            if (SetIfMatching(event, "ShowKeyboard", bool_value)) {
                table.InsertGrowIfNeeded(arena, key::k_show_keyboard, arena.New<Value>(bool_value));
                return true;
            }
            if (SetIfMatching(event, "ShowTooltips", bool_value)) {
                table.InsertGrowIfNeeded(arena, key::k_show_tooltips, arena.New<Value>(bool_value));
                return true;
            }
            if (SetIfMatching(event, "HighContrast", bool_value)) {
                table.InsertGrowIfNeeded(arena, key::k_high_contrast_gui, arena.New<Value>(bool_value));
                return true;
            }
            return false;
        }

        PreferencesTable& table;
        ArenaAllocator& arena;

        u8 cc_num {};
    };

    ArenaAllocatorWithInlineStorage<1000> scratch_arena {PageAllocator::Instance()};

    PreferencesTable table {};

    Parser parser {
        .table = table,
        .arena = arena,
    };

    auto const o = json::Parse(file_data,
                               [&parser](json::EventHandlerStack& handler_stack, json::Event const& event) {
                                   return parser.HandleEvent(handler_stack, event);
                               },
                               scratch_arena,
                               {});
    if (o.HasError()) {
        // If the JSON is invalid we can't do anything about it, we'll just have to start with empty prefs.
        return table;
    }

    return table;
}

ErrorCodeOr<ReadResult> ReadEntirePreferencesFile(String path, ArenaAllocator& arena) {
    LogDebug(ModuleName::Preferences, "Reading preferences file: {}", path);

    auto file = TRY(OpenFile(path,
                             {
                                 .capability = FileMode::Capability::Read,
                                 .win32_share = FileMode::Share::ReadWrite | FileMode::Share::DeleteRename,
                                 .creation = FileMode::Creation::OpenExisting,
                             }));
    TRY(file.Lock({.type = FileLockOptions::Type::Shared}));
    DEFER { auto _ = file.Unlock(); };

    if (TRY(file.FileSize()) > k_max_file_size) return ErrorCode {CommonError::InvalidFileFormat};

    auto const file_last_modified = TRY(file.LastModifiedTimeNsSinceEpoch());
    auto const file_data = TRY(file.ReadWholeFile(arena));

    return ReadResult {
        .file_data = file_data,
        .file_last_modified = file_last_modified,
    };
}

static ErrorCodeOr<void> WriteKeyValLine(KeyValueUnion const& key, ValueUnion const& value, Writer writer) {
    switch (key.tag) {
        case KeyValueType::String: TRY(writer.WriteChars(key.Get<String>())); break;
        case KeyValueType::Int: TRY(writer.WriteChars(fmt::IntToString(key.Get<s64>()))); break;
    }
    TRY(writer.WriteChars(" = "));
    switch (value.tag) {
        case ValueType::String: {
            auto const s = value.Get<String>();
            TRY(writer.WriteChars(s));
            break;
        }
        case ValueType::Int: {
            auto const i = value.Get<s64>();
            TRY(writer.WriteChars(fmt::IntToString(i)));
            break;
        }
        case ValueType::Bool: {
            auto const b = value.Get<bool>();
            TRY(writer.WriteChars(b ? "true"_s : "false"));
            break;
        }
    }
    TRY(writer.WriteChars("\n"));

    return k_success;
}

static ErrorCodeOr<void>
WriteKeyValLineValueList(KeyValueUnion const& key, Value const* value_list, Writer writer) {
    for (auto value = value_list; value; value = value->next)
        TRY(WriteKeyValLine(key, *value, writer));
    return k_success;
}

#define ALLOW_UNSAFE_POINTER_TAGGING                                                                         \
    __attribute__((no_sanitize("pointer-overflow"))) __attribute__((no_sanitize("alignment")))

ErrorCodeOr<void> WritePreferencesTable(PreferencesTable const& table, Writer writer) {
    // We use a 'pointer tagging' technique with value->next to track keys that we have written. This avoids
    // the need to allocate any tracking data.
    static_assert(alignof(Value*) != 1);
    auto const set_dirty = [](Value* value) ALLOW_UNSAFE_POINTER_TAGGING {
        value->next = (Value*)((uintptr)value->next + 1);
    };
    auto const clear_dirty = [](Value* value) ALLOW_UNSAFE_POINTER_TAGGING {
        value->next = (Value*)((uintptr)value->next - 1);
    };
    auto const is_dirty = [](Value* value) ALLOW_UNSAFE_POINTER_TAGGING {
        return !IsAligned(value->next, alignof(Value*));
    };

    // Write sectionless keys first
    for (auto const [key, value_list, _] : table) {
        KeyValueUnion key_value {""_s};
        switch (key.tag) {
            case KeyType::GlobalString: key_value = key.Get<String>(); break;
            case KeyType::GlobalInt: key_value = key.Get<s64>(); break;
            case KeyType::Sectioned: continue;
        }

        TRY(WriteKeyValLineValueList(key_value, value_list, writer));
        set_dirty(value_list);
    }

    // Write sectioned keys
    for (auto const [key_union, value_list, _] : table) {
        if (is_dirty(value_list)) continue;

        String section {};

        switch (key_union.tag) {
            case KeyType::GlobalString:
            case KeyType::GlobalInt: PanicIfReached();
            case KeyType::Sectioned: section = key_union.Get<SectionedKey>().section; break;
        }

        TRY(writer.WriteChars("\n["));
        TRY(writer.WriteChars(section));
        TRY(writer.WriteChars("]\n"));

        // Write all keys in this section
        for (auto const [other_key_union, other_value_list, _] : table) {
            if (is_dirty(other_value_list)) continue;

            switch (other_key_union.tag) {
                case KeyType::GlobalString:
                case KeyType::GlobalInt: PanicIfReached();
                case KeyType::Sectioned: break;
            }

            auto const [other_section, other_key] = other_key_union.Get<SectionedKey>();
            if (section != other_section) continue;

            TRY(WriteKeyValLineValueList(other_key, other_value_list, writer));
            set_dirty(other_value_list);
        }
    }

    // Un-dirty
    for (auto const [key, value_list, _] : table) {
        ASSERT(is_dirty(value_list));
        clear_dirty(value_list);
    }

    return k_success;
}

ErrorCodeOr<void>
WritePreferencesFile(PreferencesTable const& table, String path, Optional<s128> set_last_modified) {
    LogDebug(ModuleName::Preferences, "Writing preferences file: {}", path);
    auto file = TRY(OpenFile(path,
                             {
                                 .capability = FileMode::Capability::Write,
                                 .win32_share = FileMode::Share::ReadWrite,
                                 .creation = FileMode::Creation::CreateAlways,
                                 .everyone_read_write = true,
                             }));

    TRY(file.Lock({.type = FileLockOptions::Type::Exclusive}));
    DEFER { auto _ = file.Unlock(); };

    BufferedWriter<Kb(4)> buffered_writer {
        .unbuffered_writer = file.Writer(),
    };
    DEFER { buffered_writer.Reset(); };
    TRY(WritePreferencesTable(table, buffered_writer.Writer()));

    TRY(buffered_writer.Flush());
    TRY(file.Flush());
    if (set_last_modified) TRY(file.SetLastModifiedTimeNsSinceEpoch(*set_last_modified));

    return k_success;
}

Optional<s64> LookupInt(PreferencesTable const& table, Key const& key) {
    if (auto const v = table.Find(key)) {
        if ((*v)->tag == ValueType::Int) return (*v)->Get<s64>();
    }
    return k_nullopt;
}

Optional<bool> LookupBool(PreferencesTable const& table, Key const& key) {
    if (auto const v = table.Find(key)) {
        if ((*v)->tag == ValueType::Bool) return (*v)->Get<bool>();
    }
    return k_nullopt;
}

Optional<String> LookupString(PreferencesTable const& table, Key const& key) {
    if (auto const v = table.Find(key)) {
        if ((*v)->tag == ValueType::String) return (*v)->Get<String>();
    }
    return k_nullopt;
}

Value const* LookupValues(PreferencesTable const& table, Key const& key) {
    if (auto const v = table.Find(key)) return *v;
    return nullptr;
}

ValidateResult ValidatedOrDefault(ValueUnion const& value, Descriptor const& descriptor) {
    if (value.tag != descriptor.value_requirements.tag) return {descriptor.default_value, true};
    switch (value.tag) {
        case ValueType::Int: {
            auto const info = descriptor.value_requirements.Get<Descriptor::IntRequirements>();
            auto val = value.Get<s64>();
            if (info.validator && !info.validator(val)) return {descriptor.default_value, true};
            return {val, val == descriptor.default_value.Get<s64>()};
        }
        case ValueType::Bool: return {value, false};
        case ValueType::String: {
            auto const info = descriptor.value_requirements.Get<Descriptor::StringRequirements>();
            auto val = value.Get<String>();
            if (info.validator && !info.validator(val)) return {descriptor.default_value, true};
            return {val, val == descriptor.default_value.Get<String>()};
        }
    }
}

ValidateResult GetValue(PreferencesTable const& table, Descriptor const& descriptor) {
    auto const v_ptr = table.Find(descriptor.key);
    if (!v_ptr) return {descriptor.default_value, true};
    auto const& v = **v_ptr;
    return ValidatedOrDefault(v, descriptor);
}

bool GetBool(PreferencesTable const& table, Descriptor const& descriptor) {
    ASSERT(descriptor.value_requirements.tag == ValueType::Bool);
    return GetValue(table, descriptor).value.Get<bool>();
}

s64 GetInt(PreferencesTable const& table, Descriptor const& descriptor) {
    ASSERT(descriptor.value_requirements.tag == ValueType::Int);
    return GetValue(table, descriptor).value.Get<s64>();
}

String GetString(PreferencesTable const& table, Descriptor const& descriptor) {
    ASSERT(descriptor.value_requirements.tag == ValueType::String);
    return GetValue(table, descriptor).value.Get<String>();
}

template <typename Type>
static usize GetValuesTemplate(PreferencesTable const& table, Descriptor const& descriptor, Span<Type> out) {
    auto const v_ptr = table.Find(descriptor.key);
    if (!v_ptr) return 0;
    if (!*v_ptr) return 0;
    auto const& v = **v_ptr;

    usize pos = 0;
    for (auto node = &v; node; node = node->next) {
        auto const validated = ValidatedOrDefault(*node, descriptor);
        if (validated.is_default) continue;
        if constexpr (Same<Type, ValueUnion>)
            out[pos++] = validated.value;
        else
            out[pos++] = validated.value.Get<Type>();
        if (pos == out.size) break;
    }

    return pos;
}

usize GetValues(PreferencesTable const& table, Descriptor const& descriptor, Span<ValueUnion> out) {
    return GetValuesTemplate(table, descriptor, out);
}
usize GetValues(PreferencesTable const& table, Descriptor const& descriptor, Span<bool> out) {
    return GetValuesTemplate(table, descriptor, out);
}
usize GetValues(PreferencesTable const& table, Descriptor const& descriptor, Span<s64> out) {
    return GetValuesTemplate(table, descriptor, out);
}
usize GetValues(PreferencesTable const& table, Descriptor const& descriptor, Span<String> out) {
    return GetValuesTemplate(table, descriptor, out);
}

Optional<ValueUnion> Match(Key const& key, Value const* value_list, Descriptor const& descriptor) {
    if (key != descriptor.key) return k_nullopt;

    // If the key matches, but the value is nullptr, the value was removed, we should return the default
    // value.
    if (value_list == nullptr) return descriptor.default_value;

    // Otherwise, validate and constrain the value.
    return ValidatedOrDefault(*value_list, descriptor).value;
}

Optional<bool> MatchBool(Key const& key, Value const* value_list, Descriptor const& descriptor) {
    ASSERT(descriptor.value_requirements.tag == ValueType::Bool);
    if (auto const v = Match(key, value_list, descriptor)) return v->Get<bool>();
    return k_nullopt;
}

Optional<s64> MatchInt(Key const& key, Value const* value_list, Descriptor const& descriptor) {
    ASSERT(descriptor.value_requirements.tag == ValueType::Int);
    if (auto const v = Match(key, value_list, descriptor)) return v->Get<s64>();
    return k_nullopt;
}

Optional<String> MatchString(Key const& key, Value const* value_list, Descriptor const& descriptor) {
    ASSERT(descriptor.value_requirements.tag == ValueType::String);
    if (auto const v = Match(key, value_list, descriptor)) return v->Get<String>();
    return k_nullopt;
}

template <typename Type>
static Optional<usize>
MatchValuesTemplate(Key const& key, Value const* value_list, Descriptor const& descriptor, Span<Type> out) {
    if (key != descriptor.key) return k_nullopt;

    usize pos = 0;
    for (auto node = value_list; node; node = node->next) {
        auto const validated = ValidatedOrDefault(*node, descriptor);
        if (validated.is_default) continue;
        if constexpr (Same<Type, ValueUnion>)
            out[pos++] = validated.value;
        else
            out[pos++] = validated.value.Get<Type>();
        if (pos == out.size) break;
    }

    return pos;
}

Optional<usize>
MatchValues(Key const& key, Value const* value_list, Descriptor const& descriptor, Span<ValueUnion> out) {
    return MatchValuesTemplate(key, value_list, descriptor, out);
}
Optional<usize>
MatchValues(Key const& key, Value const* value_list, Descriptor const& descriptor, Span<bool> out) {
    return MatchValuesTemplate(key, value_list, descriptor, out);
}
Optional<usize>
MatchValues(Key const& key, Value const* value_list, Descriptor const& descriptor, Span<String> out) {
    return MatchValuesTemplate(key, value_list, descriptor, out);
}
Optional<usize>
MatchValues(Key const& key, Value const* value_list, Descriptor const& descriptor, Span<s64> out) {
    return MatchValuesTemplate(key, value_list, descriptor, out);
}

static void MarkChanged(Preferences& prefs, Key const& key, Value const* value, auto const& options) {
    if (!options.dont_mark_as_changed) prefs.write_to_file_needed = true;
    if (!options.dont_send_on_change_event)
        if (prefs.on_change) prefs.on_change(key, value);
}

static ValueUnion CloneValueUnion(ValueUnion const& v, PathPool& pool, ArenaAllocator& arena) {
    ValueUnion result = v;
    if (v.tag == ValueType::String) result = pool.Clone(v.Get<String>(), arena);
    return result;
}

static void FreeValueUnion(ValueUnion const& v, PathPool& pool) {
    if (v.tag == ValueType::String) pool.Free(v.Get<String>());
}

static Key CloneKey(Key const& k, PathPool& pool, ArenaAllocator& arena) {
    switch (k.tag) {
        case KeyType::GlobalString: return pool.Clone(k.Get<String>(), arena);
        case KeyType::GlobalInt: return k.Get<s64>();
        case KeyType::Sectioned: {
            SectionedKey result = k.Get<SectionedKey>();
            result.section = pool.Clone(result.section, arena);
            switch (result.key.tag) {
                case KeyValueType::String: result.key = pool.Clone(result.key.Get<String>(), arena); break;
                case KeyValueType::Int: break;
            }
        }
    }
    PanicIfReached();
    return ""_s;
}

static void FreeKey(Key const& k, PathPool& pool) {
    switch (k.tag) {
        case KeyType::GlobalString: pool.Free(k.Get<String>()); break;
        case KeyType::GlobalInt: break;
        case KeyType::Sectioned: {
            auto const sectioned_key = k.Get<SectionedKey>();
            pool.Free(sectioned_key.section);
            switch (sectioned_key.key.tag) {
                case KeyValueType::String: pool.Free(sectioned_key.key.Get<String>()); break;
                case KeyValueType::Int: break;
            }
            break;
        }
    }
}

static Value* AllocateValue(Preferences& prefs, ValueUnion const& value) {
    if (prefs.free_values) {
        auto result = prefs.free_values;
        prefs.free_values = prefs.free_values->next;
        *result = {value};
        return result;
    }
    return prefs.arena.New<Value>(value);
}

static void AddValueNodeToFreeList(Preferences& prefs, Value* value) {
    value->next = prefs.free_values;
    prefs.free_values = value;
}

void SetValue(Preferences& prefs, Key const& key, ValueUnion const& value, SetValueOptions options) {
    ASSERT(IsKeyValid(key));
    Value* new_value {};

    auto const existing_values_ptr = prefs.Find(key);
    if (existing_values_ptr) {
        auto const existing_values = *existing_values_ptr;
        ASSERT(existing_values);

        // If there's only one value and it's the same as the new value, we don't need to do anything.
        if (existing_values->next == nullptr && *existing_values == value) return;

        // Free the content of all existing values
        for (auto node = existing_values; node;) {
            FreeValueUnion(*node, prefs.path_pool);

            // We will reuse the first node, but for the others, we should add them to the free_values
            // list.
            if (node == existing_values) {
                node = node->next;
            } else {
                auto next = node->next;
                AddValueNodeToFreeList(prefs, node);
                node = next;
            }
        }

        // We can reuse the first node.
        *existing_values = {CloneValueUnion(value, prefs.path_pool, prefs.arena)};
        new_value = existing_values;
    } else {
        if (options.overwrite_only) return;

        new_value = AllocateValue(prefs, CloneValueUnion(value, prefs.path_pool, prefs.arena));

        auto const inserted = prefs.InsertGrowIfNeeded(
            prefs.arena,
            options.clone_key_string ? CloneKey(key, prefs.path_pool, prefs.arena) : key,
            new_value);
        ASSERT(inserted);
    }

    MarkChanged(prefs, key, new_value, options);
}

void SetValue(Preferences& prefs,
              Descriptor const& descriptor,
              ValueUnion const& value,
              SetValueOptions options) {
    auto const [v, is_default] = ValidatedOrDefault(value, descriptor);
    // If the value is default, we don't need to write it unless it already exists in the file. If it already
    // exists in the file it might have been set deliberately by the user. Whatever the reason, it's a
    // stronger intention than deferring to the default value, and we should explicitly signal that it has a
    // new value.
    if (is_default) options.overwrite_only = true;
    SetValue(prefs, descriptor.key, v, options);
}

bool AddValue(Preferences& prefs, Key const& key, ValueUnion const& value, SetValueOptions options) {
    ASSERT(IsKeyValid(key));
    Value* first_node {};

    auto existing_values_ptr = prefs.Find(key);
    if (existing_values_ptr) {
        auto const existing_values = *existing_values_ptr;
        ASSERT(existing_values);

        // Skip if the value already exists
        for (auto v = existing_values; v; v = v->next)
            if (*v == value) return false;

        auto last = existing_values;
        for (; last->next; last = last->next)
            if (*last == value) return false;
        ASSERT(last->next == nullptr);

        last->next = AllocateValue(prefs, CloneValueUnion(value, prefs.path_pool, prefs.arena));
        first_node = existing_values;
    } else {
        if (options.overwrite_only) return false;

        first_node = AllocateValue(prefs, CloneValueUnion(value, prefs.path_pool, prefs.arena));
        auto const inserted = prefs.InsertGrowIfNeeded(
            prefs.arena,
            options.clone_key_string ? CloneKey(key, prefs.path_pool, prefs.arena) : key,
            first_node);
        ASSERT(inserted);
    }

    MarkChanged(prefs, key, first_node, options);
    return true;
}

void AddValue(Preferences& preferences,
              Descriptor const& descriptor,
              ValueUnion const& value,
              SetValueOptions options) {
    auto const [v, is_default] = ValidatedOrDefault(value, descriptor);

    // The default_value of a descriptor in a multi-value situation isn't really a default value, it's just a
    // marker that the value is not set. We should not add it.
    if (is_default) return;

    AddValue(preferences, descriptor.key, v, options);
}

bool RemoveValue(Preferences& prefs, Key const& key, ValueUnion const& value, RemoveValueOptions options) {
    ASSERT(IsKeyValid(key));
    auto const existing_element = prefs.FindElement(key);
    if (!existing_element) return false;
    auto& existing_values = existing_element->data;

    bool removed = false;
    SinglyLinkedListRemoveIf(
        existing_values,
        [&](ValueUnion const& node) { return node == value; },
        [&](Value* node) {
            FreeValueUnion(*node, prefs.path_pool);
            AddValueNodeToFreeList(prefs, node);
            removed = true;
        });

    if (removed) {
        if (existing_values == nullptr) {
            FreeKey(key, prefs.path_pool);
            prefs.DeleteElement(existing_element);
            MarkChanged(prefs, key, nullptr, options);
        } else {
            MarkChanged(prefs, key, existing_values, options);
        }
    }

    return removed;
}

void Remove(Preferences& prefs, Key const& key, RemoveValueOptions options) {
    ASSERT(IsKeyValid(key));
    auto existing = prefs.Find(key);
    if (!existing) return;

    {
        for (auto node = *existing; node;) {
            FreeValueUnion(*node, prefs.path_pool);
            auto next = node->next;
            node->next = prefs.free_values;
            prefs.free_values = node;
            node = next;
        }
        FreeKey(key, prefs.path_pool);
        prefs.Delete(key);
    }

    MarkChanged(prefs, key, nullptr, options);
}

Optional<usize> Init(Preferences& prefs, Span<String const> possible_paths) {
    ASSERT(prefs.size == 0, "already init");

    Optional<usize> path_index_loaded {};
    for (auto const [index, path] : Enumerate(possible_paths)) {
        auto const read_result = TRY_OR(ReadEntirePreferencesFile(path, prefs.arena), {
            if (error == FilesystemError::PathDoesNotExist || error == FilesystemError::AccessDenied ||
                error == FilesystemError::UsedByAnotherProcess)
                continue;
            LogWarning(ModuleName::Preferences, "failed to read preferences file: {}", error);
            continue;
        });
        prefs.last_known_file_modified_time = read_result.file_last_modified;

        if (path::Equal(path::Extension(path), ".json"_s))
            (PreferencesTable&)prefs = ParseLegacyPreferencesFile(read_result.file_data, prefs.arena);
        else
            (PreferencesTable&)prefs = ParsePreferencesFile(read_result.file_data, prefs.arena);
        path_index_loaded = index;
        break;
    }

    auto watcher = CreateDirectoryWatcher(prefs.watcher_arena);
    if (watcher.HasValue())
        prefs.watcher.Emplace(watcher.ReleaseValue());
    else
        ReportError(ErrorLevel::Warning,
                    SourceLocationHash(),
                    "failed to create preferences directory watcher: {}",
                    watcher.Error());

    return path_index_loaded;
}

void Deinit(Preferences& prefs) {
    prefs.on_change = {};
    if (prefs.watcher.HasValue()) DestoryDirectoryWatcher(prefs.watcher.Value());
}

void WriteIfNeeded(Preferences& prefs) {
    if (!prefs.write_to_file_needed) return;

    prefs.last_known_file_modified_time = NanosecondsSinceEpoch();
    TRY_OR(WritePreferencesFile(prefs, PreferencesFilepath(), prefs.last_known_file_modified_time), {
        ReportError(ErrorLevel::Error, SourceLocationHash(), "failed to write preferences file: {}", error);
    });

    prefs.write_to_file_needed = false;
}

void ReplacePreferences(Preferences& prefs, PreferencesTable const& new_table, ReplaceOptions options) {
    if (options.remove_keys_not_in_new_table) {
        for (auto const [key, value, _] : prefs)
            if (!new_table.Find(key)) Remove(prefs, key);
    }

    for (auto const [key, new_value_list, _] : new_table) {

        bool changed = false;

        if (auto existing_value_list_ptr = prefs.Find(key)) {
            auto const existing_value_list = *existing_value_list_ptr;

            // Add new values that don't already exist.
            for (auto new_v = new_value_list; new_v; new_v = new_v->next)
                if (AddValue(prefs,
                             key,
                             *new_v,
                             {.clone_key_string = true, .dont_send_on_change_event = true}))
                    changed = true;

            // Remove all old values that no longer exist.
            for (auto old_v = existing_value_list; old_v;) {
                auto const next = old_v->next;
                DEFER { old_v = next; };

                bool exists_in_new_values = false;
                for (auto new_v = new_value_list; new_v; new_v = new_v->next) {
                    if (*(ValueUnion const*)old_v == *(ValueUnion const*)new_v) {
                        exists_in_new_values = true;
                        break;
                    }
                }
                if (!exists_in_new_values) {
                    RemoveValue(prefs, key, *old_v, {.dont_send_on_change_event = true});
                    changed = true;
                }
            }
        } else {
            for (auto v = new_value_list; v; v = v->next)
                AddValue(prefs, key, *v, {.clone_key_string = true, .dont_send_on_change_event = true});
            changed = true;
        }

        // To avoid sending a change event for every key-value pair, we only send one event per key.
        if (changed) MarkChanged(prefs, key, *prefs.Find(key), SetValueOptions {});
    }
}

void PollForExternalChanges(Preferences& prefs, PollForExternalChangesOptions options) {
    if (!prefs.watcher) return;

    if (prefs.write_to_file_needed) {
        // We ignore external changes if we have unsaved changes ourselves - our changes are probably more
        // recent.
        return;
    }

    if (!options.ignore_rate_limiting)
        if ((prefs.last_watcher_poll_time + k_file_watcher_poll_interval_seconds) > TimePoint::Now()) return;
    DEFER { prefs.last_watcher_poll_time = TimePoint::Now(); };

    auto const path = PreferencesFilepath();
    auto const dir = *path::Directory(path);

    DirectoryToWatch const watch_dir {
        .path = dir,
        .recursive = false,
    };
    auto const changes = TRY_OR(PollDirectoryChanges(prefs.watcher.Value(),
                                                     {
                                                         .dirs_to_watch = Array {watch_dir},
                                                         .result_arena = prefs.watcher_scratch,
                                                         .scratch_arena = prefs.watcher_scratch,
                                                     }),
                                {
                                    ReportError(ErrorLevel::Warning,
                                                SourceLocationHash(),
                                                "failed to poll for preferences changes: {}",
                                                error);
                                });
    DEFER { prefs.watcher_scratch.ResetCursorAndConsolidateRegions(); };

    for (auto const& change : changes) {
        for (auto const& subpath : change.subpath_changesets) {
            if (!path::Equal(subpath.subpath, path::Filename(path))) continue;

            // We ignore changes that are older or the same as our last known modification time.
            auto const file_last_modified = TRY_OR(LastModifiedTimeNsSinceEpoch(path), return);
            if (file_last_modified <= prefs.last_known_file_modified_time) continue;

            // We need to apply the new prefs to our existing prefs. If we have a key that doesn't exist
            // in the new table, we keep our existing key-value, but for all keys that exist in the new table,
            // we update our values to exactly match the new table.
            auto const read_result = TRY_OR(ReadEntirePreferencesFile(path, prefs.watcher_scratch), return);
            auto const new_table = ParsePreferencesFile(read_result.file_data, prefs.watcher_scratch);

            ReplacePreferences(prefs, new_table, {.remove_keys_not_in_new_table = true});

            // We just loaded fresh data from the file, so we can mark that we don't need to write to the
            // file.
            prefs.last_known_file_modified_time = read_result.file_last_modified;
            prefs.write_to_file_needed = false;
            return;
        }
    }
}

TEST_CASE(TestLegacyPreferences) {
    DynamicArray<char> json {R"foo({
    "presets_folder": "{ROOT}Presets",
    "use_old_param_mapping": false,
    "dismiss_all_notifications": false,
    "mirage_version": "2.1.0",
    "libraries": [
        {
            "name": "Slow",
            "path": "{ROOT}mdatas/slow.mdata"
        },
        {
            "name": "Wraith",
            "path": "{ROOT}mdatas/wraith.mdata"
        },
        {
            "name": "VMB",
            "path": "{ROOT}other_mdatas/vmb.mdata"
        }
    ],
    "default_ccs": {
        "1": [
            "MastVol"
        ]
    },
    "gui_settings": {
        "DismissedNotfications": [
            "Slow library released",
            "Arctic Strings library released"
        ],
        "GUISize": 3,
        "HideMarketing": true,
        "KeyboardOctave": 0,
        "NewsPrefsDecided": false,
        "PresetRandomMode": 0,
        "ShowKeyboard": true,
        "ShowTooltips": true,
        "HighContrast": false,
        "SortLibsAlpha": false
    }
})foo"_s,
                             tester.scratch_arena};
    auto const root_dir = IS_WINDOWS ? "C:/"_s : "/";
    dyn::Replace(json, "{ROOT}"_s, root_dir);

    auto const table = ParseLegacyPreferencesFile(json, tester.scratch_arena);

    {
        auto const v = LookupValues(table, key::k_extra_libraries_folder);
        REQUIRE(v);
        CHECK(v->next);

        auto const expected_dir_1 = fmt::Format(tester.scratch_arena, "{}mdatas"_s, root_dir);
        auto const expected_dir_2 = fmt::Format(tester.scratch_arena, "{}other_mdatas"_s, root_dir);

        usize num_dirs = 0;
        for (auto i = &*v; i; i = i->next) {
            ++num_dirs;
            ASSERT(num_dirs < 100);
            auto const path = i->Get<String>();
            if (path != expected_dir_1 && path != expected_dir_2) {
                TEST_FAILED("library path wrong: {}", path);
                break;
            }
        }

        CHECK_EQ(num_dirs, 2u);
    }

    CHECK(*LookupBool(table, key::k_show_keyboard));
    CHECK(*LookupBool(table, key::k_show_tooltips));
    CHECK(!*LookupBool(table, key::k_high_contrast_gui));
    CHECK_EQ(*LookupInt(table, key::k_gui_keyboard_octave), 0);
    CHECK_EQ(*LookupInt(table, key::k_window_width), 910);

    CHECK(*LookupString(table, key::k_extra_presets_folder) ==
          fmt::Format(tester.scratch_arena, "{}Presets"_s, root_dir));

    {
        auto const v = LookupValues(table,
                                    SectionedKey {
                                        .section = key::section::k_cc_to_param_id_map_section,
                                        .key = (s64)1,
                                    });
        REQUIRE(v);
        CHECK(!v->next);
        REQUIRE(v->tag == ValueType::Int);
        CHECK_EQ(v->Get<s64>(), k_param_descriptors[ToInt(ParamIndex::MasterVolume)].id);
    }

    return k_success;
}

TEST_CASE(TestPreferences) {
    SUBCASE("read/write basics") {

#if IS_WINDOWS
#define ROOT "C:/"
#else
#define ROOT "/"
#endif

        struct KeyVal {
            Key key;
            ValueUnion value;
        };
        auto const keyvals = ArrayT<KeyVal>({
            {key::k_show_tooltips, true},
            {key::k_gui_keyboard_octave, (s64)0},
            {SectionedKey {.section = "section", .key = "key1"_s}, true},
            {SectionedKey {.section = "section", .key = "key2"_s}, false},
            {key::k_show_keyboard, true},
            {key::k_window_width, (s64)1200},
            {SectionedKey {.section = key::section::k_cc_to_param_id_map_section, .key = (s64)10}, (s64)1},
            {SectionedKey {.section = key::section::k_cc_to_param_id_map_section, .key = (s64)10}, (s64)3},
            {SectionedKey {.section = key::section::k_cc_to_param_id_map_section, .key = (s64)10}, (s64)4},
            {"unknown_key"_s, "unknown value"_s},
            {"unknown_key"_s, "unknown value"_s},
            {key::k_extra_libraries_folder, ROOT "Libraries"_s},
            {key::k_extra_libraries_folder, ROOT "Floe Libraries"_s},
            {key::k_extra_presets_folder, ROOT "Projects/Test"_s},

        });

        DynamicArray<char> file_data {tester.scratch_arena};
        {
            Preferences prefs;
            for (auto const& kv : keyvals)
                AddValue(prefs, kv.key, kv.value, {});
            TRY(WritePreferencesTable(prefs, dyn::WriterFor(file_data)));
            tester.log.Debug("file_data: {}", file_data);
        }

        auto const prefs = ParsePreferencesFile(file_data, tester.scratch_arena);

        for (auto const& kv : keyvals) {
            CAPTURE(kv.key);

            DynamicArrayBounded<ValueUnion, 4> expected_values;
            for (auto const& other_kv : keyvals)
                if (kv.key == other_kv.key) dyn::AppendIfNotAlreadyThere(expected_values, other_kv.value);
            CAPTURE(expected_values.Items());

            auto value_list = LookupValues(prefs, kv.key);
            REQUIRE(value_list);

            usize num_values = 0;
            for (auto value = value_list; value; value = value->next) {
                ++num_values;
                if (!Contains(expected_values, *value)) {
                    TEST_FAILED("value not expected key: {}, value: {}, expected_values: {}",
                                kv.key,
                                *value,
                                expected_values.Items());
                }
            }

            CHECK_EQ(num_values, expected_values.size);
        }

        // Check for some specific values
        CHECK(*LookupBool(prefs, SectionedKey {.section = "section", .key = "key1"_s}) == true);
        CHECK(*LookupBool(prefs, SectionedKey {.section = "section", .key = "key2"_s}) == false);
    }

    SUBCASE("file read/write") {
        auto const filename = tests::TempFilename(tester);
        constexpr String k_file_data = "key1 = value1\nkey2 = value2\n"_s;
        TRY(WriteFile(filename, k_file_data));

        {
            auto const file_data = TRY(ReadEntirePreferencesFile(filename, tester.scratch_arena));
            CHECK_EQ(file_data.file_data, k_file_data);

            auto const table = ParsePreferencesFile(file_data.file_data, tester.scratch_arena);
            CHECK_EQ(table.size, 2u);
            CHECK_EQ(*LookupString(table, "key1"_s), "value1"_s);
            CHECK_EQ(*LookupString(table, "key2"_s), "value2"_s);

            TRY(WritePreferencesFile(table, filename));
        }

        // Read again
        {
            auto const file_data = TRY(ReadEntirePreferencesFile(filename, tester.scratch_arena));
            CHECK_EQ(file_data.file_data, k_file_data);
        }
    }

    SUBCASE("values are recycled") {
        Preferences prefs;
        CHECK(prefs.free_values == nullptr);

        for (auto _ : Range(10)) {
            AddValue(prefs, "key"_s, "value"_s);
            RemoveValue(prefs, "key"_s, "value"_s);
        }

        // There should be exactly one value in the free list.
        REQUIRE(prefs.free_values);
        CHECK(prefs.free_values->next == nullptr);
    }

    static constexpr String k_key = "key";
    static constexpr String k_alpha = "alpha"_s;
    static constexpr String k_beta = "beta"_s;
    static constexpr String k_gamma = "gamma"_s;

    SUBCASE("change listener") {
        Preferences prefs;

        // on_change is a fixed size function object that only has room for one pointer, so we combine our
        // necessary data into a single object.
        struct OnChangeContext {
            tests::Tester& tester;
            DynamicArrayBounded<String, 3> expected_values;
        } callback_ctx {tester, {}};

        prefs.on_change = [&callback_ctx](Key key, Value const* value) {
            auto& tester = callback_ctx.tester;
            CHECK_EQ(key, k_key);
            usize num_values = 0;
            for (auto v = value; v; v = v->next) {
                CAPTURE(v->Get<String>());
                ++num_values;
                // Values are unordered, so we just check that all expected values are present.
                CHECK(Contains(callback_ctx.expected_values, v->Get<String>()));
            }
            CHECK_EQ(num_values, callback_ctx.expected_values.size);
        };

        auto const set_expected_callback = [&](Span<String const> expected_values) {
            callback_ctx.expected_values = expected_values;
        };

        SUBCASE("set") {
            set_expected_callback(Array {k_alpha});
            SetValue(prefs, k_key, k_alpha);

            // replace with a new value
            set_expected_callback(Array {k_beta});
            SetValue(prefs, k_key, k_beta);

            // add a second value
            set_expected_callback(Array {k_beta, k_gamma});
            AddValue(prefs, k_key, k_gamma);
        }

        SUBCASE("simple add and remove") {
            set_expected_callback(Array {k_alpha});
            AddValue(prefs, k_key, k_alpha);

            set_expected_callback({});
            RemoveValue(prefs, k_key, k_alpha);
        }

        SUBCASE("add multiple") {
            set_expected_callback(Array {k_alpha});
            AddValue(prefs, k_key, k_alpha);

            set_expected_callback(Array {k_alpha, k_beta});
            AddValue(prefs, k_key, k_beta);

            SUBCASE("remove alpha first") {
                set_expected_callback(Array {k_beta});
                RemoveValue(prefs, k_key, k_alpha);

                set_expected_callback({});
                RemoveValue(prefs, k_key, k_beta);
            }

            SUBCASE("remove beta first") {
                set_expected_callback(Array {k_alpha});
                RemoveValue(prefs, k_key, k_beta);

                set_expected_callback({});
                RemoveValue(prefs, k_key, k_alpha);
            }

            SUBCASE("remove key") {
                set_expected_callback({});
                Remove(prefs, k_key, {});
            }
        }
    }

    SUBCASE("replace entire prefs") {
        Preferences prefs1;
        Preferences prefs2;

        SUBCASE("one value change") {
            AddValue(prefs1, k_key, k_alpha);
            AddValue(prefs2, k_key, k_beta);

            // We're expecting one on_change callback for the value change.
            prefs1.on_change = [&tester](Key key, Value const* value) {
                CHECK_EQ(key, k_key);
                CHECK_EQ(value->Get<String>(), k_beta);
                CHECK(value->next == nullptr);
            };
            ReplacePreferences(prefs1, prefs2, {});

            CHECK_EQ(prefs1.size, 1u);
            CHECK_EQ(*LookupString(prefs1, k_key), k_beta);
        }

        SUBCASE("multiple values replaced") {
            AddValue(prefs1, k_key, k_alpha);
            AddValue(prefs1, k_key, k_beta);
            AddValue(prefs1, k_key, k_gamma);

            AddValue(prefs2, k_key, k_beta);
            AddValue(prefs2, k_key, k_gamma);

            // We should receive a callback for the key containing 2 values.
            prefs1.on_change = [&tester](Key key, Value const* value) {
                CHECK_EQ(key, k_key);
                usize num_values = 0;
                for (auto v = value; v; v = v->next) {
                    ++num_values;
                    CHECK(Contains(Array {k_beta, k_gamma}, v->Get<String>()));
                }
                CHECK_EQ(num_values, 2u);
            };
            ReplacePreferences(prefs1, prefs2, {});

            CHECK_EQ(prefs1.size, 1u);
            CHECK_EQ(*LookupString(prefs1, k_key), k_beta);
        }

        SUBCASE("new key added") {
            AddValue(prefs2, k_key, k_alpha);

            prefs1.on_change = [&tester](Key key, Value const* value) {
                CHECK_EQ(key, k_key);
                CHECK_EQ(value->Get<String>(), k_alpha);
                CHECK(value->next == nullptr);
            };
            ReplacePreferences(prefs1, prefs2, {});
        }

        SUBCASE("existing keys retain") {
            AddValue(prefs1, k_key, k_alpha);

            AddValue(prefs2, "other_key"_s, k_beta);

            ReplacePreferences(prefs1, prefs2, {.remove_keys_not_in_new_table = false});

            // The existing key should still be present, along with the new key.
            CHECK_EQ(prefs1.size, 2u);
            CHECK_EQ(*LookupString(prefs1, k_key), k_alpha);
            CHECK_EQ(*LookupString(prefs1, "other_key"_s), k_beta);
        }

        SUBCASE("existing keys removed") {
            AddValue(prefs1, k_key, k_alpha);

            AddValue(prefs2, "other_key"_s, k_beta);

            ReplacePreferences(prefs1, prefs2, {.remove_keys_not_in_new_table = true});

            // The existing key should be removed, and the new key should be present.
            CHECK_EQ(prefs1.size, 1u);
            CHECK_EQ(*LookupString(prefs1, "other_key"_s), k_beta);
        }
    }

    SUBCASE("file watcher poll") {
        auto const filepath = PreferencesFilepath();

        // The prefs system only polls the official PreferencesFilepath(), so we need to write to that
        // file. Let's first backup the existing file and restore it when we're done.
        auto const original_prefs_file_data = ({
            Optional<String> d {};
            auto const o = ReadEntireFile(filepath, tester.scratch_arena);
            if (o.HasValue()) d = o.Value();
            d;
        });
        DEFER {
            if (original_prefs_file_data) auto _ = WriteFile(filepath, *original_prefs_file_data);
        };

        constexpr String k_file_data = "key1 = value1\nkey2 = value2\n"_s;
        TRY(WriteFile(filepath, k_file_data));

        Preferences prefs;

        auto const path_index_used = Init(prefs, Array {filepath});
        REQUIRE(path_index_used == 0);
        DEFER { Deinit(prefs); };

        CHECK(!prefs.write_to_file_needed);

        CHECK_EQ(prefs.size, 2u);
        CHECK_EQ(*LookupString(prefs, "key1"_s), "value1"_s);
        CHECK_EQ(*LookupString(prefs, "key2"_s), "value2"_s);

        auto const last_know_modified_time = prefs.last_known_file_modified_time;
        PollForExternalChanges(prefs, {.ignore_rate_limiting = true});
        CHECK(!prefs.write_to_file_needed);
        CHECK_EQ(last_know_modified_time, prefs.last_known_file_modified_time);

        CHECK_EQ(prefs.size, 2u);
        CHECK_EQ(*LookupString(prefs, "key1"_s), "value1"_s);
        CHECK_EQ(*LookupString(prefs, "key2"_s), "value2"_s);

        // Add a new key.
        {
            auto const t1 = NanosecondsSinceEpoch();
            {
                auto f = TRY(OpenFile(filepath,
                                      {
                                          .capability = FileMode::Capability::Append,
                                          .win32_share = FileMode::Share::Read,
                                          .creation = FileMode::Creation::OpenExisting,
                                      }));
                TRY(f.Write("key3 = value3\n"_s));
                TRY(f.Flush());
                // We need to explicitly set the last modified time because otherwise the OS might only update
                // it lazily - and our subsequent check might not see the change.
                TRY(f.SetLastModifiedTimeNsSinceEpoch(NanosecondsSinceEpoch()));
            }
            auto const t2 = TRY(LastModifiedTimeNsSinceEpoch(filepath));
            CAPTURE(t1);
            CAPTURE(t2);
            REQUIRE(t2 > t1);
        }

        for (auto _ : Range(25)) {
            CHECK(!prefs.write_to_file_needed);
            PollForExternalChanges(prefs, {.ignore_rate_limiting = true});
            if (prefs.size != 2) break;
            SleepThisThread(1); // wait for the file watcher to pick up the change
        }

        CHECK_EQ(prefs.size, 3u);
        CHECK_EQ(*LookupString(prefs, "key1"_s), "value1"_s);
        CHECK_EQ(*LookupString(prefs, "key2"_s), "value2"_s);
        CHECK_EQ(*LookupString(prefs, "key3"_s), "value3"_s);

        // Replace the value of key1, remove everything else.
        {
            auto const t1 = NanosecondsSinceEpoch();
            TRY(WriteFile(filepath, "key1 = value4\n"_s));
            TRY(SetLastModifiedTimeNsSinceEpoch(filepath, NanosecondsSinceEpoch()));
            auto const t2 = TRY(LastModifiedTimeNsSinceEpoch(filepath));
            REQUIRE(t2 > t1);
        }

        for (auto _ : Range(25)) {
            PollForExternalChanges(prefs, {.ignore_rate_limiting = true});
            CHECK(!prefs.write_to_file_needed);
            if (LookupString(prefs, "key1"_s) == "value4"_s) break;
            SleepThisThread(1); // wait for the file watcher to pick up the change
        }

        CHECK_EQ(prefs.size, 1u);
        CHECK_EQ(*LookupString(prefs, "key1"_s), "value4"_s);
    }

    SUBCASE("descriptor") {
        Descriptor int_descriptor {
            .key = "key"_s,
            .value_requirements =
                Descriptor::IntRequirements {
                    .validator =
                        [](s64& value) {
                            value = Clamp<s64>(value, 0, 10);
                            return true;
                        },
                },
            .default_value = (s64)5,
            .gui_label = "Key"_s,
            .long_description = "This is a key"_s,
        };
        SUBCASE("int") {
            auto const validate = [&](s64 value) {
                return ValidatedOrDefault(value, int_descriptor).value.Get<s64>();
            };

            SUBCASE("basics") {
                CHECK_EQ(validate(5), 5);
                CHECK_EQ(validate(0), 0);

                // value should be clamped to the range
                CHECK_EQ(validate(100), 10);
                CHECK_EQ(validate(-100), 0);
            }

            SUBCASE("no clamp") {
                int_descriptor.value_requirements.Get<Descriptor::IntRequirements>().validator =
                    [](s64& value) {
                        if (value < 0) return false;
                        if (value > 10) return false;
                        return true;
                    };
                CHECK_EQ(validate(100), 5);
                CHECK_EQ(validate(-100), 5);
            }

            SUBCASE("custom constrainer") {
                int_descriptor.value_requirements.Get<Descriptor::IntRequirements>().validator =
                    [](s64& value) {
                        value = 7;
                        return true;
                    };
                CHECK_EQ(validate(100), 7);
                CHECK_EQ(validate(5), 7);
            }
        }

        SUBCASE("string") {
            Descriptor descriptor {
                .key = "key"_s,
                .value_requirements =
                    Descriptor::StringRequirements {
                        .validator =
                            [](String& value) {
                                if (value.size < 2) return false;
                                if (value.size > 10) return false;
                                return true;
                            },
                    },
                .default_value = "default"_s,
                .gui_label = "Key"_s,
                .long_description = "This is a key"_s,
            };
            auto const validate = [&](String value) {
                return ValidatedOrDefault(value, descriptor).value.Get<String>();
            };

            CHECK_EQ(validate("value"), "value"_s);
            CHECK_EQ(validate("valuevaluevalue"), "default"_s); // too long
            CHECK_EQ(validate("v"), "default"_s); // too short

            // ensure_utf8
            descriptor.value_requirements.Get<Descriptor::StringRequirements>().validator =
                [](String& value) { return IsValidUtf8(value); };
            CHECK_EQ(validate("value"), "value"_s);
            CHECK_EQ(validate("value\xFF"), "default"_s);

            // ensure_absolute_path
            descriptor.value_requirements.Get<Descriptor::StringRequirements>().validator =
                [](String& value) { return path::IsAbsolute(value); };
            if constexpr (IS_WINDOWS)
                CHECK_EQ(validate("C:\\value"), "C:\\value"_s);
            else
                CHECK_EQ(validate("/value"), "/value"_s);
            CHECK_EQ(validate("value"), "default"_s);
        }

        SUBCASE("get value") {
            PreferencesTable prefs;
            SUBCASE("not present") {
                auto const result = GetValue(prefs, int_descriptor);
                CHECK(result.is_default);
                CHECK_EQ(result.value.Get<s64>(), 5);
            }

            SUBCASE("already valid") {
                prefs.InsertGrowIfNeeded(tester.scratch_arena,
                                         int_descriptor.key,
                                         tester.scratch_arena.New<Value>((s64)7));
                auto const result = GetValue(prefs, int_descriptor);
                CHECK(!result.is_default);
                CHECK_EQ(result.value.Get<s64>(), 7);
            }

            SUBCASE("invalid") {
                prefs.InsertGrowIfNeeded(tester.scratch_arena,
                                         int_descriptor.key,
                                         tester.scratch_arena.New<Value>((s64)100));
                auto const result = GetValue(prefs, int_descriptor);
                CHECK(!result.is_default);
                CHECK_EQ(result.value.Get<s64>(), 10);
            }

            SUBCASE("wrong type") {
                prefs.InsertGrowIfNeeded(tester.scratch_arena,
                                         int_descriptor.key,
                                         tester.scratch_arena.New<Value>("value"_s));
                auto const result = GetValue(prefs, int_descriptor);
                CHECK(result.is_default);
                CHECK_EQ(result.value.Get<s64>(), 5);
            }

            SUBCASE("multiple") {
                prefs.InsertGrowIfNeeded(tester.scratch_arena,
                                         int_descriptor.key,
                                         tester.scratch_arena.New<Value>((s64)7));
                auto v = prefs.Find(int_descriptor.key);
                SinglyLinkedListPrepend(*v, tester.scratch_arena.New<Value>((s64)8));
                SinglyLinkedListPrepend(*v, tester.scratch_arena.New<Value>("foo"_s));
                SinglyLinkedListPrepend(*v, tester.scratch_arena.New<Value>((s64)100));

                DynamicArrayBounded<s64, 5> buffer {};
                buffer.size = buffer.Capacity();
                auto const num_written = GetValues(prefs, int_descriptor, buffer);
                CHECK_EQ(num_written, 3u);
                buffer.size = num_written;
                CHECK(Contains(buffer, 7));
                CHECK(Contains(buffer, 8));
                CHECK(Contains(buffer, 10));
            }
        }

        SUBCASE("match") {
            PreferencesTable prefs;
            SUBCASE("finds matching value") {
                prefs.InsertGrowIfNeeded(tester.scratch_arena,
                                         int_descriptor.key,
                                         tester.scratch_arena.New<Value>((s64)7));

                auto const& first_item = *(*prefs.begin()).value;
                auto const opt = Match(int_descriptor.key, &first_item, int_descriptor);
                REQUIRE(opt);
                CHECK_EQ(opt->Get<s64>(), 7);
            }

            SUBCASE("doesn't find non-existing") {
                Value value = {"string"_s};
                auto const opt = Match("foo"_s, &value, int_descriptor);
                CHECK(!opt);
            }

            SUBCASE("constrains matching value") {
                prefs.InsertGrowIfNeeded(tester.scratch_arena,
                                         int_descriptor.key,
                                         tester.scratch_arena.New<Value>((s64)100));

                auto const& first_item = *(*prefs.begin()).value;
                auto const opt = Match(int_descriptor.key, &first_item, int_descriptor);
                REQUIRE(opt);
                CHECK_EQ(opt->Get<s64>(), 10);
            }
        }
    }

    SUBCASE("store a long string") {
        Preferences prefs;
        auto long_string = tester.scratch_arena.AllocateExactSizeUninitialised<char>(1000);
        for (auto& c : long_string)
            c = 'a';
        AddValue(prefs, "key"_s, (String)long_string);
        CHECK_EQ(*LookupString(prefs, "key"_s), long_string);
    }

    return k_success;
}

} // namespace prefs

TEST_REGISTRATION(RegisterPreferencesTests) {
    REGISTER_TEST(prefs::TestLegacyPreferences);
    REGISTER_TEST(prefs::TestPreferences);
}
