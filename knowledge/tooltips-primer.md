We have 2 complementary systems:
- 'value_popup' or 'immediate tooltips' are for displaying readouts that are not normally shown on the GUI when you hover a control. For example showing the dB readout on a volume knob.
- Tooltips are longer sentences to help the user understand how to use the control and Floe in general. Typically static text, although occasionally may be concatentated from a few strings.

Tooltips (help text):
- Include **only what's necessary**, concise but not dense. Get straight to the point, don't waffle. 1 sentence if possible. Rarely 2, or 3 sentences - to up to (only in extreme case) 4 sentences.
- Are written for our user: a musician/producer who knows the bare basics of production but it otherwise totally unfamiliar with Floe, they are trying to work out how to use Floe. However, they have a rough idea of what the common production terms like: levels, what a filter is, what an ADSR envelope roughly is, etc.
- Written in natural prose with full stops. Don't lead with "<name>: blah", instead speak naturally like "The <name> shapes the ..." or "Enable <name> to ...".
- British English
- Use \n\n if needed to separate topics
- It's necessary to research before writing - the tooltip must match what the code actually does
- Consider the greater context around the control - what the control relates on other parts of the GUI - our use is looking at the GUI when the tooltip appears. Consider if the control is inactive, if it has different modes (and therefore should have multiple tooltips depending on the mode)
- Style it as a professional, helpful, colleague explaining something.
- Explain WHY and HOW it's useful - we are trying to help the user become confident in using Floe
- Incorporate technical details such as 'brickwall limiter, 'LUFS meter', 'markers every 6dB', but typically nearer the bottom since they are aspects that only a power-user wants to know
- Anticipate why the user might be confused and address that
- Use "\n\nTip: ..." if there's a natural place to show a direcly-related handy feature/use-case that they might otherwise no know about
- If it's natural to do so, explain a little about the parameter's place in Floe (signal chain, before/after)
- When talking about Instruments (Floe's term for a sound-source loadable into a layer), use a capital letter, proper noun, 'Instrument'
- Consider using a proper noun for the control in question - but only if it doesn't clutter the readibility of the tooltip
- The complete text must have pleasant, natural word flow and read nicely from start to finish, as if it was said out-loud
- Menu items sometimes also need tooltips, but only if there's actually something useful to say. For example a menu item 'Sine Wave' on a 'LFO shape' menu doesn't need explaining - we don't need to describe what a sine wave is
- Avoid jargon. It's fine to use widely understood production terms or technical terms such as from Floe's glossary, but otherwise prefer natural language over dense 'developer talk'
- Don't say what the control _doesn't do_, just say the affirmative - avoid ', rather than'.
