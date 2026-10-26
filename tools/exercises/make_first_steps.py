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

# python3 tools/exercises/make_first_steps.py res/examples/first-song.ftm res/exercises: writes Help > Exercises' First steps starter songs (2A03, one frame of 1-4 bars,
# C major chosen) with the first example song's instruments (00 Lead, 01 Pluck, 02 Bass, ...), through the exercises'
# make_starters.py (beside it): parts are steps of an eighth (two rows), a pitch starts a note, - holds it, . rests (a release).
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_starters as ms

C_MAJOR = (0, 0)
EMPTY = '. . . . . . . .'


def song(outdir, template, name, title, bars, parts, about):
    comment = 'Help > Exercises, First steps: %s.\n%s' % (title, about)
    ms.write(outdir, name, ms.module(template, title, title, 16 * bars, parts, C_MAJOR, comment))


def main():
    template, outdir = sys.argv[1], sys.argv[2]
    lead = lambda steps, bars, vol=12: ms.part(steps, ms.LEAD, vol, bars=bars)

    # Your first note, High and low, A steady beat, A rhythm to clap: one empty bar
    song(outdir, template, 'first-note', 'Your first note', 1, {}, 'Click a note into the piano roll and hear it.')
    song(outdir, template, 'high-low', 'High and low', 1, {}, 'A low note, then a high one.')
    song(outdir, template, 'steady-beat', 'A steady beat', 1, {}, 'Four notes, one on each beat line.')
    song(outdir, template, 'rhythm', 'A rhythm to clap', 1, {}, 'One note, long and short.')
    # Long and short: two notes of two beats, neither long nor short yet
    song(outdir, template, 'long-short', 'Long and short', 2, {
        0: lead('C4 - - - . . . . | G4 - - - . . . .', 2),
    }, 'Make one note long and another short.')
    # Loud and soft: four notes on one pitch, all as loud as they go
    song(outdir, template, 'loud-soft', 'Loud and soft', 1, {
        0: lead('G4 - G4 - G4 - G4 -', 1, 15),
    }, 'Make one note quieter than the others.')
    # Up the stairs: the first note, C, where the stairs start
    song(outdir, template, 'stairs', 'Up the stairs', 3, {
        0: lead('C4 - . . . . . . | ' + EMPTY + ' | ' + EMPTY, 3),
    }, 'Five notes up the lighter lanes, then back down.')
    # Copy and repeat: a one-bar rhythm, then three empty bars
    song(outdir, template, 'copy-repeat', 'Copy and repeat', 4, {
        0: lead('C4 - - - E4 - G4 - | ' + EMPTY + ' | ' + EMPTY + ' | ' + EMPTY, 4),
    }, 'Copy bar 1 and paste it into bars 2, 3 and 4.')
    # A different sound and Two parts at once: a little tune on Pulse 1 (the triangle empty)
    tune = 'C4 - E4 - G4 - E4 -'
    song(outdir, template, 'new-sound', 'A different sound', 1, {0: lead(tune, 1)},
         'The same notes with another sound from the Sound browser.')
    # (Two parts' tune lower, E-3 to C-4, so it and the bass under it fit in a short piano roll together)
    song(outdir, template, 'two-parts', 'Two parts at once', 1, {0: lead('E3 - G3 - C4 - G3 -', 1)},
         'A low note on the triangle under the tune.')
    # Your first tiny tune: two empty bars
    song(outdir, template, 'tiny-tune', 'Your first tiny tune', 2, {}, 'Four notes up, four down, ending where it started.')


if __name__ == '__main__':
    main()
