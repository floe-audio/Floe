// SPDX-FileCopyrightText: 2026 Sam Windell
// SPDX-License-Identifier: MIT

// What a sample library can declare it has in its manual.json, so its PDF manual only explains what
// applies to it. See LibraryFeature.
export const k_library_features = [
    // Instruments made of several samples, such as a drum kit or a sampled piano
    'multi-sampled-instruments',
    // Instruments sampled at many pitches of the same sound, so Transpose reaches different recordings of it
    'pitched-multisamples',
    // Instruments whose velocity chooses between different samples
    'velocity-layers',
    'round-robins',
    'release-samples',
    // Samples marked as always or never keytracked
    'keytrack-marked-samples',
    // Instruments that the Timbre control sweeps between layers of samples
    'timbre-layers',
    'sliced-loops',
    'built-in-loops',
    'named-key-ranges',
    'impulse-responses',
    // Delivered as an encrypted package (.floe-pkg-enc), installed with a license key
    'encrypted-package',
];

export function checkLibraryFeature(feature, context) {
    if (!k_library_features.includes(feature)) {
        throw new Error(`${context}: unknown library feature '${feature}', expected one of: ${k_library_features.join(', ')}`);
    }
}
