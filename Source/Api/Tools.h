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

namespace ftapi {

class SongHost;

struct ToolInfo {
	std::string Name;
	std::string Title;
	std::string Description;
	Json InputSchema;
	bool ReadOnly;
	bool Destructive;
	bool Idempotent;
};

struct ToolResult {
	std::string Text;
	bool IsError;
	ToolResult() : IsError(false) {}
};

class ToolSet {
public:
	explicit ToolSet(SongHost &Host);

	const std::vector<ToolInfo> &List() const { return m_Tools; }
	bool Has(const std::string &Name) const;
	ToolResult Call(const std::string &Name, const Json &Arguments);

	// For the client: how to use the tools
	static std::string Instructions();

private:
	SongHost &m_Host;
	std::vector<ToolInfo> m_Tools;
};

}	// namespace ftapi
