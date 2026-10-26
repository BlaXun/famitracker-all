# FamiTracker - NES/Famicom sound tracker
# Copyright (C) 2026  The beginner-friendly FamiTracker contributors
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Library General Public License for more details.  To obtain a
# copy of the GNU Library General Public License, write to the Free
# Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

# python3 tools/exercises/first_steps_scripts.py res/exercises: writes Help > Exercises' First steps scripts (ASCII,
# CRLF), each beside its starter song (make_first_steps.py)
import sys
OUT = sys.argv[1]
S = {}
S['first-note'] = '''; Help > Exercises, First steps: your first note (Source/ExercisesDlg.cpp opens first-note.ftm with it)
title: Your first note
about: Click a note into the piano roll and hear it: up is higher, right is later.

step
show: piano-roll
text: This is the [[piano roll]], where you draw music. Each row across it is a [[lane]], with a piano key at its left end.

  Click once anywhere on the lanes. A [[note]] appears where you click, and you hear it.
wait: notes 1

step
show: piano-roll
play: bar 1
check: first-note
text: That's your first note! Press Play bar 1, here in this note, to hear it again.

  A note higher up sounds higher, and a note further right plays later. Click a few more if you like, then press Check.
hint: Click on the lanes themselves, not on the keys at the left or on the numbered strip at the top. A right-click on a note takes it away again.
answer: Pulse 1, bar 1, Lead: C4 - . . . . . .
'''
S['high-low'] = '''; Help > Exercises, First steps: high and low
title: High and low
about: A low note, then a high one: hear the difference.

step
show: piano-roll
text: How high or low a note sounds is its [[pitch]]. In the piano roll, a higher [[lane]] sounds higher.

  Click a note low down, at the start of the bar: near the left edge of the lanes.
wait: notes 1

step
show: piano-roll
play: bar 1
check: high-low
text: Now click a second note to the right of it, much higher up: five lanes or more above the first one.

  Press Play bar 1 to hear them, low and then high. Then press Check.
hint: Each lane is one step of pitch. The keys at the left are labelled C-3, C-4, C-5: try a note on the C-4 lane and one on the C-5 lane, twelve lanes higher. That's the same note, an octave up.
answer: Pulse 1, bar 1, Lead: C4 - - - C5 - - -
'''
S['long-short'] = '''; Help > Exercises, First steps: long and short
title: Long and short
about: Drag a note's end to make it long, and make another one short.

step
show: piano-roll
play: bars 1-2
text: A note's [[length]] is how long it lasts. These two notes are the same length: two [[beats|beat]] each, two steps of the grid. Press Play bars 1-2 to hear them.
wait: heard

step
show: piano-roll
play: bars 1-2
check: long-short
text: Make the first note long. Point at its right edge, hold the mouse button down and drag to the right, until the note reaches the next bar line: the strong line where bar 2 starts.

  Then make the second note short: drag its right edge to the left until it's as short as it goes. Press Play bars 1-2, then Check.
hint: Grab the note right at its end, its last few pixels; if the whole note moves instead, Ctrl+Z puts it back. As you drag, the end jumps from one beat line to the next.
answer: Pulse 1, bar 1: C4 - - - - - - - | G4 - . . . . . .
'''
S['steady-beat'] = '''; Help > Exercises, First steps: a steady beat
title: A steady beat
about: Four notes, one on each beat line: that's one bar.

step
show: piano-roll
text: Music moves to a steady [[beat]], the pulse you tap your foot to. In the piano roll, the thin lines going up and down are the beats.

  Click a note on the very first line, where the bar starts.
wait: notes 1

step
show: piano-roll
play: bar 1
check: steady-beat
text: Now click a note on each of the next three thin lines, at any height. Four beats like these make one [[bar]].

  Press Play bar 1 to hear your steady beat, then press Check.
hint: A click puts the note's start on the nearest line to its left, so click just to the right of each line. The notes can be on one lane or on several.
answer: Pulse 1, bar 1, Lead: C4 - C4 - C4 - C4 -
'''
S['loud-soft'] = '''; Help > Exercises, First steps: loud and soft
title: Loud and soft
about: Make one note quieter than the others, and hear it.

step
show: piano-roll
play: bar 1
text: Music isn't loud all the time: [[loud and soft|dynamics]] notes side by side give it life. These four notes are all as loud as they go. Press Play bar 1 to hear them.
wait: heard

step
show: piano-roll
play: bar 1
check: loud-soft
text: Each note has a [[volume]], how loud it plays, from 15 (the loudest) down to 1. Make the third note quieter: double-click it, point at Volume in the menu that opens, and click Soft.

  The note gets thinner. Press Play bar 1 to hear it step back, then press Check.
hint: Double-click right on the third note from the left. If no menu opens, click the note once, then use the Effects button at the top of the piano roll. Pointing at a note shows its volume in the line at the bottom.
answer: Pulse 1, bar 1: G4 - G4 - G4v6 - G4v15 -
'''
S['stairs'] = '''; Help > Exercises, First steps: up the stairs
title: Up the stairs
about: Five notes going up a step at a time on the lighter lanes, then back down.

step
show: piano-roll
play: bars 1-3
text: Some lanes are lighter than others. The lighter ones are the notes of the song's [[key]], C major: notes that sound good together. The C lanes are a little warmer: that's where the key feels at home.

  Your first note is there already, a C at the start. Click a note on the next lighter lane above it, on the next beat line.
wait: notes 1

step
show: piano-roll
play: bars 1-3
check: stairs
text: Go on up the stairs, one lighter lane higher on each beat line, until five notes go up: C, D, E, F and G. Then come back down the same way, F, E, D and C, to where you started.

  Going up and down the lighter lanes one at a time is a [[scale]]. Press Play bars 1-3, then Check.
hint: Between most lighter lanes there's a darker one to step over, but between E and F there's none: go to the next lighter lane each time. Snap to key, at the top of the piano roll, keeps every click on a lighter lane.
answer: Pulse 1, bar 1, Lead: C4 - D4 - E4 - F4 - | G4 - F4 - E4 - D4 - | C4 - - - - - - -
'''
S['rhythm'] = '''; Help > Exercises, First steps: a rhythm to clap
title: A rhythm to clap
about: One note in a pattern of long and short ones, like long, short-short.

step
show: piano-roll
text: A [[rhythm]] is a pattern of long and short notes: the part of music you could clap. Let's make one on a single lane: long, short-short.

  Click a note on the first beat line, and before you let go, drag to the right over the next beat line: a long note.
wait: notes 1

step
show: piano-roll
play: bar 1
check: rhythm
text: Now click two short notes on the same lane, one on the third beat line and one on the fourth.

  Press Play bar 1 and clap along: long, short-short. Then press Check. Hear it plays one too.
hint: A click makes a note one beat long, and dragging before you let go makes it longer. If your first note is short, drag its right edge to the right, over the next beat line.
answer: Pulse 1, bar 1, Lead: G4 - - - G4 - G4 -
'''
S['copy-repeat'] = '''; Help > Exercises, First steps: copy and repeat
title: Copy and repeat
about: Copy a one-bar rhythm and paste it three times: four bars that repeat.

step
show: piano-roll
play: bars 1-4
text: Bar 1 has a little rhythm, and bars 2, 3 and 4 are empty. Music [[repeats|repetition-variation]] a lot, and a copy saves you drawing it again.

  First copy bar 1. Click Select at the top of the piano roll. Then drag along the ruler, the numbered strip above the lanes, across bar 1: its notes light up. Press Ctrl+C.

step
show: piano-roll
play: bars 1-4
check: copy-repeat
text: Now point the mouse at the start of bar 2, right on its bar line, and press Ctrl+V: the copy lands there. Do the same at bar 3 and at bar 4.

  Press Play bars 1-4 to hear four bars that repeat, then press Check.
hint: The copy starts on the beat line at the mouse, or the one to its left, so point just to the right of the bar line. A copy in the wrong place: Ctrl+Z, and paste it again.
answer: Pulse 1, bar 2: C4 - - - E4 - G4 - | C4 - - - E4 - G4 - | C4 - - - E4 - G4 -
'''
S['new-sound'] = '''; Help > Exercises, First steps: a different sound
title: A different sound
about: The same notes with another instrument from the Sound browser.

step
show: menu Instrument
play: bar 1
text: Every note plays with an [[instrument]], the [[sound]] it makes. These notes play one called Lead. Let's give them another.

  Open Instrument > Sound browser. Click sounds to hear them. When you like one, press Add to the song, then Close.
wait: command instrument-library

step
show: piano-roll
play: bar 1
check: new-sound
text: Now give your notes the new sound. Click Select at the top of the piano roll and drag a box around the four notes. Then click the Instrument button at the top, point at Give the selected notes, and click your new sound.

  Press Play bar 1 to hear the same notes with another sound, then press Check.
hint: The sound you added is the last one in the list. Any sound but Lead will do, even Pluck, which the song has already.
answer: Pulse 1, bar 1, Pluck: C4 - E4 - G4 - E4 -
'''
S['two-parts'] = '''; Help > Exercises, First steps: two parts at once
title: Two parts at once
about: A low note on the triangle under the tune: two parts playing together.

step
show: piano-roll
play: bar 1
text: Music is often several [[parts|part]] at once. Here's one part, a little tune on Pulse 1, one of the NES's voices. Press Play bar 1 to hear it.
wait: heard

step
show: piano-roll
play: bar 1
check: two-parts
instrument: 02
text: Now a second part under it, on the [[triangle]], the NES's deep voice for the [[bass|bass-line]]. The piano roll draws the triangle's notes now, and the tune's are faint behind them.

  Click at the start of the bar on the C-3 lane, well below the faint notes, and before you let go, drag to the right to the end of the bar. Press Play bar 1 to hear both parts, then Check.
hint: The keys at the left are labelled C-3 and C-4. A click on a faint note adds a triangle note there too; only a double-click would switch back to the tune.
answer: Triangle, bar 1, Bass: C3 - - - - - - -
answer: Pulse 1, bar 1: E3 - G3 - C4 - G3 -
'''
S['tiny-tune'] = '''; Help > Exercises, First steps: your first tiny tune
title: Your first tiny tune
about: Four notes up and four down, ending on the note it started on.

step
show: piano-roll
text: Time for a [[tune|melody]] of your own: notes one after another that go somewhere and come back. Use the lighter lanes, the notes of the key.

  Start with a note on the first beat line, on a warmer lane: C, the key's [[home|home-note]].
wait: notes 1

step
show: piano-roll
play: bars 1-2
check: tiny-tune
text: That note is the first of four going up: click three more, each on a higher lane, one on each beat line. Then four coming down in bar 2, the last one on the lane you started on.

  Small steps or bigger ones, as you like. Press Play bars 1-2 to hear your tune, then Check.
hint: For example C, D, E and G going up in bar 1, then G, E, D and C coming down in bar 2. The keys at the left are labelled C-4 and C-5 to find your way.
answer: Pulse 1, bar 1, Lead: C4 - D4 - E4 - G4 - | G4 - E4 - D4 - C4 -
'''
for name, text in S.items():
    assert all(ord(c) < 127 for c in text), name
    open('%s/%s.txt' % (OUT, name), 'wb').write(text.replace('\n', '\r\n').encode('ascii'))
print(len(S), 'scripts')
