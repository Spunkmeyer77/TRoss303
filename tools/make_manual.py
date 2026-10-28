#!/usr/bin/env python3
"""Builds docs/TRoss303_Manual.pdf.  Needs reportlab (pip install reportlab).

    python3 tools/make_manual.py [out.pdf]
"""
import sys
from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.platypus import (Image, PageBreak, Paragraph, SimpleDocTemplate, Spacer, Table,
                                TableStyle)

VERSION = "1.2.1"
ROOT = Path(__file__).resolve().parent.parent
RED = colors.HexColor("#c0261a")
DARK = colors.HexColor("#1c1c1f")
SILVER = colors.HexColor("#d6d6d2")

ss = getSampleStyleSheet()
H1 = ParagraphStyle("H1", parent=ss["Heading1"], textColor=DARK, fontSize=17, spaceBefore=6, spaceAfter=6)
H2 = ParagraphStyle("H2", parent=ss["Heading2"], textColor=RED, fontSize=12.5, spaceBefore=10, spaceAfter=4)
H3 = ParagraphStyle("H3", parent=ss["Heading3"], textColor=DARK, fontSize=10.5, spaceBefore=6, spaceAfter=3)
P = ParagraphStyle("P", parent=ss["BodyText"], fontSize=9.6, leading=13.2, alignment=TA_LEFT)
SMALL = ParagraphStyle("S", parent=P, fontSize=8.2, leading=11, textColor=colors.HexColor("#444444"))
CODE = ParagraphStyle("C", parent=P, fontName="Courier", fontSize=8.6, leading=11.5, backColor=colors.HexColor("#f0f0ee"),
                      borderPadding=4, leftIndent=4, rightIndent=4, spaceBefore=3, spaceAfter=6)


def table(rows, widths):
    t = Table([[Paragraph(str(c), P) for c in r] for r in rows], colWidths=widths)
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), DARK),
        ("TEXTCOLOR", (0, 0), (-1, 0), colors.white),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("GRID", (0, 0), (-1, -1), 0.4, colors.HexColor("#bbbbbb")),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, colors.HexColor("#f4f4f2")]),
    ]))
    for i in range(len(rows[0])):
        t._cellvalues[0][i] = Paragraph(f"<b><font color='white'>{rows[0][i]}</font></b>", P)
    return t


def header_footer(canvas, doc):
    canvas.saveState()
    w, h = A4
    canvas.setFillColor(SILVER)
    canvas.rect(0, h - 16 * mm, w, 16 * mm, stroke=0, fill=1)
    canvas.setFillColor(RED)
    canvas.rect(0, h - 16.6 * mm, w, 0.6 * mm, stroke=0, fill=1)
    canvas.setFillColor(DARK)
    canvas.setFont("Helvetica-Bold", 13)
    canvas.drawString(15 * mm, h - 10.5 * mm, "TRoss 303")
    canvas.setFillColor(RED)
    canvas.setFont("Helvetica-Bold", 8)
    canvas.drawString(41 * mm, h - 10.5 * mm, "ACID BASS  -  USER MANUAL  v" + VERSION)
    canvas.setFillColor(colors.HexColor("#555555"))
    canvas.setFont("Helvetica", 7.5)
    canvas.drawString(15 * mm, 9 * mm, "Ticco Ross  -  ticcoross.gumroad.com  -  ticcoross@gmail.com")
    canvas.drawRightString(w - 15 * mm, 9 * mm, f"page {doc.page}")
    canvas.restoreState()


def build(out):
    s = []
    s.append(Paragraph("TRoss 303 &mdash; Acid Bass for Max for Live", H1))
    s.append(Paragraph(
        "TRoss 303 is a TB-303 style bass synthesizer with a built-in 303-style step sequencer, 64-pattern "
        "memory, pattern chaining and a random pattern creator. The sound comes from the <b>Lemus ro-mono-303</b> "
        "engine: a part-by-part circuit model of the original voice board (pitch and slide, VCO, the "
        "four-pole transistor ladder filter, envelopes, accent sweep, VCA and output stage), compiled into the "
        "device so it sounds the same as the Lemus plugin.", P))
    s.append(Spacer(1, 4 * mm))
    img = ROOT / "docs" / "panel.png"
    if img.exists():
        s.append(Image(str(img), width=180 * mm, height=180 * mm * 169 / 760))
        s.append(Paragraph("The device panel (preview rendering).", SMALL))

    s.append(Paragraph("1. Installing", H2))
    s.append(Paragraph(
        "Unzip the download. You get a folder <b>TRoss 303</b> containing the device, the panel script and the "
        "engine for Mac (<i>tross303~.mxo</i>) and Windows (<i>tross303~.mxe64</i>). Keep these files together "
        "in the same folder, and drag the whole folder into the <b>User Library</b> in Live's browser. Then drag "
        "<b>TRoss 303.amxd</b> onto a MIDI track.", P))
    s.append(Paragraph(
        "<b>macOS:</b> files downloaded from the web are quarantined, and Live won't load the engine until the "
        "quarantine flag is removed. Run this once in Terminal (adjust the path to where the folder is):", P))
    s.append(Paragraph('xattr -dr com.apple.quarantine ~/Music/Ableton/User\\ Library/TRoss\\ 303', CODE))
    s.append(Paragraph(
        "<b>Single-file device:</b> to make one self-contained .amxd (for example to share or sell), open the "
        "device in the Max editor, press <b>Freeze Device</b> in the toolbar, and save. Freezing embeds the panel "
        "script and both engine files.", P))
    s.append(Paragraph("Requirements: Ableton Live 11 or 12 with Max for Live (Max 8.6 or newer), macOS 11+ "
                       "(Apple Silicon or Intel) or Windows 10/11 64-bit.", SMALL))

    s.append(Paragraph("2. Sound controls", H2))
    s.append(table([
        ["Control", "What it does"],
        ["TUNING", "Fine pitch, about a fifth either way (centre = in tune)."],
        ["CUT OFF", "Filter cutoff of the 4-pole ladder."],
        ["RESO", "Resonance. High settings give the classic squelch."],
        ["ENV MOD", "How far the filter envelope sweeps the cutoff."],
        ["DECAY", "Filter envelope decay time (accented notes always use the short decay, like the original)."],
        ["ACCENT", "How strongly accented steps (and MIDI notes with velocity 100+) hit the filter and VCA."],
        ["VOLUME", "Output level."],
        ["WAVEFORM", "Saw or square oscillator: click the switch."],
        ["CUT MAX", "(Readout at the top.) How high the CUT OFF knob reaches at full (1200-3000 Hz). Real units differ: 2300 Hz is a "
                    "typical one, lower is darker."],
        ["TRANSP", "The <b>selected pattern's own transpose</b>, -24 to +24 semitones. Drag the readout up or down; "
                   "double-click it to reset to 0. Each of the 64 patterns keeps its own value (see section 5)."],
        ["SEQ", "Click the SEQ LED to turn the built-in sequencer on or off. With it off, the device is a MIDI-played 303 voice."],
    ], [30 * mm, 145 * mm]))
    s.append(Paragraph("Every control is a Live parameter: you can automate it, map it to MIDI or a Push, and it's "
                       "saved with your set.", SMALL))

    s.append(Paragraph("3. Playing from MIDI", H2))
    s.append(Paragraph(
        "MIDI notes on the track play the voice too, the way Roland's own TB-03/TD-3 take MIDI: <b>velocity 100 or "
        "more is an accent</b> (velocity is otherwise ignored), and a note that starts while another is still held "
        "<b>slides</b> to its pitch without retriggering. Overlap notes in a clip to get slides. While you hold MIDI "
        "notes they take over from the sequencer.", P))
    s.append(Paragraph(
        "TRoss 303 is an instrument, so it doesn't send MIDI out to other tracks (Max for Live instruments can't). "
        "To get its patterns as MIDI, use <b>MIDI EXPORT</b> (section 8).", P))

    s.append(Paragraph("4. The sequencer", H2))
    s.append(Paragraph(
        "The sequencer follows Live's transport: press Play in Live and it starts from step 1 of the current pattern "
        "(or of the chain), in sync with Live's tempo; press Stop and it stops. Each pattern has up to 16 steps, "
        "and every step has five lanes:", P))
    s.append(table([
        ["Lane", "How to edit", "Meaning"],
        ["NOTE", "Click and drag up/down. Clicking a rest step makes it a note. Alt-click turns it back into a rest.",
         "C to C' (the 303's 13 keys)."],
        ["OCT", "Click to cycle: none, UP, DN.", "One octave up or down."],
        ["ACCENT", "Click to toggle.", "Accented step: louder, brighter, faster filter sweep."],
        ["SLIDE", "Click to toggle.", "Glides from this step into the next one, without retriggering."],
        ["TIME", "Click to cycle: note (dot), tie (dash), rest (empty).",
         "Tie holds the previous note through this step. A tie after a rest plays nothing."],
    ], [20 * mm, 85 * mm, 70 * mm]))
    s.append(Paragraph(
        "The <b>STEP</b> LEDs above the steps show which step is playing (when the pattern you're editing is the "
        "one playing; while a chain plays, the step editor follows the pattern that is sounding). <b>Click a STEP LED to set the pattern length</b> to that step (1 to 16); steps past the end "
        "are dimmed. The LCD shows the pattern's length as <i>L16</i>, <i>L12</i> and so on.", P))

    s.append(Paragraph("5. Pattern memory", H2))
    s.append(Paragraph(
        "Like the TB-303 there are <b>64 patterns</b>: 4 groups (<b>I-IV</b>) &times; 8 patterns (<b>1-8</b>) "
        "&times; 2 sections (<b>A/B</b>). Pick a group, a pattern and a section to select one; the panel shows "
        "the pattern you're editing in large letters on the LCD, and <b>PLAY</b> with the one playing below it. As on the original, a new "
        "selection made while playing takes over when the current pattern reaches its end, so changes stay in time: "
        "the LCD shows it as <b>NEXT</b>.", P))
    s.append(Paragraph(
        "The <b>red LEDs</b> under the pattern keys 1-8 always show the pattern that is <b>playing</b> (when it's in the "
        "group and section shown). A pattern you selected that will play next <b>blinks</b>. When Live is stopped, "
        "the LED shows the selected pattern. The orange LEDs show the group (I-IV) and section (A/B) you're in.", P))
    s.append(Paragraph("The LCD", H3))
    s.append(table([
        ["Shows", "Meaning"],
        ["I-1A", "The pattern you're editing (large)."],
        ["L16", "Its length in steps."],
        ["T+3", "Its own transpose (see below)."],
        ["PLAY / STOP / NEXT", "What's playing, whether Live is stopped, and a pattern waiting to play next."],
        ["CHAIN", "Chain information while chaining."],
    ], [38 * mm, 137 * mm]))
    s.append(Paragraph(
        "<b>Touch the LCD</b> (click it) and the acid smiley appears.", P))
    s.append(Paragraph("Per-pattern transpose", H3))
    s.append(Paragraph(
        "Every pattern has its <b>own transpose</b>, -24 to +24 semitones. Select a pattern and drag the "
        "<b>TRANSP</b> readout (next to WAVEFORM) up or down; you can also drag up or down on the LCD. The LCD "
        "shows the value as T+5, T-3&hellip; For example: select I-1A and set TRANSP to +5, then select I-2A and "
        "set it to -3. I-1A now always plays 5 semitones up and I-2A 3 down, whether you switch between them or "
        "chain them. When you select another pattern, TRANSP shows that pattern's value. The transpose is saved "
        "with the pattern in your Live set, copied and pasted with it, kept by RANDOM, and exported to MIDI; "
        "double-click TRANSP to reset it, and UNDO takes back a whole drag.", P))
    s.append(table([
        ["Button", "What it does"],
        ["COPY / PASTE", "Copy the selected pattern, then paste it into another one."],
        ["CLEAR", "Clears the selected pattern (all rests, 16 steps). While chain EDIT is on, it clears the chain instead."],
        ["UNDO", "Undoes the last change to the selected pattern (random, variation, clear, paste, transpose or an "
                 "edit). Press again to redo."],
    ], [30 * mm, 145 * mm]))
    s.append(Paragraph("Every key on the panel goes down visibly while you press it. All 64 patterns and the "
                       "chain are saved inside your Live set, and each instance of the device has its own.", SMALL))

    s.append(Paragraph("6. Pattern chaining", H2))
    s.append(Paragraph(
        "A chain plays up to 16 patterns in order and loops (for example I-1A, I-1A, I-1B, I-2A). Press <b>EDIT</b> "
        "(next to CHAIN) and click pattern buttons to add them to the end of the chain; the current group and "
        "section are used, and clicking A or B adds that section; the LCD shows how many entries the chain has and the "
        "last one added. Press EDIT again to stop adding. <b>CHAIN</b> (the gold key; its LED is lit when on) turns "
        "chain playback on or off, and <b>CLEAR</b> pressed while EDIT is on empties the chain. With chain playback on, the sequencer "
        "plays the chain from its first entry each time Live starts. While the chain plays, the step editor, the LCD "
        "and the red pattern LEDs follow the pattern that is sounding, so you can watch (and edit) each one as it "
        "comes round.", P))

    s.append(Paragraph("7. Random pattern creator", H2))
    s.append(Paragraph(
        "The gold <b>RANDOM</b> key writes a new acid line into the selected pattern, and the LCD greets it with an "
        "acid smiley for a moment. UNDO brings back the previous one.", P))
    s.append(Paragraph(
        "<b>SHIFT + RANDOM</b> makes a small <b>variation</b> of the pattern instead of a new one: 2 to 4 steps "
        "change one thing each (a note moves within the scale, an octave flips, an accent or slide comes and goes, "
        "a step plays or rests), so the line stays recognisable. Press it again for more variation; UNDO steps "
        "back. Variations follow the same SCALE, ROOT and LOCK settings.", P))
    s.append(Paragraph("The controls next to it shape what it makes:", P))
    s.append(table([
        ["Control", "What it does"],
        ["SCALE / ROOT", "Notes stay in this key. <i>Acid</i> is a root-heavy minor-seventh set, the classic sound; "
                         "<i>Chromatic</i> uses every note."],
        ["DENSITY", "How many steps play rather than rest (downbeats play a little more often, so lines keep a pulse)."],
        ["ACCENT", "Chance of an accent on each step."],
        ["SLIDE", "Chance of a slide on each step."],
        ["OCTAVE", "How many steps get an octave UP or DOWN."],
        ["LOCK", "Click a LOCK key (its LED lights) to keep a lane as it is when re-rolling: for example lock NOTE and OCT and press RANDOM again to "
                 "re-roll only the rhythm, accents and slides."],
    ], [30 * mm, 145 * mm]))

    s.append(Paragraph("8. MIDI export", H2))
    s.append(Paragraph(
        "<b>MIDI EXPORT</b> (bottom right) saves a standard MIDI file (.mid). It exports the <b>selected pattern</b>, "
        "or the <b>whole chain</b> in order when CHAIN is on. A save dialog asks where to put it; the name is "
        "suggested for you (for example <i>TRoss303 I-1A.mid</i> or <i>TRoss303 chain.mid</i>). Drag the file into "
        "Live (or any DAW) to get a MIDI clip of the line. The key's LED lights and the LCD says MIDI SAVED when "
        "it's done.", P))
    s.append(Paragraph(
        "The file plays back the same through TRoss 303, a TB-03/TD-3 or any 303 that takes MIDI: accented steps "
        "have velocity 127 (others 90), slides are overlapping notes, ties are long notes. Each pattern's transpose "
        "is included, and the file carries Live's current tempo.", P))

    s.append(Paragraph("9. About, credits and license", H2))
    s.append(Paragraph(
        "Click the <b>TRoss 303</b> logo for the About window; click the Gumroad link (on the panel or in the About "
        "window) to open the store.", P))
    s.append(Paragraph(
        "TRoss 303 by Ticco Ross &mdash; ticcoross@gmail.com &mdash; https://ticcoross.gumroad.com/", P))
    s.append(Paragraph(
        "The sound engine is <b>Lemus ro-mono-303</b> (https://github.com/lemus-dev/lemus, version 0.1.3), used "
        "unchanged. Lemus and TRoss 303 are free software under the <b>GNU Affero General Public License v3</b>: "
        "the complete source code of this device is at https://github.com/Spunkmeyer77/TRoss303. TB-303 is a "
        "trademark of Roland Corporation; this product is not affiliated with or endorsed by Roland.", SMALL))

    s.append(Paragraph("10. Version history", H2))
    s.append(table([
        ["Version", "Changes"],
        ["1.2.1", "TRANSP is now the selected pattern's own transpose (-24 to +24): each pattern keeps its own "
                  "value and the readout follows the selected pattern. Double-click TRANSP to reset it. (The "
                  "single global transpose is gone; it wasn't saved per pattern.)"],
        ["1.2.0", "MIDI export of a pattern or the chain. SHIFT + RANDOM makes a variation. Per-pattern transpose "
                  "(drag on the LCD). Bigger LCD with larger text and NEXT pattern info. The smiley appears whenever "
                  "the LCD is touched. Red pattern LEDs follow the playing pattern (the next one blinks). The step "
                  "editor follows the sounding pattern while a chain plays. Every key shows when it's pressed."],
        ["1.1.1", "Fix: LOCK keys turned themselves on when the device loaded, so RANDOM changed nothing; SEQ "
                  "could start switched off."],
        ["1.1.0", "Silver Classic panel: chicken-head knobs, rubber keys, LEDs, green LCD with acid smiley, gold "
                  "keys. Pattern length set from the STEP LEDs."],
        ["1.0.1", "Fix: the panel drawing hid the controls."],
        ["1.0.0", "First release: Lemus TB-303 voice, 303 sequencer, 64-pattern memory, chain, random creator."],
    ], [22 * mm, 153 * mm]))

    doc = SimpleDocTemplate(str(out), pagesize=A4, leftMargin=15 * mm, rightMargin=15 * mm,
                            topMargin=22 * mm, bottomMargin=16 * mm,
                            title="TRoss 303 User Manual", author="Ticco Ross")
    doc.build(s, onFirstPage=header_footer, onLaterPages=header_footer)


if __name__ == "__main__":
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "docs" / "TRoss303_Manual.pdf"
    build(out)
    print("wrote", out)
