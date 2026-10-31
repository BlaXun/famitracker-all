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
// writing parts, instruments from presets, keys and moods, checks of the
// notes, listening to the rendered song, and playback for the user. Changes arrive as clips unless
// the user allows direct changes, which are undoable one by one.

#include <string>
#include <vector>
#include "Json.h"
#include "Keys.h"
#include "ChordRoles.h"

namespace ftapi {

class SongHost;
struct SoundEntry;
struct SongFile;

// The selected track's key and the chords its parts play, for FamiTracker's
// piano roll (ToolSet::Harmony)
struct HeardChord {
	int Row, Rows;					// song rows: a bar, or half of one
	std::string Name;				// "Am", "G7", "C/E", spelled as the key spells them
	std::string Degree;				// in the key: "vi", "V7", "bVII"
	std::vector<int> PitchClasses;	// its notes, the root's first (0 = C)
	int Bass;						// the bass's pitch class: the root, or another of its notes (C/E)
	bool InKey;						// all of them the key's
	chord_role_t Role;				// what it does in the key (ChordRoles.h): home, away, tension or color...
	std::string RoleWords;			// ...in plain words, after the chord right before it: "back home: the tension settles"
	HeardChord() : Row(0), Rows(0), Bass(0), InKey(true), Role(CHORD_ROLE_HOME) {}
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

// A file a reply points to or carries (export_for_game's OGG files), as MCP gives it: a link to the resource a
// client reads with resources/read, or the file itself when Bytes holds it (export_for_game's attach)
struct ToolFile {
	std::string Uri;		// "famitracker://files/Town.ogg" (ExportUri in Network.h)
	std::string Name;		// "Town.ogg"
	std::string MimeType;	// "audio/ogg"
	size_t Size;			// bytes
	std::string Bytes;		// the file itself when the reply carries it, else empty
	ToolFile() : Size(0) {}
};

struct ToolResult {
	std::string Text;
	std::string Image;		// a PNG to go with the text (look_at_song), or empty
	std::vector<ToolFile> Files;	// files to link as resources or carry (export_for_game), after the text
	bool IsError;
	ToolResult() : IsError(false) {}
};

// Who calls: a client on another computer can't open the files the tools
// write on this one, so export_for_game gives it links to fetch them
struct Caller {
	std::string FilesUrl;	// "http://192.168.1.10:46600/files/" as that client reached FamiTracker; empty for this computer
	// The code a link to those files carries in its address (?code=, FilesCode in Mcp.h), so a browser opens it without
	// the access code; it opens nothing else
	std::string FilesCode;
	// The same links over Tailscale ("http://100.101.102.103:46600/files/"), for a phone away from home, when this
	// computer has a Tailscale address and the client reached it another way; else empty
	std::string AwayUrl;
	// A program connected over MCP (Claude Code, Hermes...), not FamiTracker's own windows: it can't use File > Save As,
	// so save_song and export_for_game save a song without a file into FamiTracker's Songs folder
	bool Client;
	bool Core;				// asked through the small set (/mcp/core), whose replies name only its tools
	// Asked from another computer (HandleMcp: a peer that isn't this one), whose paths are never taken (open_song's path)
	bool Remote;
	Caller() : Client(false), Core(false), Remote(false) {}
};

class ToolSet {
public:
	explicit ToolSet(SongHost &Host);

	const std::vector<ToolInfo> &List() const { return m_Tools; }
	bool Has(const std::string &Name) const;
	ToolResult Call(const std::string &Name, const Json &Arguments, const Caller &Who = Caller());
	// What a call with arguments the tool doesn't take is told (a local model's guess, like find_sound's "words" for its
	// query): each one named, with the argument it likely meant, and the tool's own arguments; empty when the tool takes
	// every one (or there's no such tool). Call refuses such a call with it, doing nothing
	std::string UnknownArguments(const std::string &Name, const Json &Arguments) const;

	// A file sent from another computer (send_file, PUT /inbox) into the Inbox folder next to FamiTracker.exe:
	// the reply that names its path for the tools that read it, or false with the reason (a name that isn't
	// a plain file name of a kind they read, too big)
	bool ReceiveFile(const std::string &Name, const std::string &Bytes, std::string &Reply);
	// A file export_for_game saved next to the song, for a client on another computer (GET /files): false when
	// the name isn't one of those files' (Error says so) or there's none (Error empty)
	bool ReadExport(const std::string &Name, std::string &Bytes, std::string &Error);
	// The files next to the song a client may fetch (MCP's resources/list): the OGG files there, the newest
	// first, at most MAX_LISTED_EXPORTS (Song.h); false with the reason
	bool ListExports(std::vector<SongFile> &Files, std::string &Error);
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
	// After a call, so undo says what it undoes and a call that failed leaves nothing of its own to undo: the change a
	// call made gets its label (SongHost::LabelUndo), what a call that failed had changed is taken back
	void AfterCall(const std::string &Name, const Json &Args, long long UndoBefore, ToolResult &Result, const Caller &Who);
	// Then the agent log (AgentLog.h): the change lines and errors AfterCall added are written next to the song, and a
	// client hears of notes from the user it hasn't read
	void AfterLog(const std::string &Name, const Json &Args, ToolResult &Result, const Caller &Who);

	SongHost &m_Host;
	std::vector<ToolInfo> m_Tools;
	std::string m_FailedCall;	// the call that changes the song and failed last, while no change came after it ("" for none)
};

}	// namespace ftapi
