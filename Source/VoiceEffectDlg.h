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

// Edit > Sound effect from your voice: say "pew", "boing" or "kssh" into the
// microphone, or open a WAV file, and it becomes a sound effect for a game.
// The dialog records the take, which stops by itself once the sound is over
// (transcribe::VoiceTake), keeps it in Documents\FamiTracker recordings, says
// what it heard and plays it as the NES would (transcribe::RenderEffect).
// Add to the song writes it as a track of its own with Claude Code's
// make_sound_effect tool, so the dialog and Claude make the same sound from a
// take.

// What the dialog needs from the tools' side of FamiTracker (McpConnection.cpp)
class CVoiceEffectMaker
{
public:
	virtual ~CVoiceEffectMaker() {}

	// A name for a sound effect that no track or instrument has: Base, or
	// Base with a number ("SFX Voice 2")
	virtual CString FreeSoundName(const CString &Base) = 0;
	// The sound effect from the WAV file at Path as a new track, which
	// becomes the selected one. Name: the name to give it, or empty for the
	// tool's choice; the name it got comes back in it. Octaves: how far to
	// move its pitch, unless FitOctave lets the tool choose. Its number from
	// 0, or -1 with the reason in Error.
	virtual int MakeSoundEffect(const CString &Path, CString &Name, bool FitOctave, int Octaves, bool Noise, CString &Error) = 0;
	// Plays the selected track from its start
	virtual void PlaySelectedTrack() = 0;
};

void ShowVoiceEffectDialog(CVoiceEffectMaker *pMaker, CWnd *pParent);
