// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lua.h>

#include "foundation/foundation.hpp"
#include "os/misc.hpp"
#include "os/web.hpp"
#include "utils/cli_arg_parse.hpp"
#include "utils/json/json_reader.hpp"
#include "utils/json/json_writer.hpp"
#include "utils/logger/logger.hpp"

#include "common_infrastructure/cc_mapping.hpp"
#include "common_infrastructure/common_errors.hpp"
#include "common_infrastructure/descriptors/effect_descriptors.hpp"
#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/global.hpp"
#include "common_infrastructure/sample_library/sample_library.hpp"
#include "common_infrastructure/tags.hpp"

#include "config.h"
#include "packager_tool/packager.hpp"

static ErrorCodeOr<void> WriteLuaSectionValues(json::WriteContext& ctx,
                                               String lua,
                                               String identifier_prefix,
                                               ArenaAllocator& allocator) {
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
            auto const key = fmt::Format(allocator, "{}:{}", identifier_prefix, *current_section_name);
            TRY(json::WriteKeyValue(ctx, key, section));
        }
    }
    return k_success;
}

static ErrorCodeOr<void> WriteLuaData(json::WriteContext& ctx) {
    ArenaAllocator scratch {PageAllocator::Instance()};
    DynamicArray<char> buffer {scratch};

    // Lua examples with sections
    {
        dyn::Clear(buffer);
        TRY(sample_lib::WriteDocumentedLuaExample(dyn::WriterFor(buffer), true));
        TRY(WriteLuaSectionValues(ctx, buffer, "sample-library-example-lua", scratch));
    }

    // Lua LSP definitions
    {
        dyn::Clear(buffer);
        TRY(sample_lib::WriteLuaLspDefintionsFile(dyn::WriterFor(buffer)));
        TRY(json::WriteKeyValue(ctx, "floe-lua-lsp-defs", String(buffer)));
    }

    // Lua example without comments
    {
        dyn::Clear(buffer);
        TRY(sample_lib::WriteDocumentedLuaExample(dyn::WriterFor(buffer), false));
        TRY(json::WriteKeyValue(ctx, "sample-library-example-lua-no-comments", String(buffer)));
    }

    return k_success;
}

static ErrorCodeOr<void> WriteVersionData(json::WriteContext& ctx) {
    ArenaAllocator scratch {PageAllocator::Instance()};

    // Lua version
    TRY(json::WriteKeyValue(ctx, "lua-version", String {LUA_VERSION_MAJOR "." LUA_VERSION_MINOR}));

    // Windows version
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

        TRY(json::WriteKeyValue(ctx, "min-windows-version", windows_version));
    }

    // macOS version
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

        TRY(json::WriteKeyValue(ctx, "min-macos-version", String(macos_version_str)));
    }

    return k_success;
}

struct GithubRelease {
    struct Asset {
        String name;
        usize size;
        String url;
    };

    String version_string {};
    Optional<Version> version {};
    ArenaList<Asset> assets {};
};

static json::EventCallback ReleaseJsonParser(GithubRelease& release, ArenaAllocator& arena) {
    auto handle_asset_object = [&arena, &release](json::EventHandlerStack&, json::Event const& event) {
        if (event.type == json::EventType::HandlingStarted) {
            *release.assets.PrependUninitialised(arena) = {};
            return true;
        }

        if (json::SetIfMatchingRef(event, "name", release.assets.first->data.name)) return true;
        if (json::SetIfMatching(event, "size", release.assets.first->data.size)) return true;
        if (json::SetIfMatchingRef(event, "browser_download_url", release.assets.first->data.url))
            return true;

        return false;
    };

    auto handle_asset_array = [handle_asset_object](json::EventHandlerStack& handler_stack,
                                                    json::Event const& event) {
        if (json::SetIfMatchingObject(handler_stack, event, "", handle_asset_object)) return true;
        return false;
    };

    return json::EventCallback(
        [handle_asset_array, &release](json::EventHandlerStack& handler_stack, json::Event const& event) {
            if (json::SetIfMatchingRef(event, "tag_name", release.version_string)) {
                release.version_string = TrimStartIfMatches(release.version_string, "v"_s);
                release.version = ParseVersionString(release.version_string);
                return true;
            }
            if (json::SetIfMatchingArray(handler_stack, event, "assets", handle_asset_array)) return true;
            return false;
        },
        arena);
}

enum class GithubReleaseEndpoint {
    LatestRelease,
    ListReleases,
};

static ErrorCodeOr<String> GetGitHubReleaseJson(ArenaAllocator& arena, GithubReleaseEndpoint endpoint) {

    // The GitHub API has rate limiting that we can sometimes hit during documentation generation. We
    // work-around this by using a cached response if available.

    auto const cached_response_filename = ({
        String filename {};
        switch (endpoint) {
            case GithubReleaseEndpoint::LatestRelease: filename = "latest-release.json"; break;
            case GithubReleaseEndpoint::ListReleases: filename = "list-releases.json"; break;
        }
        filename;
    });

    auto const cached_response_path =
        path::Join(arena, Array {String(FLOE_PROJECT_CACHE_PATH), cached_response_filename});

    auto const cached_response = ({
        Optional<String> response = {};
        auto const o = ReadEntireFile(cached_response_path, arena);
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
        if (auto const token = GetEnvironmentVariable("GITHUB_TOKEN"_s, arena)) {
            headers = arena.AllocateExactSizeUninitialised<String>(1);
            headers[0] = fmt::Format(arena, "Authorization: Bearer {}", *token);
        }

        DynamicArray<char> data {arena};
        TRY(HttpsGet(({
                         String s;
                         switch (endpoint) {
                             case GithubReleaseEndpoint::LatestRelease:
                                 s = "https://api.github.com/repos/floe-audio/Floe/releases/latest";
                                 break;
                             case GithubReleaseEndpoint::ListReleases:
                                 s = "https://api.github.com/repos/floe-audio/Floe/releases";
                                 break;
                         }
                         s;
                     }),
                     dyn::WriterFor(data),
                     {.headers = headers}));
        json_data = data.ToOwnedSpan();
        TRY(WriteFile(cached_response_path, json_data));
    }

    return json_data;
}

static ErrorCodeOr<void>
WriteGithubReleaseData(json::WriteContext& ctx, GithubRelease const& release, ArenaAllocator& scratch) {
    DynamicArrayBounded<char, 250> name {};
    for (auto& asset : release.assets) {
        // Generate base key name
        dyn::Assign(name, asset.name);
        dyn::Replace(name, fmt::Join(scratch, Array {"-v"_s, release.version_string}), ""_s);
        name.size -= path::Extension(name).size;

        // Write as object with name, url, and size fields
        TRY(json::WriteKeyObjectBegin(ctx, String(name)));
        TRY(json::WriteKeyValue(ctx, "name", asset.name));
        TRY(json::WriteKeyValue(ctx, "url", asset.url));
        TRY(json::WriteKeyValue(ctx,
                                "size",
                                fmt::Format(scratch, "{} MB", Max(1uz, asset.size / 1024 / 1024))));
        TRY(json::WriteObjectEnd(ctx));
    }

    return k_success;
}

static ErrorCodeOr<void> WriteGithubReleaseData(json::WriteContext& ctx) {
    ArenaAllocator scratch {PageAllocator::Instance()};

    auto const latest_json_data = TRY(GetGitHubReleaseJson(scratch, GithubReleaseEndpoint::LatestRelease));

    GithubRelease latest_release {};
    {
        auto const o = json::Parse(latest_json_data, ReleaseJsonParser(latest_release, scratch), scratch, {});

        if (o.HasError()) return ErrorCode {CommonError::InvalidFileFormat};
        if (!latest_release.version) {
            LogError(ModuleName::Main, "Failed to parse github release version: {}\n", latest_json_data);
            return ErrorCode {CommonError::InvalidFileFormat};
        }
    }

    if (latest_release.version->beta) {
        TRY(json::WriteKeyObjectBegin(ctx, "latest-beta-release"));
        TRY(json::WriteKeyValue(ctx, "version", latest_release.version_string));
        TRY(WriteGithubReleaseData(ctx, latest_release, scratch));
        TRY(json::WriteObjectEnd(ctx));

        DynamicArray<GithubRelease> releases {scratch};

        auto const list_json_data = TRY(GetGitHubReleaseJson(scratch, GithubReleaseEndpoint::ListReleases));

        auto const o = json::Parse(list_json_data,
                                   [&](json::EventHandlerStack& handler_stack, json::Event const& event) {
                                       if (event.type == json::EventType::ObjectStart) {
                                           dyn::Append(releases, {});
                                           dyn::Emplace(handler_stack,
                                                        ReleaseJsonParser(Last(releases), scratch),
                                                        handler_stack.allocator);
                                           Last(handler_stack).HandleEvent(handler_stack, event);
                                           return true;
                                       }
                                       return false;
                                   },
                                   scratch,
                                   {});

        if (o.HasError()) return ErrorCode {CommonError::InvalidFileFormat};

        Optional<GithubRelease> non_beta_release {};
        for (auto const& r : releases) {
            if (r.version && !r.version->beta) {
                non_beta_release = r;
                break;
            }
        }
        if (!non_beta_release) {
            LogError(ModuleName::Main,
                     "Failed to find non-beta release in github releases: {}\n",
                     list_json_data);
            return ErrorCode {CommonError::InvalidFileFormat};
        }

        latest_release = *non_beta_release;
    }

    {
        TRY(json::WriteKeyValue(ctx, "latest-release-version", latest_release.version_string));
        TRY(WriteGithubReleaseData(ctx, latest_release, scratch));
    }

    return k_success;
}

static ErrorCodeOr<void> WritePackagerData(json::WriteContext& ctx) {
    ArenaAllocator scratch {PageAllocator::Instance()};
    DynamicArray<char> packager_help {scratch};
    TRY(PrintUsage(dyn::WriterFor(packager_help),
                   "floe-packager",
                   k_packager_description,
                   k_packager_command_line_args_defs));
    TRY(json::WriteKeyValue(ctx, "packager-help", WhitespaceStrippedEnd(String(packager_help))));
    return k_success;
}

static ErrorCodeOr<void> WriteParameterData(json::WriteContext& ctx) {
    ArenaAllocator scratch {PageAllocator::Instance()};

    Array<ParamDescriptor const*, k_num_parameters> descriptors;
    for (auto const i : Range(k_num_parameters))
        descriptors[i] = &k_param_descriptors[i];
    Sort(descriptors, [](ParamDescriptor const* a, ParamDescriptor const* b) {
        if (ToInt(a->module_parts[0]) == ToInt(b->module_parts[0])) return a->id < b->id;
        return ToInt(a->module_parts[0]) < ToInt(b->module_parts[0]);
    });

    TRY(json::WriteKeyArrayBegin(ctx, "parameters"));

    for (auto const& p : descriptors) {
        TRY(json::WriteObjectBegin(ctx));
        TRY(json::WriteKeyValue(ctx, "module", p->ModuleString()));
        TRY(json::WriteKeyValue(ctx, "name", p->name));
        TRY(json::WriteKeyValue(ctx, "id", p->id));
        TRY(json::WriteKeyValue(ctx, "description", p->tooltip));
        TRY(json::WriteObjectEnd(ctx));
    }

    TRY(json::WriteArrayEnd(ctx));
    return k_success;
}

static ErrorCodeOr<void> WriteEffectsData(json::WriteContext& ctx) {
    ArenaAllocator scratch {PageAllocator::Instance()};

    TRY(json::WriteKeyArrayBegin(ctx, "effects"));

    for (auto const& e : k_effect_info) {
        TRY(json::WriteObjectBegin(ctx));
        TRY(json::WriteKeyValue(ctx, "name", e.name));
        TRY(json::WriteKeyValue(ctx, "description", e.description));
        TRY(json::WriteObjectEnd(ctx));
    }

    TRY(json::WriteArrayEnd(ctx));
    TRY(json::WriteKeyValue(ctx, "effects-count", fmt::IntToString(k_num_effect_types)));
    return k_success;
}

static ErrorCodeOr<void> WriteCCMappingData(json::WriteContext& ctx) {
    ArenaAllocator scratch {PageAllocator::Instance()};
    DynamicArray<char> text {scratch};
    for (auto const& map : k_default_cc_to_param_mapping) {
        auto const& desc = k_param_descriptors[ToInt(map.param)];
        fmt::Append(text, "- CC {}: {} ({})\n", map.cc, desc.name, desc.ModuleString(" › "));
    }
    TRY(json::WriteKeyValue(ctx, "default-cc-mappings", String(text)));
    return k_success;
}

static ErrorCodeOr<void> WriteTagsData(json::WriteContext& ctx) {
    ArenaAllocator scratch {PageAllocator::Instance()};

    TRY(json::WriteKeyArrayBegin(ctx, "tag-categories"));

    u8 category_number = 1;

    for (auto const category : EnumIterator<TagCategory>()) {
        auto const tags = Tags(category);

        // If more than 25% tags have descriptions we use a table rather than notes.
        bool use_table = false;
        {
            u8 num_descriptions = 0;
            for (auto const tag : tags.tags) {
                auto const tag_info = GetTagInfo(tag);
                if (tag_info.description.size) {
                    ++num_descriptions;
                    if (num_descriptions > tags.tags.size / 4) {
                        use_table = true;
                        break;
                    }
                }
            }
        }

        TRY(json::WriteObjectBegin(ctx));
        TRY(json::WriteKeyValue(ctx, "number", category_number++));
        TRY(json::WriteKeyValue(ctx, "emoji", tags.emoji));
        TRY(json::WriteKeyValue(ctx, "name", tags.name));
        TRY(json::WriteKeyValue(ctx, "question", tags.question));
        TRY(json::WriteKeyValue(ctx, "use_table", use_table));
        TRY(json::WriteKeyValue(ctx, "recommendation", tags.recommendation));

        TRY(json::WriteKeyArrayBegin(ctx, "tags"));
        for (auto const tag : tags.tags) {
            auto const tag_info = GetTagInfo(tag);
            TRY(json::WriteObjectBegin(ctx));
            TRY(json::WriteKeyValue(ctx, "name", tag_info.name));
            TRY(json::WriteKeyValue(ctx, "description", tag_info.description));
            TRY(json::WriteObjectEnd(ctx));
        }
        TRY(json::WriteArrayEnd(ctx));

        TRY(json::WriteObjectEnd(ctx));
    }

    TRY(json::WriteArrayEnd(ctx));
    return k_success;
}

static ErrorCodeOr<void> GenerateAllData(json::WriteContext& ctx) {
    TRY(json::WriteObjectBegin(ctx));

    TRY(WriteLuaData(ctx));
    TRY(WriteVersionData(ctx));
    TRY(WriteGithubReleaseData(ctx));
    TRY(WritePackagerData(ctx));
    TRY(WriteParameterData(ctx));
    TRY(WriteEffectsData(ctx));
    TRY(WriteCCMappingData(ctx));
    TRY(WriteTagsData(ctx));

    TRY(json::WriteObjectEnd(ctx));
    return k_success;
}

static ErrorCodeOr<int> Main(ArgsCstr) {
    WebGlobalInit();
    DEFER { WebGlobalCleanup(); };

    DynamicArray<char> output {PageAllocator::Instance()};
    json::WriteContext ctx {.out = dyn::WriterFor(output), .add_whitespace = true};

    TRY(GenerateAllData(ctx));

    TRY(StdPrint(StdStream::Out, output.ToOwnedSpan()));

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
