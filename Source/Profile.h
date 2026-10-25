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

// Profiles: how much of FamiTracker shows (View > Profile, View > Show
// everything). "VRC6 game music" is a simpler FamiTracker for writing music
// for a game, and each of its changes can be switched off on its own;
// "Everything" is FamiTracker as it always was. The songs don't change: the
// template only fills new songs, which stay ordinary modules. The choice is
// kept in the settings (CSettings::Profile); CMainFrame::ApplyProfile shows it.

class CFamiTrackerDoc;
class CMenu;
class CWnd;

enum profile_t {
	PROFILE_UNSET = -1,		// not asked yet
	PROFILE_EVERYTHING = 0,
	PROFILE_GAME_MUSIC = 1
};

// What the profile in the settings asks for
bool ProfileSongTemplate();		// new songs from the game music template
bool ProfileBPM();				// BPM in place of speed and tempo
bool ProfileFewerMenus();		// rarely used menu items hidden

// The game music template on a new, empty song: the VRC6 chip, 120 BPM and
// instruments to start with, the song left unmodified. Views aren't told:
// OnInitialUpdate follows a new document.
void ApplySongTemplate(CFamiTrackerDoc *pDoc);
int TemplateBPM();

// BPM in place of speed and tempo: a song's BPM with RowsPerBeat rows to a
// beat (the first row highlight), and the speed and tempo for a BPM
double SongBPM(int Speed, int Tempo, int FrameRate, int RowsPerBeat);
void SpeedTempoForBPM(int Bpm, int RowsPerBeat, int FrameRate, int CurrentSpeed, int SplitPoint, int &Speed, int &Tempo);
// The BPM box shows BPM_LEAST to BPM_MOST. A song at speed 1 (a sound effect,
// ambience) has no beat to count there, and its BPM read 900: the box shows
// the frames a row lasts ("1 frame") under the label Speed, and 1 typed, a
// BPM over BPM_MOST or a step past it set speed 1 at the default tempo, a row
// a frame (FrameTempo); ftapi::LayoutWantsFrames in Api/Music.h decides
const int BPM_LEAST = 20, BPM_MOST = 400;
bool BPMShowsFrames(int Speed);
CString BPMFramesText(int Speed, int Tempo, int FrameRate);
bool BPMWantsFrames(int Bpm);
int FrameTempo(int FrameRate);

// Takes the rarely used items out of a copy of the main menu, and the
// separators they leave doubled
void RemoveAdvancedMenuItems(CMenu *pMenu);

// View > Profile; true if anything changed
bool ShowProfileDialog(CWnd *pParent);

// The first time FamiTracker starts with this: asks whether to use the game
// music profile. True if the answer was yes.
bool AskForProfile(CWnd *pParent);
