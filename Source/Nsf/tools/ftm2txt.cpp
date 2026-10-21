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

// ftm2txt: a FamiTracker module read as the study tools read it
// (FtmFile.h) and written as FamiTracker's text (Module.h's WriteText), to
// compare with what FamiTracker's File > Export text makes of it.
//
//   ftm2txt song.ftm [song.txt]
//
// The text goes to the file, or else to the standard output; the file's
// version, the settings a Module doesn't hold and what couldn't be read go
// to the standard error.

#include "../FtmFile.h"
#include "../Module.h"

#include <cstdio>
#include <string>

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::printf("ftm2txt: a FamiTracker module as FamiTracker's text\n\n"
			"  ftm2txt song.ftm [song.txt]\n\n"
			"Without a text file the text goes to the standard output.\n");
		return 1;
	}
	nsf::Module M;
	nsf::FtmInfo Info;
	std::string Error;
	if (!nsf::ReadFtmFile(argv[1], M, Info, Error)) {
		std::fprintf(stderr, "ftm2txt: %s\n", Error.c_str());
		return 2;
	}
	std::fprintf(stderr, "ftm2txt: version %X.%02X, highlights %d and %d, split point %d, vibrato %d, engine speed %d\n", Info.Version >> 8,
		Info.Version & 0xFF, Info.FirstHighlight, Info.SecondHighlight, Info.SplitPoint, Info.VibratoStyle, Info.EngineSpeed);
	for (size_t i = 0; i < Info.Problems.size(); ++i)
		std::fprintf(stderr, "ftm2txt: %s\n", Info.Problems[i].c_str());

	const std::string Text = nsf::WriteText(M);
	FILE *f = stdout;
	if (argc > 2) {
#ifdef _MSC_VER
		if (fopen_s(&f, argv[2], "wb") != 0)
			f = nullptr;
#else
		f = std::fopen(argv[2], "wb");
#endif
		if (!f) {
			std::fprintf(stderr, "ftm2txt: can't write %s\n", argv[2]);
			return 3;
		}
	}
	std::fwrite(Text.data(), 1, Text.size(), f);
	if (f != stdout)
		std::fclose(f);
	return 0;
}
