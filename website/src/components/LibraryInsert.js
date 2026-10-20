// SPDX-FileCopyrightText: 2026 Sam Windell
// SPDX-License-Identifier: MIT

import { useContext } from 'react';
import { ManualContext } from './ManualContext';
import { checkLibraryInsertSlot } from './libraryInserts.mjs';

// Where a library manual shows the MDX file its manual.json gives for this slot, in place of the
// children. The children are the general version, shown on the website, in Floe's manual, and in
// library manuals with nothing for the slot.
export default function LibraryInsert({ slot, children }) {
    checkLibraryInsertSlot(slot, 'LibraryInsert');
    const Content = useContext(ManualContext)?.libraryInserts?.[slot];
    if (Content) return <Content />;
    return children ?? null;
}
