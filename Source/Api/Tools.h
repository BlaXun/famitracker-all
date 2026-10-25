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

// The tools Claude Code calls over MCP, in musical terms: reading the song,
// writing parts, instruments from presets, keys and moods, checks for what
// it can't hear, and playback for the user. Changes arrive as clips unless
// the user allows direct changes, which are undoable one by one.

#include <string>
#include <vector>
#include "Json.h"
#include "Keys.h"

namespace ftapi {

class SongHost;
struct SoundEntry;

// The selected track's key and the chords its parts play, for FamiTracker's
// piano roll (ToolSet::Harmony)
struct HeardChord {
	int Row, Rows;					// song rows: a bar, or half of one
	std::string Name;				// "Am", "G7", "C/E", spelled as the key spells them
	std::string Degree;				// in the key: "vi", "V7", "bVII"
	std::vector<int> PitchClasses;	// its notes, the root's first (0 = C)
	bool InKey;						// all of them the key's
	HeardChord() : Row(0), Rows(0), InKey(true) {}
};

struct TrackHarmony {
	bool HasKey;		// a key is chosen for the track, or its notes fit one
	bool Chosen;		// the key is the one chosen for the track (Keys.h)
	KeyChoice Choice;	// the key: the chosen one, else the one the notes fit
	bool HasFound;		// the notes fit a key...
	Key Found;			// ...this one, as the tools find it, whether or not one is chosen
	std::vector<HeardChord> Chords;	// the chords the parts play, as get_song hears them, in the key
	TrackHarmony() : HasKey(false), Chosen(false), HasFound(false) {}
};

struct ToolInfo {
	std::string Name;
	std::string Title;
	std::string Description;
	Json InputSchema;
	bool ReadOnly;
	bool Destructive;
	bool Idempotent;
};

// A recipe the client offers as a command (an MCP prompt): a task in steps
struct PromptArgument {
	std::string Name;
	std::string Description;
	bool Required;
};

struct PromptInfo {
	std::string Name;
	std::string Title;
	std::string Description;
	std::vector<PromptArgument> Arguments;
};

struct ToolResult {
	std::string Text;
	std::string Image;		// a PNG to go with the text (look_at_song), or empty
	bool IsError;
	ToolResult() : IsError(false) {}
};

// Who calls: a client on another computer can't open the files the tools
// write on this one, so export_for_game gives it links to fetch them
struct Caller {
	std::string FilesUrl;	// "http://192.168.1.10:46600/files/" as that client reached FamiTracker; empty for this computer
	bool Core;				// asked through the small set (/mcp/core), whose replies name only its tools
	Caller() : Core(false) {}
};

class ToolSet {
public:
	explicit ToolSet(SongHost &Host);

	const std::vector<ToolInfo> &List() const { return m_Tools; }
	bool Has(const std::string &Name) const;
	ToolResult Call(const std::string &Name, const Json &Arguments, const Caller &Who = Caller());

	// A file sent from another computer (send_file, PUT /inbox) into the Inbox folder next to FamiTracker.exe:
	// the reply that names its path for the tools that read it, or false with the reason (a name that isn't
	// a plain file name of a kind they read, too big)
	bool ReceiveFile(const std::string &Name, const std::string &Bytes, std::string &Reply);
	// A file export_for_game saved next to the song, for a client on another computer (GET /files): false when
	// the name isn't one of those files' (Error says so) or there's none (Error empty)
	bool ReadExport(const std::string &Name, std::string &Bytes, std::string &Error);
	// For FamiTracker's piano roll: the selected track's key (chosen, or found from the notes as the tools find it)
	// and the chords its parts play, as get_song hears them; false with the reason when there's no song
	bool Harmony(TrackHarmony &Out, std::string &Error);
	// For FamiTracker's sound browser (Instrument > Sound browser): every sound the song can get, as find_sound
	// lists them (SoundIndex.h), Cursor the channel the user is on (-1 for none); false with the reason. Without
	// Library, the ready-made sounds and the song's own only, quickly (the main window's Listen)
	bool Sounds(int Cursor, std::vector<SoundEntry> &Out, std::string &Error, bool Library = true);
	// One of them made into an instrument, as add_instrument or reference_sound makes it (a song's own is
	// itself): its index, or -1 with the reason
	int MakeSound(const SoundEntry &Sound, std::string &Error);

	// The small set of tools for local models (/mcp/core): the composing essentials
	static const std::vector<std::string> &CoreTools();
	static bool IsCoreTool(const std::string &Name);

	// For the client: how to use the tools, and how to write music that doesn't sound plain
	static std::string Instructions();
	// The same for the small set, short enough for a local model, with the craft and the rules that decide how
	// it sounds
	static std::string CoreInstructions();
	// Recipes: compose a track, give it depth, offer versions, study one
	static const std::vector<PromptInfo> &Prompts();
	// A prompt's message with its arguments filled in; false with the reason
	// when there's no such prompt or a required argument is missing
	static bool PromptText(const std::string &Name, const Json &Arguments, std::string &Text, std::string &Error);

private:
	SongHost &m_Host;
	std::vector<ToolInfo> m_Tools;
};

}	// namespace ftapi
