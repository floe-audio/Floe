# General Usage

> General usage and tips and tricks for working with Floe

Floe is an audio plugin for your digital audio workstation (DAW). It's designed to be efficient with your CPU and can be added multiple times in a single project. In most cases, multiple instances of Floe will share resources such as sample libraries to keep memory usage low.

## Resizing the window

Floe's window is fully scalable. It has a fixed aspect ratio but can be scaled up or down to any size.

Simply grab the bottom-right corner of the window (the 2 small diagonal 'grabber' lines) and drag to resize it. ![Resize corner](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABYAAAAWBAMAAAA2mnEIAAAAG1BMVEUaGiEcHCAdHh8gISI9P0FCRUdfY2Z0eHyQlps9XDCbAAAAWElEQVQY02NQggMFBtLYCQi2WgGCnW4AZ6sVI9SnG8HZasUIc9KNlAIY4MKqDQxw4QgHBrhwC1Q9SNgJwgYLQ80BC0PYEGEIGyIMZkOFwWyoMIgNE1YSAAAFsSoHkNDFJwAAAABJRU5ErkJggg==)

Alternatively, open Floe's preferences panel and use the buttons next to _window size_ to increase or decrease the size.

In some DAWs such as Logic Pro, you must grab the bottom-right corner of the plugin window itself (not the DAW's window that wraps around it) to resize it.

## Interacting with the UI

Floe's knobs and sliders can be adjusted by clicking and dragging with your mouse. They work in both an up/down direction and a left/right direction - use whichever feels most comfortable to you.

Holding down the `shift` key on your keyboard while dragging will allow for finer adjustments.

Clicking on a control while holding down the `ctrl` key (or `cmd` key on macOS) will reset it to its default value.

Additionally for most controls, you can double-click on them to enter a precise value using your keyboard, and you can right-click on them to bring up a context menu with additional options.

## Tooltips

Hovering your mouse cursor over most elements in the Floe interface will display a tooltip with a brief description of that element's function.

![Tooltip example](/assets/images/tooltips-04c73047e2a8b88d0406f7c656041ab1.png)

You can disable tooltips in the preferences panel if you find them distracting.

## Backdrop images

Floe's backdrop image changes depending on what library is loaded. However, if you mix instruments from different libraries, the backdrop returns to the default style except each layer features a slight tint from the respective library's colour scheme.

## MIDI control

Setup Floe to work with your MIDI controller by following the instructions on the [MIDI](/docs/usage/midi) page.
