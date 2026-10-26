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

# ftm.py: reads and writes FamiTracker 0.4.6 modules (2A03 only) as FamiTracker writes them
# (FamiTrackerDoc.cpp's WriteBlock_*), for make_starters.py and make_first_steps.py.
import struct

SEQ_NAMES = ['volume', 'arpeggio', 'pitch', 'hipitch', 'duty']


def i32(v):
    return struct.pack('<i', v)


class Reader:
    def __init__(self, data):
        self.d, self.p = data, 0

    def int(self):
        v = struct.unpack_from('<i', self.d, self.p)[0]
        self.p += 4
        return v

    def char(self):
        v = self.d[self.p]
        self.p += 1
        return v

    def schar(self):
        v = struct.unpack_from('<b', self.d, self.p)[0]
        self.p += 1
        return v

    def bytes(self, n):
        v = self.d[self.p:self.p + n]
        self.p += n
        return v

    def cstr(self):
        e = self.d.index(b'\0', self.p)
        v = self.d[self.p:e]
        self.p = e + 1
        return v


def read_blocks(path):
    data = open(path, 'rb').read()
    assert data[:18] == b'FamiTracker Module'
    version = struct.unpack_from('<I', data, 18)[0]
    pos, blocks = 22, []
    while data[pos:pos + 3] != b'END':
        ident = data[pos:pos + 16].split(b'\0')[0].decode('latin-1')
        ver, size = struct.unpack_from('<ii', data, pos + 16)
        blocks.append((ident, ver, data[pos + 24:pos + 24 + size]))
        pos += 24 + size
    assert data[pos:] == b'END'
    return version, blocks


def write_blocks(version, blocks):
    out = bytearray(b'FamiTracker Module' + struct.pack('<I', version))
    for ident, ver, body in blocks:
        out += ident.encode('latin-1').ljust(16, b'\0') + struct.pack('<ii', ver, len(body)) + bytes(body)
    out += b'END'
    return bytes(out)


class Module:
    """One 2A03 module: params, info, tracks (one used here), instruments, sequences, comments, sections"""

    def __init__(self):
        self.params = dict(chip=0, channels=5, machine=0, engine=0, vibrato=1, hl1=4, hl2=16, split=32)
        self.title, self.artist, self.copyright = b'', b'', b''
        self.track_names = [b'New song']
        self.columns = [[1] * 5]          # per track, per channel: effect columns shown (1-4)
        self.frames = []                  # per track: dict(speed, tempo, length, order=[[pattern per channel] per frame])
        self.patterns = {}                # (track, channel, pattern) -> {row: (note, octave, inst, vol, [(eff, param)] * columns)}
        self.instruments = {}             # index -> dict(type=1, seqs=[(enable, index)] * 5, dpcm=bytes(288), name=bytes)
        self.sequences = {}               # (index, type) -> dict(items=[...], loop=-1, release=-1, setting=0)
        self.dpcm = b'\x00'
        self.comment = None               # (show, bytes)
        self.sections = None              # raw body of BF SECTIONS, or [(track, [(first, last, name)])]
        self.extra = []                   # other blocks, kept as they are

    # Reading ---------------------------------------------------------------------------------------------------

    @staticmethod
    def read(path):
        m = Module()
        version, blocks = read_blocks(path)
        assert version == 0x0440
        for ident, ver, body in blocks:
            r = Reader(body)
            if ident == 'PARAMS':
                assert ver == 6
                p = m.params
                p['chip'] = r.char()
                p['channels'], p['machine'], p['engine'], p['vibrato'], p['hl1'], p['hl2'] = [r.int() for _ in range(6)]
                assert p['chip'] == 0
                p['split'] = r.int()
            elif ident == 'INFO':
                m.title, m.artist, m.copyright = r.bytes(32), r.bytes(32), r.bytes(32)
            elif ident == 'HEADER':
                tracks = r.char() + 1
                m.track_names = [r.cstr() for _ in range(tracks)]
                m.columns = [[0] * m.params['channels'] for _ in range(tracks)]
                m.channel_types = []
                for c in range(m.params['channels']):
                    m.channel_types.append(r.char())
                    for t in range(tracks):
                        m.columns[t][c] = r.char() + 1
            elif ident == 'INSTRUMENTS':
                for _ in range(r.int()):
                    index, kind = r.int(), r.char()
                    assert kind == 1
                    n = r.int()
                    seqs = [(r.char(), r.char()) for _ in range(n)]
                    dpcm = r.bytes(8 * 12 * 3)
                    name = r.bytes(r.int())
                    m.instruments[index] = dict(type=kind, seqs=seqs, dpcm=dpcm, name=name)
            elif ident == 'SEQUENCES':
                keys = []
                for _ in range(r.int()):
                    index, kind = r.int(), r.int()
                    count = r.char()
                    loop = r.int()
                    items = [r.schar() for _ in range(count)]
                    m.sequences[(index, kind)] = dict(items=items, loop=loop)
                    keys.append((index, kind))
                for k in keys:
                    m.sequences[k]['release'] = r.int()
                    m.sequences[k]['setting'] = r.int()
            elif ident == 'FRAMES':
                m.frames = []
                for t in range(len(m.track_names)):
                    count, speed, tempo, length = r.int(), r.int(), r.int(), r.int()
                    order = [[r.char() for _ in range(m.params['channels'])] for _ in range(count)]
                    m.frames.append(dict(speed=speed, tempo=tempo, length=length, order=order))
            elif ident == 'PATTERNS':
                assert ver == 5
                while r.p < len(body):
                    t, c, p, items = r.int(), r.int(), r.int(), r.int()
                    rows = {}
                    for _ in range(items):
                        row = r.int()
                        note, octave, inst, vol = r.char(), r.char(), r.char(), r.char()
                        fx = [(r.char(), r.char()) for _ in range(m.columns[t][c])]
                        rows[row] = (note, octave, inst, vol, fx)
                    m.patterns[(t, c, p)] = rows
            elif ident == 'DPCM SAMPLES':
                m.dpcm = body
            elif ident == 'COMMENTS':
                m.comment = (r.int(), r.cstr())
            elif ident == 'BF SECTIONS':
                tracks = []
                for _ in range(r.int()):
                    t, n = r.int(), r.int()
                    tracks.append((t, [(r.int(), r.int(), r.cstr()) for _ in range(n)]))
                m.sections = tracks
            else:
                m.extra.append((ident, ver, body))
        return m

    # Writing ---------------------------------------------------------------------------------------------------

    def write(self):
        blocks = []
        p = self.params
        body = bytes([p['chip']]) + b''.join(i32(p[k]) for k in ('channels', 'machine', 'engine', 'vibrato', 'hl1', 'hl2', 'split'))
        blocks.append(('PARAMS', 6, body))
        blocks.append(('INFO', 1, self.title.ljust(32, b'\0')[:32] + self.artist.ljust(32, b'\0')[:32] +
                       self.copyright.ljust(32, b'\0')[:32]))
        tracks = len(self.track_names)
        body = bytes([tracks - 1]) + b''.join(n + b'\0' for n in self.track_names)
        types = getattr(self, 'channel_types', list(range(p['channels'])))
        for c in range(p['channels']):
            body += bytes([types[c]]) + bytes(self.columns[t][c] - 1 for t in range(tracks))
        blocks.append(('HEADER', 3, body))
        body = i32(len(self.instruments))
        for index in sorted(self.instruments):
            ins = self.instruments[index]
            body += i32(index) + bytes([ins['type']]) + i32(len(ins['seqs']))
            body += b''.join(bytes([e, s]) for e, s in ins['seqs'])
            body += ins['dpcm'] + i32(len(ins['name'])) + ins['name']
        blocks.append(('INSTRUMENTS', 6, body))
        keys = sorted(k for k in self.sequences if self.sequences[k]['items'])
        body = i32(len(keys))
        for k in keys:
            s = self.sequences[k]
            body += i32(k[0]) + i32(k[1]) + bytes([len(s['items'])]) + i32(s['loop'])
            body += b''.join(struct.pack('<b', v) for v in s['items'])
        for k in keys:
            s = self.sequences[k]
            body += i32(s['release']) + i32(s['setting'])
        blocks.append(('SEQUENCES', 6, body))
        body = b''
        for f in self.frames:
            body += i32(len(f['order'])) + i32(f['speed']) + i32(f['tempo']) + i32(f['length'])
            body += b''.join(bytes(row) for row in f['order'])
        blocks.append(('FRAMES', 3, body))
        body = b''
        for t in range(tracks):
            for c in range(p['channels']):
                for pat in range(128):
                    rows = self.patterns.get((t, c, pat))
                    if not rows:
                        continue
                    body += i32(t) + i32(c) + i32(pat) + i32(len(rows))
                    for row in sorted(rows):
                        note, octave, inst, vol, fx = rows[row]
                        assert len(fx) == self.columns[t][c], (t, c, pat, row)
                        body += i32(row) + bytes([note, octave, inst, vol]) + b''.join(bytes([e, v]) for e, v in fx)
        blocks.append(('PATTERNS', 5, body))
        blocks.append(('DPCM SAMPLES', 1, self.dpcm))
        if self.comment is not None:
            blocks.append(('COMMENTS', 1, i32(self.comment[0]) + self.comment[1] + b'\0'))
        if self.sections:
            body = i32(len(self.sections))
            for t, secs in self.sections:
                body += i32(t) + i32(len(secs)) + b''.join(i32(a) + i32(b) + n + b'\0' for a, b, n in secs)
            blocks.append(('BF SECTIONS', 1, body))
        blocks += self.extra
        return write_blocks(0x0440, blocks)


if __name__ == '__main__':
    import sys
    for path in sys.argv[1:]:
        m = Module.read(path)
        same = m.write() == open(path, 'rb').read()
        print(path, 'round trip', 'same' if same else 'DIFFERENT')
        print(' params', m.params, 'tracks', m.track_names, 'frames', [(len(f['order']), f['speed'], f['tempo'], f['length']) for f in m.frames])
        for i in sorted(m.instruments)[:3]:
            ins = m.instruments[i]
            print(' inst %02X %r seqs %s dpcm head %s' % (i, ins['name'], ins['seqs'], ins['dpcm'][:9].hex()))
        for k in sorted(m.sequences)[:4]:
            print(' seq', k, m.sequences[k])
        print(' sections', m.sections)
