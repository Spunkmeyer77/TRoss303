# Changelog

## 1.2.1
- **TRANSP is per pattern**: the TRANSP readout now shows and sets the selected pattern's own transpose (−24…+24), so e.g. I-1A can be +5 and I-2A −3. It follows the selected pattern; double-click resets to 0. The old single global transpose is removed.

## 1.2.0
- **MIDI export**: the MIDI EXPORT key saves the selected pattern, or the whole chain when CHAIN is on, as a .mid file (accent = velocity 127, slides as overlapping notes, both transposes, Live's tempo).
- **SHIFT + RANDOM** makes a small variation of the pattern (2–4 steps change), respecting SCALE, ROOT and LOCK.
- **Per-pattern transpose** (−12…+12): drag up/down on the LCD (TRANSP readout from 1.2.1). Saved, copied, pasted and undone with the pattern. Older sets load with 0.
- **Bigger LCD** with larger text: pattern, length (L16), transpose (T+3), PLAY/STOP, NEXT pattern, chain info.
- The **acid smiley** appears whenever the LCD is touched (and on RANDOM / variation).
- **Red pattern LEDs follow the playing pattern**; a pattern queued to play next blinks.
- While a **chain** plays, the step editor and LCD follow the pattern that is sounding.
- Every key shows **pressed** feedback.

## 1.1.1
- Fix: the LOCK keys turned on at load (so RANDOM changed nothing) and SEQ could start off.

## 1.1.0
- Silver Classic panel: chicken-head knobs, rubber keys, LEDs, green LCD with acid smiley, gold keys.

## 1.0.1
- Fix: the panel drawing hid the controls.

## 1.0.0
- First release.
