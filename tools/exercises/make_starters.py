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

# python3 tools/exercises/make_starters.py res/examples/first-song.ftm res/exercises: writes Help > Exercises' Next steps
# starter songs (2A03, one frame) with the example song's
# instruments (00 Lead, 01 Pluck, 02 Bass, 03 Kick, 04 Snare, 05 Tom, 06 Hi-hat, 07 Open hi-hat). Parts are steps of an
# eighth (two rows): a pitch (E3, Eb3, F#3) starts a note, - holds it, . rests (a release where a note sounded), | is a
# bar line; on the noise channel kick, snare, hihat and open name the drums.
import struct
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ftm

NOTES = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
LEAD, PLUCK, BASS, KICK, SNARE, TOM, HAT, OPEN = range(8)
DRUMS = {'kick': (KICK, 4, 13), 'snare': (SNARE, 10, 12), 'hihat': (HAT, 13, 7), 'open': (OPEN, 13, 9)}


def pitch(token):
    name = token[0].upper()
    rest = token[1:]
    p = NOTES[name]
    if rest.startswith('#'):
        p += 1
        rest = rest[1:]
    elif rest.startswith('b'):
        p -= 1
        rest = rest[1:]
    return int(rest) * 12 + p


def cell(p, inst, vol):
    return ((p % 12) + 1, p // 12, inst, vol)


def part(steps, inst, vol, rows_per_step=2, bars=4, release=13, noise=False):
    """Cells {row: (note, octave, inst, vol)} from steps; checks each bar has 8 steps"""
    bar_texts = [b.split() for b in steps.split('|')]
    assert len(bar_texts) == bars, (steps, len(bar_texts))
    for b in bar_texts:
        assert len(b) == 8, (steps, b)
    tokens = [t for b in bar_texts for t in b]
    cells, sounding, first = {}, False, True
    for i, t in enumerate(tokens):
        row = i * rows_per_step
        if t == '-':
            continue
        if t == '.':
            if sounding:
                cells[row] = (release, 0, 64, 16)
            sounding = False
            continue
        if noise:
            d_inst, d_pitch, d_vol = DRUMS[t]
            cells[row] = cell(d_pitch, d_inst, d_vol)
        else:
            cells[row] = cell(pitch(t), inst, vol if first else 16)	# the volume where the part starts, as the examples write it
        sounding, first = True, False
    return cells


def module(template, title, track, rows, parts, key=None, comment=''):
    m = ftm.Module.read(template)
    m.title = title.encode('latin-1')
    m.artist = b'FamiTracker exercises'
    m.copyright = b'2026'
    m.track_names = [track.encode('latin-1')]
    m.columns = [[1] * 5]
    m.frames = [dict(speed=8, tempo=150, length=rows, order=[[0, 0, 0, 0, 0]])]
    m.patterns = {}
    for channel, cells in parts.items():
        if cells:
            m.patterns[(0, channel, 0)] = {r: c + ([(0, 0)],) for r, c in cells.items()}
    m.comment = (0, comment.replace('\n', '\r\n').encode('latin-1'))
    m.sections = None
    m.extra = []
    if key is not None:
        tonic, mode = key
        m.extra.append(('BF KEYS', 1, struct.pack('<iiiii', 1, 0, tonic, mode, 0)))
    return m.write()


# Broken chords on Pulse 2 (the pluck, soft): low, high, middle, high twice a bar (an Alberti bass). Each part has lanes of its own in
# the piano roll, where a click on another channel's note picks that channel: the tune from C4 up, these chords under it
# (B2 to A3), the triangle's bass from E2 to D3. In the bass line's song the chords sit higher (E3 to D4), clear of the
# bass the user writes.
VOICINGS = {'C': 'C3 G3 E3 G3', 'Am': 'C3 A3 E3 A3', 'F': 'C3 A3 F3 A3', 'G': 'B2 G3 D3 G3', 'Dm': 'D3 A3 F3 A3'}
HIGH_VOICINGS = {'C': 'E3 G3 C4 G3', 'Am': 'E3 A3 C4 A3', 'F': 'F3 A3 C4 A3', 'G': 'G3 B3 D4 B3', 'Dm': 'F3 A3 D4 A3'}
# The beat's song keeps its first chords (nothing is clicked in beside them)
BEAT_VOICINGS = {'C': 'G2 C3 E3 C3', 'G': 'G2 B2 D3 B2'}


def chords(bars, voicings=VOICINGS):
    """'C | Am | F G | C': each bar's chords, a half bar each when there are two"""
    out = []
    for bar in bars.split('|'):
        names = bar.split()
        if len(names) == 1:
            out.append(voicings[names[0]] + ' ' + voicings[names[0]])
        else:
            out.append(voicings[names[0]] + ' ' + voicings[names[1]])
    return ' | '.join(out)


def bass(roots):
    """'C3 A2 F2 G2' or 'F2/G2': a root on beats 1 and 3 of each bar (two roots: one each)"""
    out = []
    for bar in roots.split():
        names = bar.split('/')
        if len(names) == 1:
            out.append('%s - - - %s - - -' % (names[0], names[0]))
        else:
            out.append('%s - - - %s - - -' % (names[0], names[1]))
    return ' | '.join(out)


def write(outdir, name, data):
    path = os.path.join(outdir, name + '.ftm')
    open(path, 'wb').write(data)
    print(path, len(data), 'bytes')


def main():
    template, outdir = sys.argv[1], sys.argv[2]
    C_MAJOR = (0, 0)
    rest = '. . . . . . . .'

    # The home note: a tune that stops a note short, on D over G
    write(outdir, 'home-note', module(template, 'The home note', 'The home note', 64, {
        0: part('E4 - G4 - C5 - G4 - | A4 - G4 F4 A4 - - - | G4 - F4 E4 D4 - - - | ' + rest, LEAD, 12),
        1: part(chords('C | F | G | C'), PLUCK, 6),
        2: part(bass('C3 F2 G2 C3'), BASS, 15, release=14),
    }, C_MAJOR, 'Help > Exercises: The home note.\nThe tune on Pulse 1 stops a note short: give it a last note that\n'
       'sounds like the end. Open it from Help > Exercises for the task,\na hint and Check.'))

    # Four bars over chords: the chords and the bass, no tune yet
    write(outdir, 'four-bars', module(template, 'Four bars over chords', 'Four bars over chords', 64, {
        1: part(chords('C | Am | F G | C'), PLUCK, 6),
        2: part(bass('C3 A2 F2/G2 C3'), BASS, 15, release=14),
    }, C_MAJOR, 'Help > Exercises: Four bars over chords.\nWrite a tune on Pulse 1 over the chords, mostly by step, ending at\nhome.'))

    # Strong notes: a tune whose notes on beats 1 and 3 often miss the chord
    write(outdir, 'strong-notes', module(template, 'Strong notes', 'Strong notes', 64, {
        0: part('E4 - D4 E4 D4 - C4 - | B4 - C5 B4 A4 - - - | A4 - G4 A4 C5 - B4 - | D5 - - - C5 - - -', LEAD, 12),
        1: part(chords('C | Am | F G | C'), PLUCK, 6),
        2: part(bass('C3 A2 F2/G2 C3'), BASS, 15, release=14),
    }, C_MAJOR, 'Help > Exercises: Strong notes.\nMake the notes on beats 1 and 3 notes of the chord there.'))

    # A phrase that breathes: a tune of eighths that never stops
    write(outdir, 'phrases', module(template, 'A phrase that breathes', 'A phrase that breathes', 64, {
        0: part('E4 F4 G4 E4 C4 D4 E4 G4 | D4 E4 F4 D4 B3 D4 G4 F4 | E4 F4 G4 E4 A4 G4 E4 C4 | F4 G4 A4 F4 C5 A4 G4 F4', LEAD, 12),
        1: part(chords('C | G | Am | F'), PLUCK, 6),
        2: part(bass('C3 G2 A2 F2'), BASS, 15, release=14),
    }, C_MAJOR, 'Help > Exercises: A phrase that breathes.\nSplit the tune into two phrases of two bars, each ending with a rest\n'
       'or a held note.'))

    # A bass line: a tune and chords, the triangle empty
    write(outdir, 'bass', module(template, 'A bass line', 'A bass line', 64, {
        0: part('E4 - G4 - C5 - - - | C5 - A4 - E4 - - - | F4 - A4 - C5 - - - | B4 - G4 - D4 - - -', LEAD, 12),
        1: part(chords('C | Am | F | G', HIGH_VOICINGS), PLUCK, 6),
    }, C_MAJOR, 'Help > Exercises: A bass line.\nPut the chords\' roots on the triangle, one on each bar\'s first beat,\n'
       'then two notes a bar.'))

    # A beat: two bars of chords and a bass, the noise channel empty
    write(outdir, 'beat', module(template, 'A beat', 'A beat', 32, {
        1: part(chords('C | G', BEAT_VOICINGS), PLUCK, 6, bars=2),
        2: part('C3 - - C3 - - C3 - | G2 - - G2 - - G2 -', BASS, 15, bars=2, release=14),
    }, C_MAJOR, 'Help > Exercises: A beat.\nA kick on beat 1, a snare on beats 2 and 4, and hi-hats in between,\n'
       'with the drum grid (View > Drum grid).'))

    # Major and minor: a happy tune over the roots alone (no key chosen: the notes say it)
    write(outdir, 'minor', module(template, 'Major and minor', 'Major and minor', 64, {
        0: part('C4 - E4 - G4 - E4 - | F4 - E4 D4 C4 - - - | D4 - E4 F4 G4 - F4 E4 | E4 D4 C4 - - - - -', LEAD, 12),
        2: part(bass('C3 F2 G2 C3'), BASS, 15, release=14),
    }, None, 'Help > Exercises: Major and minor.\nMake the happy tune sad by lowering its third: every E to Eb.'))

    # A hook that comes back: a two-bar hook, then the chords alone
    write(outdir, 'hook', module(template, 'A hook that comes back', 'A hook that comes back', 64, {
        0: part('E4 - G4 E4 C4 - D4 E4 | D4 - B3 - D4 - - - | ' + rest + ' | ' + rest, LEAD, 12),
        1: part(chords('C | G | C | G C'), PLUCK, 6),
        2: part(bass('C3 G2 C3 G2/C3'), BASS, 15, release=14),
    }, C_MAJOR, 'Help > Exercises: A hook that comes back.\nPlay the two-bar hook again in bars 3-4, with a different ending.'))


if __name__ == '__main__':
    main()
