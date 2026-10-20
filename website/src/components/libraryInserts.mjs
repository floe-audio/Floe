// SPDX-FileCopyrightText: 2026 Sam Windell
// SPDX-License-Identifier: MIT

// Places in the docs where a sample library's manual can add its own MDX, or replace a general example
// with one about the library. Inserts describe the library's content, never Floe's behaviour, so they
// don't go stale when Floe changes. Mapped from slot names to files in the "inserts" of its manual.json. See
// LibraryInsert.
export const k_library_insert_slots = [
    // After the FAQ's questions, for questions about the library itself
    'faq',
    // Playback tab, after the waveform display: which of the library's Instruments are single samples
    // and which are multisamples
    'waveform-display',
    // Arp tab: the kind of Instrument Fixed Notes suits
    'arp-fixed-notes-use',
    // Arp tab: an example of recording a Fixed Notes pattern
    'arp-record-example',
    // Folders: how to recognise the library's and its presets' folders
    'folder-names',
    // Install Packages and the Quick Start Guide, download step: where to download the library's files from
    'package-download',
    // System Requirements, after Floe's requirements: what the library itself needs, such as disk space
    // or a minimum Floe version
    'requirements',
    // Install Packages, before the updates paragraph: how you hear about a new version of the library
    'package-updates',
];

export function checkLibraryInsertSlot(slot, context) {
    if (!k_library_insert_slots.includes(slot)) {
        throw new Error(`${context}: unknown library insert slot '${slot}', expected one of: ${k_library_insert_slots.join(', ')}`);
    }
}

// Places between docs where a sample library's manual can add whole pages of its own, each with its own
// contents entry. Mapped from slot names to lists of files in the "pages" of its manual.json. Each slot
// names the doc its pages follow, in order. A page's only H1 is its title.
export const k_library_page_slots = {
    // For pages to read once the library is installed and playing, such as its story or preset categories
    'after-quick-start': 'getting-started/quick-start-guide',
};

export function checkLibraryPageSlot(slot, context) {
    if (!(slot in k_library_page_slots)) {
        throw new Error(`${context}: unknown library page slot '${slot}', expected one of: ${Object.keys(k_library_page_slots).join(', ')}`);
    }
}
