# Sample Libraries

> What is a Floe sample library

Sample libraries are where Floe's sounds come from: collections of mapped, playable sounds along with images, tags and other details. Without a sample library, Floe can't make any sound.

A library usually has a theme or focus. 'Music Box Suite', for example, is multi-sampled music boxes, while 'Lost Reveries' is a collection of single-sample ambient tones.

A sample library can contain [Instruments](/docs/reference/instruments), impulse responses, or both. Most focus on Instruments, but some contain only impulse responses, giving the [Convolution Reverb](/docs/usage/effects#convolution-reverb) new spaces and textures to work with.

You don't load a library itself. Instead, you load its Instruments into Floe's layers and its impulse responses into the Convolution Reverb. To see which libraries you have installed, open the menu at the top of Floe's window and choose _Info_.

## What's inside a library

Floe sample libraries use Floe's own open format: portable folders of audio files and configuration files, made with Floe's [developer tools](/docs/develop/develop-libraries). A library's folder usually has a name like `Developer Name - Library Name`, and inside it a `Samples` subfolder and a file called `floe.lua`. Libraries can hold thousands of audio files and take up many gigabytes.
