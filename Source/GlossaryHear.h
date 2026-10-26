/*
** FamiTracker - NES/Famicom sound tracker
** Copyright (C) 2005-2014  Jonathan Liss
** Copyright (C) 2026  The beginner-friendly FamiTracker contributors
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** Library General Public License for more details.  To obtain a
** copy of the GNU Library General Public License, write to the Free
** Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** Any permitted reproduction of these routines, in whole or in part,
** must bear this legend.
*/

#pragma once

// Hear it: the glossary's examples (Api/Glossary.h) played on the song's
// channels, a note at a time as the note keys play them (CSoundGen::QueueNote),
// timed by a message-only window as CSoundPreview times a sound. Each channel
// plays a plain instrument of its chip: the song's own one without sequences,
// or one made for the example and taken out again when it ends, the song's
// "changed" mark kept as it was across both. Starting the song or another
// example ends it, and the channels are reset after it (CSoundGen::SilentAll)
// unless the song plays, so its effects (a vibrato, a pan) don't stay on them.

#include <string>

namespace ftapi { struct ChannelInfo; }
class CMainFrame;
class CFamiTrackerDoc;

// Plays a glossary example by its id: false with why it can't (a chip the song doesn't have says how to hear it)
bool PlayGlossaryExample(CMainFrame *pMainFrame, const std::string &Id, CString &Why);
// Help > Hear the effect at the cursor: the effect the glossary's tooltip shows in the pattern editor, else the one
// under its cursor, played plain and then with its own numbers on its channel, with the instrument and the note it
// plays there; the status bar says what plays, or why it can't
void HearEffectAtCursor(CMainFrame *pMainFrame);
void StopGlossaryExample();
bool IsGlossaryExamplePlaying();
// What plays (the example's own words), "" when nothing does
CString GlossaryExamplePlaying();
void CloseGlossaryPlayer();		// the main frame's OnDestroy

// A channel of the song (a column) as the tools describe it: its kind, chip and name, for the effects' words
ftapi::ChannelInfo GlossaryChannelInfo(CFamiTrackerDoc *pDoc, int Channel);

// Why an effect changes nothing where it's written: the instrument of the note it plays on sets the same thing itself
// every frame (Vxx under a duty sequence, the N163's wave sequence or the 5B's noise and mode sequence), as "its
// instrument, Bright lead, sets it with a duty sequence"; "" when the effect is heard
CString EffectOverriddenBy(CFamiTrackerDoc *pDoc, int Track, int Frame, int Row, int Channel, int Effect);
