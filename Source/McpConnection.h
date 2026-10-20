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

class CMainFrame;

// File > Connect Claude Code: FamiTracker as an MCP server, so Claude Code on
// the same computer can work on the open song in musical terms (the tools are
// in Api/Tools.cpp, the protocol in Api/Mcp.cpp; see MCP.md).
//
// Off until the user switches it on. It listens on 127.0.0.1 only, and every
// request needs the access token from the command the dialog shows. Requests
// arrive on the listener's thread (McpSocket.cpp) and are answered here on
// the UI thread, where every change goes through the undo system. By default
// Claude's ideas become clips; a setting lets it change the song directly.
class CMcpConnection
{
public:
	CMcpConnection();
	~CMcpConnection();

	void Start(CMainFrame *pMainFrame);	// when the main window is up: listens if switched on
	void Stop();						// before the main window goes
	void ShowDialog();					// File > Connect Claude Code

private:
	struct Impl;
	Impl *m_pImpl;

	CMcpConnection(const CMcpConnection &);
	CMcpConnection &operator=(const CMcpConnection &);
};
