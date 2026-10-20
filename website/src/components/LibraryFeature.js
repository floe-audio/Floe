// SPDX-FileCopyrightText: 2026 Sam Windell
// SPDX-License-Identifier: MIT

import { useManualLibrary } from './ManualContext';
import { checkLibraryFeature } from './libraryFeatures.mjs';

// Curates content that depends on what a sample library contains, using the features its manual.json
// declares. The website and Floe's manual describe everything, so there:
// - has: always shown
// - lacks: never shown
// - unless: always shown
// In a library manual, has is shown when the library declares the feature, and lacks and unless when it
// doesn't.
export default function LibraryFeature({ has, lacks, unless, children }) {
    const given = [has, lacks, unless].filter(prop => prop !== undefined);
    if (given.length !== 1) throw new Error('LibraryFeature: give exactly one of has, lacks or unless');
    const feature = given[0];
    checkLibraryFeature(feature, 'LibraryFeature');
    const library = useManualLibrary();
    if (library === null) return lacks === undefined ? children : null;
    return library.features.includes(feature) === (has !== undefined) ? children : null;
}
