// SPDX-FileCopyrightText: 2026 Sam Windell
// SPDX-License-Identifier: MIT

import { useManualLibrary } from './ManualContext';

// A link to the product page of the sample library a library manual is for. Only for use inside
// <ManualOnly types={['library']}>.
export default function LibraryProductLink({ children }) {
    const library = useManualLibrary();
    if (library === null) throw new Error("LibraryProductLink: only valid inside <ManualOnly types={['library']}>");
    return <a href={library.productUrl}>{children}</a>;
}
