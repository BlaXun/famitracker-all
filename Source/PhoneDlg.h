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

// Edit > Record from your phone: a small window with a QR code (drawn with
// Api/QrCode.h) of the phone page's address on this computer's home network
// (Api/PhoneTakes.h), the address as text, and the ideas that arrived. When
// FamiTracker doesn't listen to other computers yet, it offers to (the
// setting of File > Connect Claude Code). It stays open beside the song
// (modeless), so the ideas go into the clips while it shows (PhoneLink.h).

class CMainFrame;

void ShowPhoneWindow(CMainFrame *pMainFrame);
