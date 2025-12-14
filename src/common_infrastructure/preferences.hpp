// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "os/misc.hpp"

// Preferences are stored in the INI file format.
//
// This is for anything we want to persist between sessions, e.g. window size, extra library folders, etc.
//
// Primarily, preferences are controlled by the user through the GUI. However, we also support users manually
// editing the preferences file. We use a directory watcher and a diff algorithm to detect changes to the file
// and update the preferences accordingly. This is also necessary in the case that there are multiple
// processes running Floe at the same time (this can happen in some DAWs).
//
// In general, we take the approach that the preferences system doesn't know anything about the data that it
// is storing. Instead, each part of the code that uses the preferences should know their own keys and
// validate the values they get from the preferences. However, for backwards compatibility this code does know
// about keys in the legacy file format so that it can remap them.
//
// We want preferences to be both forwards and backwards compatible because sometimes multiple versions of
// Floe can be installed at the same type (for example, when using multiple plugin folders, DAWs can sometimes
// load the plugin from either version). This isn't a common scenario but it's one that can sometimes occur.
// We want both old and new versions of Floe to be able to read and write the preferences file without losing
// any data.
//
// INI is not a strict format. These are our specific rules:
// - 'key = value\n' syntax. Spaces or tabs around the = are ignored.
// - Key and value must be on the same line.
// - There's no escaping of special characters.
// - There's no quoting of strings.
// - The same key can appear multiple times with different values, in which case the same key has multiple
//   values (an array). These values are unordered. Duplicate values for the same key are ignored.
// - Keys and sections must be <= k_max_key_size long.
// - Sections are in square brackets on line of their own: [Section Name].
// - Comments are lines starting with a semicolon.
// - We don't enforce a format for keys, but prefer keys-with-dashes.
//
// Preferences are kept in a hash table. The key is a string/int or a section + key string/int pair. The value
// is a linked list. You can loop over the table to get all the key-value pairs.

namespace prefs {

constexpr usize k_max_file_size = Kb(32);
constexpr usize k_max_key_size = 50; // also for sections
constexpr f64 k_file_watcher_poll_interval_seconds = 1;

enum class ValueType : u8 {
    String,
    Int,
    Bool,
};

using ValueUnion = TaggedUnion<ValueType,
                               TypeAndTag<String, ValueType::String>,
                               TypeAndTag<s64, ValueType::Int>,
                               TypeAndTag<bool, ValueType::Bool>>;

struct Value : ValueUnion {
    bool operator==(ValueUnion const& v) const { return (ValueUnion const&)*this == v; }
    Value* next {};
};

enum class KeyValueType : u8 {
    String,
    Int,
};

using KeyValueUnion =
    TaggedUnion<KeyValueType, TypeAndTag<String, KeyValueType::String>, TypeAndTag<s64, KeyValueType::Int>>;

struct SectionedKey {
    bool operator==(SectionedKey const& other) const = default;
    String section;
    KeyValueUnion key;
};

// NOTE: GlobalString and GlobalInt could be combined and stored using KeyValueUnion, but it makes the API a
// little less ergonomic because we'd then have to explicitly specify the KeyValueUnion type when creating a
// key, rather than just String or s64.
enum class KeyType : u32 { GlobalString, GlobalInt, Sectioned };

// Our HashTable implementation currently requires keys to be default constructible.
constexpr auto TaggedUnionDefaultValue(KeyType) { return ""_s; }

using Key = TaggedUnion<KeyType,
                        TypeAndTag<String, KeyType::GlobalString>,
                        TypeAndTag<s64, KeyType::GlobalInt>,
                        TypeAndTag<SectionedKey, KeyType::Sectioned>>;

ErrorCodeOr<void> CustomValueToString(Writer writer, Key const& key, fmt::FormatOptions options);
ErrorCodeOr<void> CustomValueToString(Writer writer, ValueUnion const& key, fmt::FormatOptions options);
u64 HashKey(Key const& key);

using PreferencesTable = HashTable<Key, Value*, HashKey>;

PreferencesTable ParsePreferencesFile(String file_data, ArenaAllocator& arena);
PreferencesTable ParseLegacyPreferencesFile(String file_data, ArenaAllocator& arena);

struct ReadResult {
    String file_data;
    s128 file_last_modified {};
};
ErrorCodeOr<ReadResult> ReadEntirePreferencesFile(String path, ArenaAllocator& arena);

ErrorCodeOr<void> WritePreferencesTable(PreferencesTable const& table, Writer writer);
ErrorCodeOr<void> WritePreferencesFile(PreferencesTable const& table,
                                       String path,
                                       Optional<s128> set_last_modified = NanosecondsSinceEpoch());

// NOTE: you shouldn't trust the values you get from any of the Lookup functions. They could be manually
// modified in the file, or they could be from a different version of Floe that had different uses of the same
// key. Therefore, you should always validate the values before using them.

// These functions assume that the key has a single value (or no value). If you want to lookup multiple
// values, then use LookupValues.
Optional<s64> LookupInt(PreferencesTable const& table, Key const& key);
Optional<bool> LookupBool(PreferencesTable const& table, Key const& key);
Optional<String> LookupString(PreferencesTable const& table, Key const& key);

// Can return null. Value is an intrusive linked list. Iterate through it using 'next'.
// The order of values is always undefined. There's guaranteed to not be duplicate values for a key.
Value const* LookupValues(PreferencesTable const& table, Key const& key);

template <dyn::DynArray DynArrayType>
PUBLIC void ValuesToArray(Value const* value_list, DynArrayType& array) {
    for (auto value = value_list; value; value = value->next)
        if (auto const v = value->TryGet<typename DynArrayType::ValueType>())
            dyn::AppendIfNotAlreadyThere(array, *v);
}

template <dyn::DynArray DynArrayType>
PUBLIC void LookupValues(PreferencesTable const& table, Key const& key, DynArrayType& array) {
    dyn::Clear(array);
    if (auto v = LookupValues(table, key)) ValuesToArray(v, array);
}

template <typename Type, usize k_size>
PUBLIC DynamicArrayBounded<Type, k_size> LookupValues(PreferencesTable const& table, Key const& key) {
    DynamicArrayBounded<Type, k_size> result;
    LookupValues(table, key, result);
    return result;
}

// =================================================================================================
// Higher-level API
// =================================================================================================

// Validators are functions that validator or constrain a value. Returns true if value is valid.
// If false, the value will be replaced with the default value. 'value' is a reference, you can modify
// it and return true if you want to keep the value.
using IntValidator = TrivialFixedSizeFunction<32, bool(s64& value)>;
using StringValidator = TrivialFixedSizeFunction<32, bool(String& value)>;

// Information for validating and constraining individual values.
// Can't be constexpr sadly because of the TaggedUnion and validators.
// Primarily used for single-value keys, however, you can use it for multi-value keys validation too. In that
// case, default_value is not really a default value, but instead a special indicator that the value does not
// pass validation.
struct Descriptor {
    struct IntRequirements {
        IntValidator validator {}; // optional
    };

    struct StringRequirements {
        StringValidator validator {}; // optional
    };

    using ValueRequirements = TaggedUnion<ValueType,
                                          TypeAndTag<IntRequirements, ValueType::Int>,
                                          TypeAndTag<StringRequirements, ValueType::String>>;

    Key key;
    ValueRequirements value_requirements;
    ValueUnion default_value;
    String gui_label;
    String long_description;
};

struct ValidateResult {
    ValueUnion value;
    bool is_default; // Saves you from having to do a comparison with the default value.
};

// Returns the valid, constrained value or the default value if not. In a multi-value setting the default
// value means the value is invalid.
ValidateResult ValidatedOrDefault(ValueUnion const& value, Descriptor const& descriptor);

// Looks up the single value, if it exists and validates it, otherwise returns the default value.
// Guaranteed to return the value in the correct type.
ValidateResult GetValue(PreferencesTable const& table, Descriptor const& descriptor);
bool GetBool(PreferencesTable const& table, Descriptor const& descriptor);
s64 GetInt(PreferencesTable const& table, Descriptor const& descriptor);
String GetString(PreferencesTable const& table, Descriptor const& descriptor);

// Fills 'out' with valid values matching the descriptor. Returns the number of values written.
usize GetValues(PreferencesTable const& table, Descriptor const& descriptor, Span<ValueUnion> out);
usize GetValues(PreferencesTable const& table, Descriptor const& descriptor, Span<bool> out);
usize GetValues(PreferencesTable const& table, Descriptor const& descriptor, Span<s64> out);
usize GetValues(PreferencesTable const& table, Descriptor const& descriptor, Span<String> out);

template <typename Type, usize k_size>
PUBLIC DynamicArrayBounded<Type, k_size> GetValues(PreferencesTable const& table,
                                                   Descriptor const& descriptor) {
    DynamicArrayBounded<Type, k_size> result;
    result.size = k_size - 1;
    result.size = GetValues(table, descriptor, result);
    return result;
}

// If the key doesn't match the descriptor, returns nullopt. Else it returns the validated, constrained, or
// default value. Useful inside the on_change callback.
Optional<ValueUnion> Match(Key const& key, Value const* value_list, Descriptor const& descriptor);
Optional<bool> MatchBool(Key const& key, Value const* value_list, Descriptor const& descriptor);
Optional<s64> MatchInt(Key const& key, Value const* value_list, Descriptor const& descriptor);
Optional<String> MatchString(Key const& key, Value const* value_list, Descriptor const& descriptor);

Optional<usize>
MatchValues(Key const& key, Value const* value_list, Descriptor const& descriptor, Span<ValueUnion> out);
Optional<usize>
MatchValues(Key const& key, Value const* value_list, Descriptor const& descriptor, Span<bool> out);
Optional<usize>
MatchValues(Key const& key, Value const* value_list, Descriptor const& descriptor, Span<String> out);
Optional<usize>
MatchValues(Key const& key, Value const* value_list, Descriptor const& descriptor, Span<s64> out);

// Edited over time.
struct Preferences : PreferencesTable {
    ArenaAllocator arena {PageAllocator::Instance()};
    PathPool path_pool {};
    Value* free_values {};

    // We use track the last modified time so we can detect if the file has been changed externally or by our
    // own write operation.
    s128 last_known_file_modified_time {};
    bool write_to_file_needed {};

    // If value is null it means the key was removed. Also remember Value is a linked list if you are
    // expecting multiple values.
    TrivialFixedSizeFunction<8, void(Key const& key, Value const* value_list)> on_change {};

    // Watcher
    ArenaAllocator watcher_scratch {PageAllocator::Instance()};
    ArenaAllocator watcher_arena {PageAllocator::Instance()};
    Optional<DirectoryWatcher> watcher;
    TimePoint last_watcher_poll_time {};
};

struct SetValueOptions {
    bool clone_key_string {};
    bool dont_send_on_change_event {};
    bool dont_mark_as_changed {};
    bool overwrite_only {}; // do nothing if the key doesn't exist already
};

// The value will be allocated in the arena/path_pool (the string is cloned).
// Sets the value of key to the single value 'value'. If the key already has any values, they will be
// replaced.
void SetValue(Preferences& preferences,
              Key const& key,
              ValueUnion const& value,
              SetValueOptions options = {});
void SetValue(Preferences& preferences,
              Descriptor const& descriptor,
              ValueUnion const& value,
              SetValueOptions options = {});

// Same as SetValue in all ways, except instead of replacing all/eny values, this logic is used: if the key
// already has value(s), each value will be compared to the new value; if the new value matches any of the
// existing values, nothing will be added.
bool AddValue(Preferences& preferences,
              Key const& key,
              ValueUnion const& value,
              SetValueOptions options = {});
void AddValue(Preferences& preferences,
              Descriptor const& descriptor,
              ValueUnion const& value,
              SetValueOptions options = {});

struct RemoveValueOptions {
    bool dont_send_on_change_event {};
    bool dont_mark_as_changed {};
};

// The value will be compared to all values for the given key, if it matches it will be removed.
// If the last value is removed, the key will be removed.
bool RemoveValue(Preferences& preferences,
                 Key const& key,
                 ValueUnion const& value,
                 RemoveValueOptions options = {});

// Remove key and all values associated with it.
void Remove(Preferences& preferences, Key const& key, RemoveValueOptions options = {});

struct ReplaceOptions {
    // Whether keys in the exsiting preferences should be removed if they don't exist in the new table. Keys
    // that do exist in the new table always entirely replace all existing values.
    bool remove_keys_not_in_new_table {};
};

// Entirely replaces the preferences table with a new one. Emits minimal on_change notifications for all
// keys/value that have changed.
void ReplacePreferences(Preferences& preferences,
                        PreferencesTable const& new_table,
                        ReplaceOptions options = {});

// Inits the object. Reads and parses the file. possible_paths should be sorted in order of preference. The
// first is the most preferred. Returns the index of the path that was used, or nullopt if no path was used.
Optional<usize> Init(Preferences& preferences, Span<String const> possible_paths);
void Deinit(Preferences& preferences);

void WriteIfNeeded(Preferences& preferences, Optional<String> override_write_path = {});

struct PollForExternalChangesOptions {
    bool ignore_rate_limiting {};
    Optional<String> override_write_path {};
};

void PollForExternalChanges(Preferences& preferences, PollForExternalChangesOptions options = {});

namespace key {
// We have code that needs to remap legacy preference keys to new keys, so we need to store this here.
// Usually though, preference keys should be private to the module that needs them.
namespace section {
constexpr String k_cc_to_param_id_map_section = "Default Map MIDI CC to Param IDs"_s;
}
constexpr String k_extra_libraries_folder = "extra-libraries-folder"_s;
constexpr String k_extra_presets_folder = "extra-presets-folder"_s;
constexpr String k_gui_keyboard_octave = "gui-keyboard-octave"_s;
constexpr String k_high_contrast_gui = "high-contrast-gui"_s;
constexpr String k_show_keyboard = "show-keyboard"_s;
constexpr String k_show_tooltips = "show-tooltips"_s;
constexpr String k_window_width = "window-width"_s;
} // namespace key

} // namespace prefs
