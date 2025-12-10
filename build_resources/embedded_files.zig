// SPDX-FileCopyrightText: 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

const std = @import("std");

const c = @cImport({
    @cInclude("embedded_files.h");
});

const build_options = @import("build_options");

fn embeddedString(comptime str: []const u8) c.EmbeddedString {
    return .{ .data = str.ptr, .size = str.len };
}

fn embeddedFile(comptime filename: []const u8) c.BinaryData {
    const data = @embedFile(filename);
    return .{
        .data = data,
        .size = data.len,
        .filename = embeddedString(filename),
    };
}

export fn EmbeddedFontAwesome() c.BinaryData {
    return embeddedFile("fonts/fa-solid-900.ttf");
}
export fn EmbeddedRoboto() c.BinaryData {
    return embeddedFile("fonts/Roboto-Regular.ttf");
}
export fn EmbeddedRobotoItalic() c.BinaryData {
    return embeddedFile("fonts/Roboto-Italic.ttf");
}

export fn EmbeddedDefaultBackground() c.BinaryData {
    return embeddedFile("images/default-background.jpg");
}

export fn EmbeddedUnknownLibraryIcon() c.BinaryData {
    return embeddedFile("images/unknown-library-icon.png");
}

export fn EmbeddedLogoImage() c.BinaryData {
    if (build_options.logo_file) |p| {
        return embeddedFile(p);
    } else {
        return .{};
    }
}

export fn EmbeddedIconImage() c.BinaryData {
    if (build_options.icon_file) |p| {
        return embeddedFile(p);
    } else {
        return .{};
    }
}

export fn EmbeddedAboutLibraryTemplateRtf() c.BinaryData {
    return embeddedFile("about_library_template.rtf");
}

export fn EmbeddedPackageInstallationRtf() c.BinaryData {
    return embeddedFile("package_installation.rtf");
}

const embedded_irs = [_]c.EmbeddedIr{
    // Bizarre FX
    .{
        .data = embeddedFile("reverb_irs/bizarre_fx/cyber-pulse.flac"),
        .id = embeddedString("Cyber Pulse"),
        .folder = embeddedString("Bizarre FX"),
        .tag1 = embeddedString("synthesized"),
        .tag2 = embeddedString("unusual"),
        .description = embeddedString(""),
    },
    .{
        .data = embeddedFile("reverb_irs/bizarre_fx/dropping-pitch.flac"),
        .id = embeddedString("Dropping Pitch"),
        .folder = embeddedString("Bizarre FX"),
        .tag1 = embeddedString("synthesized"),
        .tag2 = embeddedString("unusual"),
        .description = embeddedString(""),
    },
    .{
        .data = embeddedFile("reverb_irs/bizarre_fx/flickering.flac"),
        .id = embeddedString("Flickering"),
        .folder = embeddedString("Bizarre FX"),
        .tag1 = embeddedString("synthesized"),
        .tag2 = embeddedString("unusual"),
        .description = embeddedString(""),
    },
    .{
        .data = embeddedFile("reverb_irs/bizarre_fx/fm-flicker.flac"),
        .id = embeddedString("FM Flicker"),
        .folder = embeddedString("Bizarre FX"),
        .tag1 = embeddedString("synthesized"),
        .tag2 = embeddedString("unusual"),
        .description = embeddedString(""),
    },
    .{
        .data = embeddedFile("reverb_irs/bizarre_fx/low-vibration.flac"),
        .id = embeddedString("Low Vibration"),
        .folder = embeddedString("Bizarre FX"),
        .tag1 = embeddedString("synthesized"),
        .tag2 = embeddedString("unusual"),
        .description = embeddedString(""),
    },
    .{
        .data = embeddedFile("reverb_irs/bizarre_fx/passby.flac"),
        .id = embeddedString("Passby"),
        .folder = embeddedString("Bizarre FX"),
        .tag1 = embeddedString("synthesized"),
        .tag2 = embeddedString("unusual"),
        .description = embeddedString(""),
    },
    .{
        .data = embeddedFile("reverb_irs/bizarre_fx/synth-didgeridoo.flac"),
        .id = embeddedString("Synth Didgeridoo"),
        .folder = embeddedString("Bizarre FX"),
        .tag1 = embeddedString("synthesized"),
        .tag2 = embeddedString("unusual"),
        .description = embeddedString(""),
    },

    // Found Sounds
    .{
        .data = embeddedFile("reverb_irs/found_sounds/creaky-door.flac"),
        .id = embeddedString("Creaky Door"),
        .folder = embeddedString("Found Sounds"),
        .tag1 = embeddedString("found sounds"),
        .tag2 = embeddedString("unusual"),
        .description = embeddedString(""),
    },
    .{
        .data = embeddedFile("reverb_irs/found_sounds/crunch-1.flac"),
        .id = embeddedString("Crunch 1"),
        .folder = embeddedString("Found Sounds"),
        .tag1 = embeddedString("found sounds"),
        .tag2 = embeddedString("unusual"),
        .description = embeddedString(""),
    },
    .{
        .data = embeddedFile("reverb_irs/found_sounds/crunch-2.flac"),
        .id = embeddedString("Crunch 2"),
        .folder = embeddedString("Found Sounds"),
        .tag1 = embeddedString("found sounds"),
        .tag2 = embeddedString("unusual"),
        .description = embeddedString(""),
    },
    .{
        .data = embeddedFile("reverb_irs/found_sounds/rain.flac"),
        .id = embeddedString("Rain"),
        .folder = embeddedString("Found Sounds"),
        .tag1 = embeddedString("found sounds"),
        .tag2 = embeddedString("unusual"),
        .description = embeddedString(""),
    },
    .{
        .data = embeddedFile("reverb_irs/found_sounds/rattle.flac"),
        .id = embeddedString("Rattle"),
        .folder = embeddedString("Found Sounds"),
        .tag1 = embeddedString("found sounds"),
        .tag2 = embeddedString("unusual"),
        .description = embeddedString(""),
    },
    .{
        .data = embeddedFile("reverb_irs/found_sounds/water-walkoff.flac"),
        .id = embeddedString("Water Walkoff"),
        .folder = embeddedString("Found Sounds"),
        .tag1 = embeddedString("found sounds"),
        .tag2 = embeddedString("unusual"),
        .description = embeddedString(""),
    },
    .{
        .data = embeddedFile("reverb_irs/found_sounds/water.flac"),
        .id = embeddedString("Water"),
        .folder = embeddedString("Found Sounds"),
        .tag1 = embeddedString("found sounds"),
        .tag2 = embeddedString("unusual"),
        .description = embeddedString(""),
    },
    .{
        .data = embeddedFile("reverb_irs/found_sounds/wind.flac"),
        .id = embeddedString("Wind"),
        .folder = embeddedString("Found Sounds"),
        .tag1 = embeddedString("found sounds"),
        .tag2 = embeddedString("unusual"),
        .description = embeddedString(""),
    },

    // Simulated Space
    .{
        .data = embeddedFile("reverb_irs/simulated_space/chamber-like-1.flac"),
        .id = embeddedString("Chamber-like 1"),
        .folder = embeddedString("Simulated Space"),
        .tag1 = embeddedString("synthesized"),
        .tag2 = embeddedString("chamber"),
        .description = embeddedString(""),
    },
    .{
        .data = embeddedFile("reverb_irs/simulated_space/chamber-like-2.flac"),
        .id = embeddedString("Chamber-like 2"),
        .folder = embeddedString("Simulated Space"),
        .tag1 = embeddedString("synthesized"),
        .tag2 = embeddedString("chamber"),
        .description = embeddedString(""),
    },
    .{
        .data = embeddedFile("reverb_irs/simulated_space/chamber-like-3.flac"),
        .id = embeddedString("Chamber-like 3"),
        .folder = embeddedString("Simulated Space"),
        .tag1 = embeddedString("synthesized"),
        .tag2 = embeddedString("chamber"),
        .description = embeddedString(""),
    },
    .{
        .data = embeddedFile("reverb_irs/simulated_space/cathedral-like-1.flac"),
        .id = embeddedString("Cathedral-like 1"),
        .folder = embeddedString("Simulated Space"),
        .tag1 = embeddedString("synthesized"),
        .tag2 = embeddedString("cathedral"),
        .description = embeddedString(""),
    },
    .{
        .data = embeddedFile("reverb_irs/simulated_space/cathedral-like-2.flac"),
        .id = embeddedString("Cathedral-like 2"),
        .folder = embeddedString("Simulated Space"),
        .tag1 = embeddedString("synthesized"),
        .tag2 = embeddedString("cathedral"),
        .description = embeddedString(""),
    },

    // Smooth
    .{
        .data = embeddedFile("reverb_irs/smooth/airy.flac"),
        .id = embeddedString("Airy"),
        .folder = embeddedString("Smooth"),
        .tag1 = embeddedString("synthesized"),
        .tag2 = embeddedString("smooth"),
        .description = embeddedString(""),
    },
    .{
        .data = embeddedFile("reverb_irs/smooth/formant.flac"),
        .id = embeddedString("Formant"),
        .folder = embeddedString("Smooth"),
        .tag1 = embeddedString("synthesized"),
        .tag2 = embeddedString("smooth"),
        .description = embeddedString(""),
    },
    .{
        .data = embeddedFile("reverb_irs/smooth/lush-modulated-1.flac"),
        .id = embeddedString("Lush Modulated 1"),
        .folder = embeddedString("Smooth"),
        .tag1 = embeddedString("synthesized"),
        .tag2 = embeddedString("smooth"),
        .description = embeddedString(""),
    },
    .{
        .data = embeddedFile("reverb_irs/smooth/lush-modulated-2.flac"),
        .id = embeddedString("Lush Modulated 2"),
        .folder = embeddedString("Smooth"),
        .tag1 = embeddedString("synthesized"),
        .tag2 = embeddedString("smooth"),
        .description = embeddedString(""),
    },
    .{
        .data = embeddedFile("reverb_irs/smooth/lush-modulated-3.flac"),
        .id = embeddedString("Lush Modulated 3"),
        .folder = embeddedString("Smooth"),
        .tag1 = embeddedString("synthesized"),
        .tag2 = embeddedString("smooth"),
        .description = embeddedString(""),
    },
    .{
        .data = embeddedFile("reverb_irs/smooth/lush-tail.flac"),
        .id = embeddedString("Lush Tail"),
        .folder = embeddedString("Smooth"),
        .tag1 = embeddedString("synthesized"),
        .tag2 = embeddedString("smooth"),
        .description = embeddedString(""),
    },
    .{
        .data = embeddedFile("reverb_irs/smooth/rough-crackle.flac"),
        .id = embeddedString("Rough Crackle"),
        .folder = embeddedString("Smooth"),
        .tag1 = embeddedString("synthesized"),
        .tag2 = embeddedString("smooth"),
        .description = embeddedString(""),
    },
    .{
        .data = embeddedFile("reverb_irs/smooth/white-noise-1.flac"),
        .id = embeddedString("White Noise 1"),
        .folder = embeddedString("Smooth"),
        .tag1 = embeddedString("synthesized"),
        .tag2 = embeddedString("smooth"),
        .description = embeddedString(""),
    },
    .{
        .data = embeddedFile("reverb_irs/smooth/white-noise-2.flac"),
        .id = embeddedString("White Noise 2"),
        .folder = embeddedString("Smooth"),
        .tag1 = embeddedString("synthesized"),
        .tag2 = embeddedString("smooth"),
        .description = embeddedString(""),
    },
    .{
        .data = embeddedFile("reverb_irs/smooth/white-noise-3.flac"),
        .id = embeddedString("White Noise 3"),
        .folder = embeddedString("Smooth"),
        .tag1 = embeddedString("synthesized"),
        .tag2 = embeddedString("smooth"),
        .description = embeddedString(""),
    },
    .{
        .data = embeddedFile("reverb_irs/smooth/white-noise-4.flac"),
        .id = embeddedString("White Noise 4"),
        .folder = embeddedString("Smooth"),
        .tag1 = embeddedString("synthesized"),
        .tag2 = embeddedString("smooth"),
        .description = embeddedString(""),
    },
    .{
        .data = embeddedFile("reverb_irs/smooth/white-noise-5.flac"),
        .id = embeddedString("White Noise 5"),
        .folder = embeddedString("Smooth"),
        .tag1 = embeddedString("synthesized"),
        .tag2 = embeddedString("smooth"),
        .description = embeddedString(""),
    },
    .{
        .data = embeddedFile("reverb_irs/smooth/white-noise-6.flac"),
        .id = embeddedString("White Noise 6"),
        .folder = embeddedString("Smooth"),
        .tag1 = embeddedString("synthesized"),
        .tag2 = embeddedString("smooth"),
        .description = embeddedString(""),
    },
};

export fn GetEmbeddedIrs() c.EmbeddedIrs {
    return .{
        .irs = &embedded_irs,
        .count = embedded_irs.len,
    };
}
