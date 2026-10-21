# Browsers

> How to find and load presets, Instruments and impulse responses with Floe's browsers.

The browsers help you find the right items quickly, without scrolling through long lists. Think of them as supercharged drop-down menus.

There are three browsers, and they all work the same way. Each one opens when you click the name of the item it loads:

-   **Preset browser** — click the preset's name in the [top panel](/docs/usage/top-panel), which is there on every page.
-   **Instrument browser** — click the Instrument name at the top of a layer, on the [_Layers_](/docs/usage/layers#layer-header) page.
-   **IR browser** — click the IR name in the [Convolution Reverb](/docs/usage/effects#convolution-reverb) effect, on the _Effects_ page.

![Layer 1's Instrument browser, open beneath its selector, with its regions numbered](https://assets.floe.audio/website/screenshots/browser-browse.73d96a92f8a5fdb2.png)12345

1.  **Items** — the light panel on the left lists everything you can load. Click one to load it.
2.  **Narrowing down** — the dark panel on the right is for narrowing down the list on the left, such as to one sample library or one tag.
3.  **Selector** — the box you clicked to open the browser. It stays usable while the browser is open.
4.  **Navigation** — step back and forth through the pages of the right-hand panel.
5.  **Mode** — switch the right-hand panel between browse mode and [filter mode](#filter-mode).

## Loading items

Click an item (1) to load it. The browser stays open, so you can try one sound after another, playing as you go. Once you've loaded something, the browser closes by itself as soon as you move the mouse away from it. You can also double-click an item to load it and close the browser in one go, or close it at any time by clicking outside it or pressing `escape`.

While the browser is open, the selector you clicked (3) can still be used: previous, next, random and unload, where it has them.

Hover over an item for a moment to see more about it, such as its description.

## Favourites

If there are items you keep coming back to, mark them as favourites. Hover over an item and click the next to its name. The star stays lit to show it's a favourite. To see only your favourites, click the in the toolbar at the bottom of the list. Click it again to see everything. Favourites are shared by every instance of Floe, so anything you star in one project is there in the next.

## The list of items

The left panel (1) starts off showing everything, and shrinks as you narrow it down with the right panel.

Beneath the list, a line of italic text says what you're looking at, such as _Showing all instruments_. As you narrow things down it changes to match, such as _Showing from Lost Reveries_. Each part of it that narrows the list has a : click one to remove it.

At the very bottom is a small toolbar:

-   — jump to the item that's currently loaded. The right panel jumps to where it came from, too.
-   — show only your favourites.
-   — search. As you type, the list shows only the items containing your text. `ctrl`+`F` (`cmd`+`F` on macOS) opens it too.

## Narrowing down

The right panel (2) has pages that you click through to narrow down the list. In the Instrument browser, for example, you might click _Libraries_, then a particular sample library. The list on the left then shows only that library's Instruments. Numbers in brackets, such as _(26)_, show how many items each option holds.

The first page offers two ways in. _Browse by collection_ narrows down by where items come from. _Browse by attribute_ narrows down by their properties, such as their tags or who made them.

As you click through the pages, a trail of the pages you've visited builds up at the bottom of the panel (4) — a bit like a web browser. Click any step to jump back to it, or click to go back to the first page. The and arrows step back and forth.

## Right-click menus

Right-click items in the browsers for options to do with their files:

-   **A preset** — open the folder it's in, make it your [default preset](/docs/presets/loading-presets#default-preset), or send it to the bin.
-   **A preset bank or folder** — open it in your file manager, or uninstall it.
-   **A sample library** — open its folder, or uninstall it. See [Uninstalling](/docs/installation/uninstalling).

## Keyboard navigation

You can use the browsers from your computer keyboard. The focused item gets a blue outline, which appears once you press an arrow key.

-   `up`/`down` move through the list.
-   `enter` clicks the focused item.
-   `tab` moves between the left and right panels.
-   `left`/`right` move along the trail at the bottom of the right panel.
-   `ctrl`+`F` (`cmd`+`F` on macOS) opens the search.
-   `home`/`end` jump to the start or end of the list.
-   `ctrl`+`up`/`down` (`cmd` on macOS) jump to the previous or next folder.
-   `page up`/`page down` jump 8 items at a time.

## Filter mode

Browse mode, where you click through one page at a time, is all most people need. When you know exactly what you're after, filter mode goes further: it lets you combine several ways of narrowing down at once, such as showing only Instruments tagged _ambient_ from one particular sample library.

Switch to it with the button (5), and back to browse mode with .

![The Instrument browser in filter mode, with its regions numbered](https://assets.floe.audio/website/screenshots/browser-full.89de4f11b56240f7.png)1234

1.  **Filters** — every option in one tree, rather than pages to click through. Click a to open a section, then click the options you want.
2.  **Summary** — what's currently showing. Click the next to a filter to remove it.
3.  **Mode** — switch between browse mode and filter mode.
4.  **Matching** — choose how your filters combine.

The list on the left updates as soon as you click an option.

Choose how the filters combine with the button next to the mode switch (4):

![The One, Match all and Match any menu](https://assets.floe.audio/website/screenshots/browser-menu.5ef644a9f8155d92.png)

-   **Match all (AND)** — the one you'll usually want. Only items that match every selected filter are shown, so the more filters you choose, the shorter the list gets. Filters that would leave nothing to show are greyed out, so you don't hit dead ends.
-   **Match any (OR)** — items that match any of the selected filters are shown, so the more filters you choose, the longer the list gets.
-   **One** — only one filter at a time, like browse mode but with the tree layout.

Filter mode has its own search, at the bottom of the right panel, for finding a filter in the tree, such as the _aggressive_ tag.

Filter mode pairs well with the random button: narrow the list down to the kind of sound you're after, then jump around within it for inspiration. See [Randomisation](/docs/usage/randomisation).

## Tips

-   The previous, next and random buttons ( ) always stay within the list you've narrowed down to, whether the browser is open or closed. On the _Perform_ page and in the top panel, the preset arrows follow the preset browser in the same way.
-   Drag the bottom-right corner of a browser to resize it. To make room for longer names, drag the line where the two panels meet. Double-click either to reset it.
-   Each layer's Instrument browser remembers its own settings. On the _Layers_ page, set up each one the way you like, perhaps the first narrowed to the _bass_ tag and the second to _keys_. Then click the button that randomises every layer, and each one jumps to a random Instrument within its own selection.
