// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lua.h>

#include "foundation/foundation.hpp"
#include "os/misc.hpp"
#include "os/web.hpp"
#include "utils/cli_arg_parse.hpp"
#include "utils/json/json_reader.hpp"
#include "utils/json/json_writer.hpp"

#include "common_infrastructure/common_errors.hpp"
#include "common_infrastructure/descriptors/effect_descriptors.hpp"
#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/global.hpp"
#include "common_infrastructure/sample_library/sample_library.hpp"

#include "config.h"
#include "packager_tool/packager.hpp"

static DynamicArrayBounded<char, 64> Identifier(String name, Optional<String> sub_name = {}) {
    DynamicArrayBounded<char, 64> result {};
    dyn::AppendSpan(result, "==");
    dyn::AppendSpan(result, name);
    if (sub_name) {
        dyn::AppendSpan(result, ":");
        dyn::AppendSpan(result, *sub_name);
    }
    dyn::AppendSpan(result, "==");
    return result;
}

static void ExpandIdentifier(DynamicArray<char>& markdown_blob,
                             String identifier,
                             String replacement,
                             ArenaAllocator& scratch) {
    dyn::Replace(markdown_blob, identifier, json::EncodeString(replacement, scratch));
}

static void ExpandIdentifiersBasedOnLuaSections(DynamicArray<char>& markdown_blob,
                                                String lua,
                                                String identifier,
                                                ArenaAllocator& scratch) {
    Optional<String> current_section_name {};
    char const* section_start {};
    char const* section_end {};
    constexpr String k_anchor_prefix = "-- SECTION: ";
    constexpr String k_anchor_end_prefix = "-- SECTION_END: ";
    for (auto const line : SplitIterator {lua, '\n'}) {
        if (StartsWithSpan(WhitespaceStrippedStart(line), k_anchor_prefix)) {
            current_section_name = line.SubSpan(k_anchor_prefix.size);
            section_start = End(line) + 1;
        } else if (StartsWithSpan(WhitespaceStrippedStart(line), k_anchor_end_prefix)) {
            section_end = line.data;
            auto const section =
                WhitespaceStripped(String {section_start, (usize)(section_end - section_start)});
            ExpandIdentifier(markdown_blob, Identifier(identifier, *current_section_name), section, scratch);
        }
    }
}

// We just replace the placeholders with the actual content.
static ErrorCodeOr<String> PreprocessMarkdownBlob(String markdown_blob) {
    ArenaAllocator scratch {PageAllocator::Instance()};
    DynamicArray<char> buffer {scratch};

    DynamicArray<char> result {PageAllocator::Instance()};
    dyn::Assign(result, markdown_blob);

    {
        dyn::Clear(buffer);
        TRY(sample_lib::WriteDocumentedLuaExample(dyn::WriterFor(buffer), true));
        ExpandIdentifiersBasedOnLuaSections(result, buffer, "sample-library-example-lua", scratch);
    }

    {
        dyn::Clear(buffer);
        TRY(sample_lib::WriteDocumentedLuaExample(dyn::WriterFor(buffer), false));
        ExpandIdentifier(result, Identifier("sample-library-example-lua-no-comments"), buffer, scratch);
    }

    ExpandIdentifier(result,
                     Identifier("lua-version"),
                     String {LUA_VERSION_MAJOR "." LUA_VERSION_MINOR},
                     scratch);

    {
        String windows_version = {};

        // From public domain https://github.com/reactos/reactos/blob/master/sdk/include/psdk/sdkddkver.h
        switch (MIN_WINDOWS_NTDDI_VERSION) {
            case 0x0A000000: windows_version = "Windows 10"; break; // 10240 / 1507 / Threshold 1
            case 0x0A000001: windows_version = "Windows 10 (Build 10586)"; break; // 1511 / Threshold 2
            case 0x0A000002: windows_version = "Windows 10 (Build 14393)"; break; // 1607 / Redstone 1
            case 0x0A000003: windows_version = "Windows 10 (Build 15063)"; break; // 1703 / Redstone 2
            case 0x0A000004: windows_version = "Windows 10 (Build 16299)"; break; // 1709 / Redstone 3
            case 0x0A000005: windows_version = "Windows 10 (Build 17134)"; break; // 1803 / Redstone 4
            case 0x0A000006: windows_version = "Windows 10 (Build 17763)"; break; // 1809 / Redstone 5
            case 0x0A000007: windows_version = "Windows 10 (Build 18362)"; break; // 1903 / 19H1 "Titanium"
            case 0x0A000008: windows_version = "Windows 10 (Build 19041)"; break; // 2004 / Vibranium
            case 0x0A000009: windows_version = "Windows 10 (Build 19042)"; break; // 20H2 / Manganese
            case 0x0A00000A: windows_version = "Windows 10 (Build 19043)"; break; // 21H1 / Ferrum
            case 0x0A00000B: windows_version = "Windows 11"; break; // 22000 / 21H2 / Cobalt
            case 0x0A00000C: windows_version = "Windows 11 (Build 22621)"; break; // 22H2 / Nickel
            case 0x0A00000D: windows_version = "Windows 11 (Build 22621)"; break; // 22H2 / Copper
            default: PanicIfReached();
        }

        ExpandIdentifier(result, Identifier("min-windows-version"), windows_version, scratch);
    }

    {
        auto const macos_version = ParseVersionString(MIN_MACOS_VERSION).Value();
        DynamicArrayBounded<char, 64> macos_version_str {"macOS "};
        ASSERT(macos_version.major != 0);
        fmt::Append(macos_version_str, "{}", macos_version.major);
        if (macos_version.minor != 0) fmt::Append(macos_version_str, ".{}", macos_version.minor);
        if (macos_version.patch != 0) fmt::Append(macos_version_str, ".{}", macos_version.patch);

        String release_name = {};
        switch (macos_version.major) {
            case 11: release_name = "Big Sur"; break;
            case 12: release_name = "Monterey"; break;
            case 13: release_name = "Ventura"; break;
            case 14: release_name = "Sonoma"; break;
            case 15: release_name = "Sequoia"; break;
            default: PanicIfReached();
        }
        fmt::Append(macos_version_str, " ({})", release_name);

        ExpandIdentifier(result, Identifier("min-macos-version"), macos_version_str, scratch);
    }

    // get the latest release version and the download links
    {
        auto const cached_reponse_path =
            path::Join(scratch, Array {String(FLOE_PROJECT_CACHE_PATH), "latest-release.json"});

        auto const cached_response = ({
            Optional<String> response = {};
            auto const o = ReadEntireFile(cached_reponse_path, scratch);
            if (o.HasError()) {
                if (o.Error() != FilesystemError::PathDoesNotExist) return o.Error();
            } else
                response = o.Value();
            response;
        });

        String json_data {};

        if (cached_response) {
            json_data = *cached_response;
        } else {
            Span<String> headers {};
            if (auto const token = GetEnvironmentVariable("GITHUB_TOKEN"_s, scratch)) {
                headers = scratch.AllocateExactSizeUninitialised<String>(1);
                headers[0] = fmt::Format(scratch, "Authorization: Bearer {}", *token);
            }

            DynamicArray<char> data {scratch};
            TRY(HttpsGet("https://api.github.com/repos/floe-audio/Floe/releases/latest",
                         dyn::WriterFor(data),
                         {.headers = headers}));
            json_data = data.ToOwnedSpan();
            TRY(WriteFile(cached_reponse_path, json_data));
        }

        String latest_release_version {};
        struct Asset {
            String name;
            usize size;
            String url;
        };
        ArenaList<Asset, false> assets {scratch};

        auto const handle_asset_object = [&](json::EventHandlerStack&, json::Event const& event) {
            if (event.type == json::EventType::HandlingStarted) {
                *assets.PrependUninitialised() = {};
                return true;
            }

            if (json::SetIfMatchingRef(event, "name", assets.first->data.name)) return true;
            if (json::SetIfMatching(event, "size", assets.first->data.size)) return true;
            if (json::SetIfMatchingRef(event, "browser_download_url", assets.first->data.url)) return true;

            return false;
        };

        auto const handle_assets_array = [&](json::EventHandlerStack& handler_stack,
                                             json::Event const& event) {
            if (SetIfMatchingObject(handler_stack, event, "", handle_asset_object)) return true;
            return false;
        };

        auto const handle_root_object = [&](json::EventHandlerStack& handler_stack,
                                            json::Event const& event) {
            json::SetIfMatchingRef(event, "tag_name", latest_release_version);
            if (SetIfMatchingArray(handler_stack, event, "assets", handle_assets_array)) return true;
            return false;
        };

        auto const o = json::Parse(json_data, handle_root_object, scratch, {});

        if (o.HasError()) return ErrorCode {CommonError::InvalidFileFormat};

        {
            DynamicArrayBounded<char, 250> name {};
            for (auto& asset : assets) {
                dyn::Assign(name, asset.name);
                dyn::Replace(name, latest_release_version, ""_s);
                dyn::Replace(name, "--"_s, "-"_s);
                name.size -= path::Extension(name).size;

                dyn::AppendSpan(name, "-markdown-link");
                ExpandIdentifier(result,
                                 Identifier(name),
                                 fmt::Format(scratch,
                                             "[Download {}]({}) ({} MB)",
                                             asset.name,
                                             asset.url,
                                             Max(1uz, asset.size / 1024 / 1024)),
                                 scratch);
            }
        }

        {
            if (StartsWith(latest_release_version, 'v')) latest_release_version.RemovePrefix(1);
            if (latest_release_version.size == 0) {
                LogError(ModuleName::Main, "Failed to parse github release version: {}\n", json_data);
                return ErrorCode {CommonError::InvalidFileFormat};
            }
            ExpandIdentifier(result, Identifier("latest-release-version"), latest_release_version, scratch);
        }
    }

    // packger tool --help
    {
        DynamicArray<char> packager_help {scratch};
        TRY(PrintUsage(dyn::WriterFor(packager_help),
                       "floe-packager",
                       k_packager_description,
                       k_packager_command_line_args_defs));
        ExpandIdentifier(result,
                         Identifier("packager-help"),
                         WhitespaceStrippedEnd(String(packager_help)),
                         scratch);
    }

    {
        DynamicArray<char> param_table {scratch};
        fmt::Append(param_table, "| Module | Name | ID | Description |\n");
        fmt::Append(param_table, "|--|--|--|--|\n");

        Array<ParamDescriptor const*, k_num_parameters> descriptors;
        for (auto const i : Range(k_num_parameters))
            descriptors[i] = &k_param_descriptors[i];
        Sort(descriptors, [](ParamDescriptor const* a, ParamDescriptor const* b) {
            if (ToInt(a->module_parts[0]) == ToInt(b->module_parts[0])) return a->id < b->id;
            return ToInt(a->module_parts[0]) < ToInt(b->module_parts[0]);
        });

        for (auto const& p : descriptors)
            fmt::Append(param_table,
                        "| {} | {} | {} | {} |\n",
                        p->ModuleString(),
                        p->name,
                        p->id,
                        p->tooltip);
        ExpandIdentifier(result, Identifier("parameter-table"), param_table, scratch);
    }

    {
        DynamicArray<char> effects_table {scratch};
        fmt::Append(effects_table, "| Name | Description |\n");
        fmt::Append(effects_table, "|--|--|\n");

        for (auto const& e : k_effect_info)
            fmt::Append(effects_table, "| {} | {} |\n", e.name, e.description);
        ExpandIdentifier(result, Identifier("effects-table"), effects_table, scratch);
    }

    ExpandIdentifier(result, Identifier("effects-count"), fmt::IntToString(k_num_effect_types), scratch);

    return result.ToOwnedSpan();
}

// "The JSON consists of an array of [context, book] where context is the serialized object
// PreprocessorContext and book is a Book object containing the content of the book. The preprocessor should
// return the JSON format of the Book object to stdout, with any modifications it wishes to perform."
//
// We can avoid parsing the JSON and instead just find the book object through some simple string
// manipulation.
static ErrorCodeOr<String> FindBookJson(String json) {
    // [
    //    {
    //      <PreprocessorContext object (we don't care about this)>
    //    },
    //    {
    //      <Book object (we need to return this)>
    //    }
    // ]

    json = WhitespaceStripped(json);

    char const* p = json.data;
    char const* end = json.data + json.size;

    auto const skip_whitespace = [&p, end]() -> ErrorCodeOr<void> {
        while (p != end && IsWhitespace(*p))
            ++p;
        if (p == end) return ErrorCode {CommonError::InvalidFileFormat};
        return k_success;
    };

    auto const skip_char = [&p, end](char c) -> ErrorCodeOr<void> {
        if (p == end || *p != c) return ErrorCode {CommonError::InvalidFileFormat};
        ++p;
        return k_success;
    };

    TRY(skip_char('['));
    TRY(skip_whitespace());

    TRY(skip_char('{'));
    // Skip everything until this current object ends, considering nested objects
    u8 nesting = 1;
    while (p != end && nesting) {
        // IMPROVE: this doesn't handle nested objects properly - what if { is in a string? It doesn't seem to
        // be a problem for the current input though.
        if (*p == '{') ++nesting;
        if (*p == '}') --nesting;
        ++p;
    }
    TRY(skip_char(','));

    TRY(skip_whitespace());

    char const* book_start = p;
    char const* book_end = end;

    // Remove the array closing bracket from the end
    book_end -= 1;

    return WhitespaceStripped(String {book_start, (usize)(book_end - book_start)});
}

static ErrorCodeOr<int> Main(ArgsCstr args) {
    if (args.size > 1) {
        // Behaviour matching mdbook's python example
        if (NullTermStringsEqual(args.args[1], "supports")) return 0;
    }

    WebGlobalInit();
    DEFER { WebGlobalCleanup(); };

    ArenaAllocator arena {PageAllocator::Instance()};

    // A mdbook preprocessor receives JSON on stdin (an array: [context, book]) and should output
    // the modified book JSON to stdout.

    auto const raw_json_input = TRY(ReadAllStdin(arena));

    auto const book_json = TRY(FindBookJson(raw_json_input));

    // We just manipulate the unparsed JSON string directly - we're only doing simple text expansions. It
    // would be good to parse it but it's a faff with our current JSON parser. We'd also need to re-serialize
    // it back to JSON.
    auto const preprocessed_book_json = TRY(PreprocessMarkdownBlob(book_json));

    TRY(StdPrint(StdStream::Out, preprocessed_book_json));

    return 0;
}

int main(int argc, char** argv) {
    auto _ = EnterLogicalMainThread();
    GlobalInit({.init_error_reporting = false, .set_main_thread = true});
    DEFER { GlobalDeinit({.shutdown_error_reporting = false}); };
    auto result = Main({argc, argv});
    if (result.HasError()) {
        StdPrintF(StdStream::Err, "Error: {}", result.Error());
        return 1;
    }
    return result.Value();
}
