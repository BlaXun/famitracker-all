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

#include "WhatNext.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace ftapi {

// A dry run: reading goes to the song, changing is counted and kept back

bool DryRunHost::GetSong(SongInfo &Info, std::string &Error)
{
	if (!m_Host.GetSong(Info, Error))
		return false;
	Info.DirectChanges = true;	// so a tool writes (here: counts) rather than making a clip
	return true;
}

void DryRunHost::ReadCells(int Channel, int First, int Rows, std::vector<Cell> &Out)
{
	m_Host.ReadCells(Channel, First, Rows, Out);
	if (!Overlay)
		return;
	// The cells as the writes kept back would leave them, as FamiTracker writes a part (CInsertRecordingAction): an exact
	// part's cells as they are, another's notes, volumes and effects with the song's own effects (speed, tempo, jumps)
	// kept where they were. Writes that move frames aren't followed.
	if (Out.size() < static_cast<size_t>(std::max(0, Rows)))
		Out.resize(static_cast<size_t>(std::max(0, Rows)));
	for (size_t w = 0; w < Writes.size(); ++w) {
		const SongWrite &Write = Writes[w].first;
		if (Write.MoveBy != 0)
			continue;
		for (size_t p = 0; p < Writes[w].second.size(); ++p) {
			const ChannelCells &Part = Writes[w].second[p];
			if (Part.Channel != Channel)
				continue;
			for (size_t r = 0; r < Part.Cells.size(); ++r) {
				const int Row = Write.First + static_cast<int>(r);
				if (Row < First || Row >= First + Rows)
					continue;
				Cell &Old = Out[static_cast<size_t>(Row - First)];
				const Cell &New = Part.Cells[r];
				if (Part.Exact) {
					Old = New;
					continue;
				}
				Cell Kept = New;
				for (int k = 0; k < EFFECT_COLUMNS; ++k)
					if (IsSongEffect(Old.Effect[k])) {
						int Column = Kept.Effect[k] == EFF_NONE ? k : -1;
						for (int j = 0; j < EFFECT_COLUMNS && Column < 0; ++j)
							if (Kept.Effect[j] == EFF_NONE)
								Column = j;
						if (Column >= 0) {
							Kept.Effect[Column] = Old.Effect[k];
							Kept.Param[Column] = Old.Param[k];
						}
					}
				Old = Kept;
			}
		}
	}
}

bool DryRunHost::ReadClip(int Index, std::vector<std::vector<Cell> > &Columns)
{
	return m_Host.ReadClip(Index, Columns);
}

bool DryRunHost::CanChange(std::string &)
{
	return true;
}

bool DryRunHost::WriteSong(const std::vector<ChannelCells> &Parts, const SongWrite &Write, std::string &)
{
	++Changes;
	Writes.push_back(std::make_pair(Write, Parts));
	return true;
}

bool DryRunHost::SetSections(const SectionList &, std::string &)
{
	++Changes;
	++Others;
	return true;
}

bool DryRunHost::SetKey(bool, const KeyChoice &, std::string &)
{
	++Changes;
	++Others;
	return true;
}

bool DryRunHost::SetMixer(const MixerSettings &, std::string &)
{
	++Changes;
	++Others;
	return true;
}

bool DryRunHost::AddClip(std::string &, const std::vector<int> &, const std::vector<std::vector<Cell> > &, std::string &)
{
	++Changes;
	++Others;
	return true;
}

namespace {

const char *const DRY_RUN = "Not in a dry run.";

}	// namespace

int DryRunHost::AddInstrument(const Preset &, const std::string &, bool, std::string &Error)
{
	Error = DRY_RUN;
	return -1;
}

int DryRunHost::AddN163Instrument(const WaveSound &, int, std::string &Error)
{
	Error = DRY_RUN;
	return -1;
}

int DryRunHost::AddFdsInstrument(const FdsSound &, std::string &Error)
{
	Error = DRY_RUN;
	return -1;
}

int DryRunHost::AddVrc7Instrument(int, const std::string &, std::string &Error)
{
	Error = DRY_RUN;
	return -1;
}

int DryRunHost::AddVrc7CustomInstrument(const std::vector<int> &, const std::string &, std::string &Error)
{
	Error = DRY_RUN;
	return -1;
}

int DryRunHost::AddDpcmKit(const DpcmKit &, std::string &Error)
{
	Error = DRY_RUN;
	return -1;
}

int DryRunHost::RefreshDpcmKit(const DpcmKit &, std::string &)
{
	return 0;	// nothing brought up to date
}

void DryRunHost::CountInstrumentUses(std::vector<int> &InTracks, std::vector<int> &InClips)
{
	m_Host.CountInstrumentUses(InTracks, InClips);
}

bool DryRunHost::RemoveInstruments(const std::vector<int> &, std::string &Error)
{
	Error = DRY_RUN;
	return false;
}

int DryRunHost::ExpansionNotes(const std::vector<std::string> &Chips, int Channels, std::vector<int> &Tracks)
{
	return m_Host.ExpansionNotes(Chips, Channels, Tracks);
}

bool DryRunHost::SetExpansion(const std::vector<std::string> &, int, int &Moved, int &Lost, std::string &Error)
{
	Moved = Lost = 0;
	Error = DRY_RUN;
	return false;
}

bool DryRunHost::SetSpeedTempo(int, int, std::string &)
{
	++Changes;
	++Others;
	return true;
}

bool DryRunHost::SetMeter(int, int, int, std::string &)
{
	++Changes;
	++Others;
	return true;
}

bool DryRunHost::SetNames(const SongNames &, std::string &)
{
	++Changes;
	++Others;
	return true;
}

bool DryRunHost::SelectTrack(int, std::string &Error)
{
	Error = DRY_RUN;
	return false;
}

int DryRunHost::AddTrack(const std::string &, const std::vector<std::vector<Cell> > &, int, int, std::string &Error)
{
	Error = DRY_RUN;
	return -1;
}

bool DryRunHost::RenderTrack(int Track, double Seconds, const RenderOptions &Options, std::vector<float> &Left,
	std::vector<float> &Right, int &SampleRate, std::vector<std::pair<int, long long> > &Marks, std::string &Error)
{
	return m_Host.RenderTrack(Track, Seconds, Options, Left, Right, SampleRate, Marks, Error);
}

std::unique_ptr<RenderSet> DryRunHost::RenderParts(int Track, double Seconds, const std::vector<RenderOptions> &Renders,
	std::string &Error)
{
	return m_Host.RenderParts(Track, Seconds, Renders, Error);
}

bool DryRunHost::SaveFile(const std::string &FileName, const std::string &, std::string &Path, std::string &)
{
	++Changes;
	++Others;
	Path = FileName;
	return true;
}

bool DryRunHost::ReadFile(const std::string &FileName, std::string &Bytes, std::string &Error)
{
	return m_Host.ReadFile(FileName, Bytes, Error);
}

std::string DryRunHost::LocalPath(const std::string &Path)
{
	return m_Host.LocalPath(Path);
}

bool DryRunHost::SaveInbox(const std::string &, const std::string &, std::string &, std::string &Error)
{
	Error = DRY_RUN;
	return false;
}

bool DryRunHost::ReadExport(const std::string &Name, std::string &Bytes, std::string &Error)
{
	return m_Host.ReadExport(Name, Bytes, Error);
}

bool DryRunHost::ListLibrary(std::vector<std::string> &Names, std::string &Error)
{
	return m_Host.ListLibrary(Names, Error);
}

bool DryRunHost::ReadLibrary(const std::string &Name, std::string &Bytes, std::string &Error)
{
	return m_Host.ReadLibrary(Name, Bytes, Error);
}

bool DryRunHost::SaveLibrary(const std::string &, const std::string &, std::string &, std::string &Error)
{
	Error = DRY_RUN;
	return false;
}

bool DryRunHost::DeleteLibrary(const std::string &, std::string &Error)
{
	Error = DRY_RUN;
	return false;
}

bool DryRunHost::LatestRecording(std::string &Path, std::string &Error)
{
	return m_Host.LatestRecording(Path, Error);
}

bool DryRunHost::OpenText(const std::string &, std::string &Error)
{
	Error = DRY_RUN;
	return false;
}

bool DryRunHost::Play(const PlayRequest &, std::string &Error)
{
	Error = DRY_RUN;
	return false;
}

bool DryRunHost::Undo(std::string &Message)
{
	Message = DRY_RUN;
	return false;
}

bool DryRunHost::Save(const std::string &, std::string &Message)
{
	Message = DRY_RUN;
	return false;
}

// The kinds of song, as the recipes (get_recipe, compose_track) and the style guide's topics have them: their tempos,
// the lead's sound, how the chords, the bass and the drums play, the mix and the export's loudness
namespace {

const NextKind KINDS[] = {
	{ NEXT_GENERAL, "general", 0.0, 0.0, 150.0, "Melody lead", "lead", "arpeggio", "arpeggio", "arpeggio", "basic", "soft", "any",
		-20.0, { "C major", "G major", "D major" } },
	{ NEXT_CALM, "calm", 60.0, 90.0, 75.0, "Calm lead", "breath", "lush", "roots", "half", "", "calm", "calm", -22.0,
		{ "F major", "D major", "G major" } },
	{ NEXT_LIVELY, "lively", 105.0, 165.0, 128.6, "Stage lead", "stepped lead", "broken", "arpeggio", "arpeggio", "lively", "lively",
		"lively", -20.0, { "G major", "D major", "A major" } },
	{ NEXT_ACTION, "action", 135.0, 195.0, 150.0, "Action lead", "bloom lead", "stabs", "octaves", "octaves", "driving", "soft", "any",
		-19.0, { "A minor", "D minor", "E minor" } },
};

}	// namespace

const NextKind &NextKindInfo(int Kind)
{
	for (size_t i = 0; i < sizeof(KINDS) / sizeof(KINDS[0]); ++i)
		if (KINDS[i].Kind == Kind)
			return KINDS[i];
	return KINDS[0];
}

bool ParseNextKind(const std::string &Text, int &Kind)
{
	std::string Word;
	for (size_t i = 0; i < Text.size(); ++i)
		if (std::isalpha(static_cast<unsigned char>(Text[i])))
			Word += static_cast<char>(std::tolower(static_cast<unsigned char>(Text[i])));
	if (Word == "calm" || Word == "ambient" || Word == "night" || Word == "peaceful")
		Kind = NEXT_CALM;
	else if (Word == "lively" || Word == "stage")
		Kind = NEXT_LIVELY;
	else if (Word == "action" || Word == "battle" || Word == "boss" || Word == "fight")
		Kind = NEXT_ACTION;
	else
		return false;
	return true;
}

// Eight bars each, ending open (on V, or IV for a calm song), so a repeat, a break or the loop leads back in
std::string NextProgression(int Kind, bool Minor, bool B)
{
	if (Kind == NEXT_CALM) {
		if (Minor)
			return B ? "| VI | III | iv | i | VI | III | iv | VII |" : "| i | VI | iv | i | VI | iv | VII | i |";
		return B ? "| vi | IV | I | V | vi | IV | ii | IV |" : "| I | vi | IV | I | vi | IV | ii | IV |";
	}
	if (Kind == NEXT_ACTION) {
		if (Minor)
			return B ? "| iv | VI | VII | i | iv | VI | VII | V |" : "| i | VI | VII | i | i | VI | VII | V |";
		return B ? "| vi | IV | V | I | vi | IV | V | V |" : "| I | bVII | IV | I | I | bVII | IV | V |";
	}
	if (Minor)
		return B ? "| iv | VI | VII | III | iv | VI | VII | V |" : "| i | VI | III | VII | i | VI | iv | V |";
	return B ? "| IV | V | iii | vi | IV | V | ii | V |" : "| I | V | vi | IV | I | V | IV | V |";
}

// The example phrase

namespace {

// A note of a rhythm: its bar in a two-bar cell (0 or 1), its beat and how long it lasts
struct Hit {
	int Bar;
	double Beat, Beats;
};

// Two-bar cells in 4/4: an open one (it ends on a held note) and a closing one (a held note and a rest)
const Hit LIVELY_OPEN[] = { { 0, 1, 1 }, { 0, 2, 0.5 }, { 0, 2.5, 0.5 }, { 0, 3, 1.5 }, { 0, 4.5, 0.5 }, { 1, 1, 1.5 }, { 1, 2.5, 0.5 },
	{ 1, 3, 2 } };
const Hit LIVELY_CLOSE[] = { { 0, 1, 0.5 }, { 0, 1.5, 0.5 }, { 0, 2, 1 }, { 0, 3, 1 }, { 0, 4, 1 }, { 1, 1, 3 } };
const Hit SYNCOPATED_OPEN[] = { { 0, 1, 1.5 }, { 0, 2.5, 1 }, { 0, 3.5, 0.5 }, { 0, 4, 1 }, { 1, 1, 0.5 }, { 1, 1.5, 0.5 }, { 1, 2, 2 } };
const Hit SYNCOPATED_CLOSE[] = { { 0, 1, 1 }, { 0, 2, 1 }, { 0, 3, 0.5 }, { 0, 3.5, 0.5 }, { 0, 4, 1 }, { 1, 1, 2.5 } };
const Hit CALM_OPEN[] = { { 0, 1, 2 }, { 0, 3, 1 }, { 0, 4, 1 }, { 1, 1, 3 } };
const Hit CALM_CLOSE[] = { { 0, 1, 1 }, { 0, 2, 1 }, { 0, 3, 2 }, { 1, 1, 2 } };
const Hit CALM_OPEN_2[] = { { 0, 1, 1 }, { 0, 2, 2 }, { 0, 4, 1 }, { 1, 1, 2 }, { 1, 3, 1 } };
const Hit CALM_CLOSE_2[] = { { 0, 2, 1 }, { 0, 3, 1 }, { 0, 4, 1 }, { 1, 1, 3 } };

int Mod12(int Value)
{
	return ((Value % 12) + 12) % 12;
}

bool Has(const std::vector<int> &Classes, int PitchClass)
{
	return std::find(Classes.begin(), Classes.end(), Mod12(PitchClass)) != Classes.end();
}

}	// namespace

std::vector<TuneNote> ExampleTune(const Key &K, int FirstBar, int Bars, int BeatsPerBar, int RowsPerBeat,
	const std::vector<std::vector<int> > &Chord, const std::vector<std::vector<int> > &Sounding, bool Calm, int Variant)
{
	Bars = Bars >= 4 ? 4 : 2;
	BeatsPerBar = std::max(1, BeatsPerBar);
	RowsPerBeat = std::max(1, RowsPerBeat);
	// The rhythm: an open cell and a closing one for four bars, the closing one alone for two
	std::vector<Hit> Rhythm;
	auto AddCell = [&](const Hit *pHits, size_t Count, int Offset) {
		for (size_t i = 0; i < Count; ++i) {
			Hit H = pHits[i];
			H.Bar += Offset;
			Rhythm.push_back(H);
		}
	};
	if (BeatsPerBar == 4) {
		// Variant's bit 0 picks the open cell's rhythm, bit 1 the closing one's
		const bool OpenTwo = (Variant & 1) != 0, CloseTwo = (Variant & 2) != 0;
		const Hit *pOpen = Calm ? (OpenTwo ? CALM_OPEN_2 : CALM_OPEN) : (OpenTwo ? SYNCOPATED_OPEN : LIVELY_OPEN);
		const Hit *pClose = Calm ? (CloseTwo ? CALM_CLOSE_2 : CALM_CLOSE) : (CloseTwo ? SYNCOPATED_CLOSE : LIVELY_CLOSE);
		const size_t OpenCount = Calm ? (OpenTwo ? 5 : 4) : (OpenTwo ? 7 : 8);
		const size_t CloseCount = Calm ? 4 : 6;
		if (Bars == 4)
			AddCell(pOpen, OpenCount, 0);
		AddCell(pClose, CloseCount, Bars == 4 ? 2 : 0);
	}
	else {
		// Other meters: a note a beat (two a beat when fast and lively), the cell's last bar one held note and a rest
		for (int Cell = 0; Cell < Bars / 2; ++Cell) {
			for (int b = 1; b <= BeatsPerBar; ++b) {
				const Hit H = { 2 * Cell, static_cast<double>(b), 1.0 };
				Rhythm.push_back(H);
			}
			const Hit Held = { 2 * Cell + 1, 1.0, static_cast<double>(std::max(1, BeatsPerBar - 1)) };
			Rhythm.push_back(Held);
		}
	}

	// The pitches: the tonic in octave 3 as home (as a pulse plays it); the phrase rises from its third to a high point
	// past the middle and comes home, its open cell ending on the fifth
	const int Home = 36 + Mod12(K.Tonic), Low = Home - 5, High = Home + 12;
	const int Third = Home + ((K.Mode == MODE_MINOR || K.Mode == 1 || K.Mode == 2 || K.Mode == 6 || K.Mode == 7) ? 3 : 4);
	const int Rows = Bars * BeatsPerBar * RowsPerBeat;
	auto RowOf = [&](const Hit &H) {
		return static_cast<int>(std::floor(((H.Bar * BeatsPerBar) + (H.Beat - 1.0)) * RowsPerBeat + 0.5));
	};
	auto Length = [&](const Hit &H) { return std::max(1, static_cast<int>(std::floor(H.Beats * RowsPerBeat + 0.5))); };
	auto Rubs = [&](int Pitch, int From, int To) {
		for (int r = std::max(0, From); r < To && r < static_cast<int>(Sounding.size()); ++r)
			for (size_t i = 0; i < Sounding[static_cast<size_t>(r)].size(); ++i) {
				const int Apart = Mod12(Pitch - Sounding[static_cast<size_t>(r)][i]);
				if (Apart == 1 || Apart == 11)
					return true;
			}
		return false;
	};
	std::vector<TuneNote> Out;
	int Previous = Third;
	const size_t Count = Rhythm.size();
	for (size_t i = 0; i < Count; ++i) {
		const Hit &H = Rhythm[i];
		const int From = RowOf(H), To = std::min(Rows, From + Length(H));
		// Where the line heads: up from the third to a high point (the sixth above home) three fifths in, then home; the
		// open cell rests on the fifth, and the last note on home or the third
		const double Along = Count > 1 ? static_cast<double>(i) / static_cast<double>(Count - 1) : 1.0;
		double Target = Along < 0.6 ? Third + (Home + 9 - Third) * (Along / 0.6) : Home + 9 - 9.0 * (Along - 0.6) / 0.4;
		const bool OpenEnd = Bars == 4 && i + 1 < Count && Rhythm[i + 1].Bar >= 2 && H.Bar < 2;
		if (OpenEnd)
			Target = Home + 7;
		if (i + 1 == Count)
			Target = (Variant & 4) != 0 ? Third : Home;	// bit 2: it ends on the third
		const bool Strong = H.Beats >= 1.0 || std::fabs(H.Beat - 1.0) < 1e-9 || (BeatsPerBar == 4 && std::fabs(H.Beat - 3.0) < 1e-9);
		const std::vector<int> &Under = From < static_cast<int>(Chord.size()) ? Chord[static_cast<size_t>(From)] : std::vector<int>();
		int Best = -1;
		double BestCost = 0.0;
		for (int Pass = 0; Pass < 4 && Best < 0; ++Pass) {
			// Relaxed pass by pass: the chord's tones on strong notes, then any of the key's, then rubs allowed
			for (int p = Low; p <= High; ++p) {
				if (!InKey(p, K))
					continue;
				if ((Pass == 0 || Pass == 2) && Strong && !Under.empty() && !Has(Under, p))
					continue;
				if (Pass < 2 && Rubs(p, From, To))
					continue;
				// Near where the line heads; a step from the note before between strong notes, a step or a third onto
				// one; seldom the same note again; moving the way the line heads
				const int Move = p - Previous, Size = std::abs(Move);
				double Cost = 0.5 * std::fabs(p - Target);
				if (i > 0) {
					if (Strong)
						Cost += Size == 0 ? 2.0 : Size <= 2 ? 0.0 : Size <= 4 ? 0.5 : Size <= 7 ? 1.5 : 4.0;
					else
						Cost += Size == 0 ? 3.0 : Size <= 2 ? 0.0 : Size <= 4 ? 1.5 : 4.0;
					if (Size > 0 && (Target > Previous + 0.5) != (Move > 0) && std::fabs(Target - Previous) > 0.5)
						Cost += 0.75;
				}
				if (Best < 0 || Cost < BestCost - 1e-9) {
					Best = p;
					BestCost = Cost;
				}
			}
		}
		if (Best < 0)
			Best = Home;
		TuneNote N;
		N.Bar = FirstBar + H.Bar;
		N.Beat = H.Beat;
		N.Pitch = Best;
		N.Beats = H.Beats;
		Out.push_back(N);
		Previous = Best;
	}
	return Out;
}

}	// namespace ftapi
