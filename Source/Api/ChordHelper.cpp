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

#include "ChordHelper.h"
#include "FdsSounds.h"
#include "Presets.h"
#include "Wavetables.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace ftapi {

namespace {

int Mod12(int Value)
{
	return ((Value % 12) + 12) % 12;
}

std::string Format(const char *pFormat, int a, int b = 0)
{
	char Text[128];
	std::snprintf(Text, sizeof(Text), pFormat, a, b);
	return Text;
}

// "A, B and C"
std::string List(const std::vector<std::string> &Items)
{
	std::string Out;
	for (size_t i = 0; i < Items.size(); ++i) {
		if (i > 0)
			Out += i + 1 == Items.size() ? " and " : ", ";
		Out += Items[i];
	}
	return Out;
}

// Semitones a channel's notes are written over how they sound, as the tools count them (Written in Tools.cpp): the
// triangle, the VRC7 and the 5B sound an octave under what's written, the FDS two in FamiTracker 0.4.6
int Lift(const HelperChannel &Channel)
{
	if (Channel.Chip == "FDS")
		return 24;
	return Channel.Kind == KIND_TRIANGLE || Channel.Kind == KIND_FM || Channel.Kind == KIND_SQUARE ? 12 : 0;
}

// The FDS's pitch register tops out at G#7
int HighestWritten(const HelperChannel &Channel)
{
	return Channel.Chip == "FDS" ? 92 : PITCHES - 1;
}

bool IsBassKind(const HelperChannel &Channel)
{
	return Channel.Kind == KIND_TRIANGLE || Channel.Kind == KIND_SAWTOOTH;
}

bool IsMelodic(const HelperChannel &Channel)
{
	return Channel.Kind != KIND_NOISE && Channel.Kind != KIND_DPCM;
}

// The order the kinds of channel take voices in: the VRC7's FM, the N163's waves, the 5B's squares, the pulses, the FDS,
// the sawtooth (a bass, usually); -1 for a channel that takes none
const int VOICE_GROUPS = 6;

int VoiceGroup(const HelperChannel &Channel)
{
	if (!TakesVoice(Channel))
		return -1;
	if (Channel.Kind == KIND_FM)
		return 0;
	if (Channel.Chip == "N163")
		return 1;
	if (Channel.Kind == KIND_SQUARE)
		return 2;
	if (Channel.Kind == KIND_PULSE)
		return 3;
	if (Channel.Chip == "FDS")
		return 4;
	return 5;
}

// The pitch a note lands on: a slide into it (Qxy, Rxy on its first row) counted
int Landed(const NoteInfo &N)
{
	int Pitch = N.Pitch;
	for (size_t e = 0; e < N.Effects.size(); ++e) {
		if (N.Effects[e].first != 0)
			continue;
		if (N.Effects[e].second.first == EFF_SLIDE_UP)
			Pitch += N.Effects[e].second.second & 0x0F;
		else if (N.Effects[e].second.first == EFF_SLIDE_DOWN)
			Pitch -= N.Effects[e].second.second & 0x0F;
	}
	return Pitch;
}

bool Sounds(const NoteInfo &N, int First, int End)
{
	return N.Row < End && N.Row + std::max(1, N.Rows) > First;
}

// A channel's notes sounding in [First, End), at the pitches they sound at (as on a pulse)
std::vector<NoteInfo> SoundingIn(const HelperChannel &Channel, int First, int End)
{
	std::vector<NoteInfo> Out;
	if (!IsMelodic(Channel))
		return Out;
	const std::vector<NoteInfo> Notes = ReadNotes(Channel.Cells, Channel.EffectColumns);
	for (size_t i = 0; i < Notes.size(); ++i) {
		if (!Sounds(Notes[i], First, End))
			continue;
		NoteInfo N = Notes[i];
		N.Pitch = Landed(N) - Lift(Channel);
		Out.push_back(N);
	}
	return Out;
}

// The pitch half of the notes' time is spent below, or Default
int MiddleOf(const std::vector<NoteInfo> &Notes, int Default)
{
	std::vector<std::pair<int, int> > Pitches;
	int Total = 0;
	for (size_t i = 0; i < Notes.size(); ++i) {
		Pitches.push_back(std::make_pair(Notes[i].Pitch, std::max(1, Notes[i].Rows)));
		Total += std::max(1, Notes[i].Rows);
	}
	if (Pitches.empty())
		return Default;
	std::sort(Pitches.begin(), Pitches.end());
	int Sum = 0;
	for (size_t i = 0; i < Pitches.size(); ++i) {
		Sum += Pitches[i].second;
		if (Sum * 2 >= Total)
			return Pitches[i].first;
	}
	return Pitches.back().first;
}

// A note that plays a chord: an arpeggio (0xy) on its first row
bool PlaysChord(const NoteInfo &N)
{
	for (size_t e = 0; e < N.Effects.size(); ++e)
		if (N.Effects[e].first == 0 && N.Effects[e].second.first == EFF_ARPEGGIO && N.Effects[e].second.second != 0)
			return true;
	return false;
}

const HelperInstrument *FindInstrument(const HelperSong &Song, int Index)
{
	for (size_t i = 0; i < Song.Instruments.size(); ++i)
		if (Song.Instruments[i].Index == Index)
			return &Song.Instruments[i];
	return 0;
}

// The channel playing the melody in [First, End): of the melodic channels with notes there that aren't bass channels or
// mostly 0xy chords (any of them when only those play), the one with the most notes, notes higher up and louder ones
// counting for more, as the tools guess it (GuessMelodyChannel in Tools.cpp); -1 when none plays
int GuessMelody(const HelperSong &Song, int First, int End)
{
	for (int Pass = 0; Pass < 2; ++Pass) {
		std::vector<int> Channels, Counts, Middles, Loudness;
		int Lowest = 1000;
		for (size_t c = 0; c < Song.Channels.size(); ++c) {
			const HelperChannel &Channel = Song.Channels[c];
			if (!IsMelodic(Channel) || (Pass == 0 && IsBassKind(Channel)))
				continue;
			const std::vector<NoteInfo> Notes = SoundingIn(Channel, First, End);
			if (Notes.empty())
				continue;
			int Chords = 0;
			for (size_t i = 0; i < Notes.size(); ++i)
				Chords += PlaysChord(Notes[i]) ? 1 : 0;
			if (Pass == 0 && Chords * 3 >= static_cast<int>(Notes.size()))
				continue;
			std::vector<int> Volumes;
			for (size_t i = 0; i < Notes.size(); ++i)
				Volumes.push_back(Channel.Kind == KIND_TRIANGLE ? 15 : Notes[i].Volume);
			std::sort(Volumes.begin(), Volumes.end());
			Channels.push_back(static_cast<int>(c));
			Counts.push_back(static_cast<int>(Notes.size()));
			Middles.push_back(MiddleOf(Notes, 48));
			Loudness.push_back(std::max(1, Volumes[Volumes.size() / 2]));
			Lowest = std::min(Lowest, Middles.back());
		}
		int Best = -1;
		double BestScore = 0.0;
		for (size_t i = 0; i < Channels.size(); ++i) {
			const double Score = Counts[i] * std::pow(2.0, (Middles[i] - Lowest) / 8.0) * std::pow(Loudness[i] / 15.0, 2.0);
			if (Score > BestScore) {
				Best = Channels[i];
				BestScore = Score;
			}
		}
		if (Best >= 0)
			return Best;
	}
	return -1;
}

// Does a channel play a bass there? The triangle and the sawtooth do, and so does a channel whose notes there mostly play
// an instrument named as a bass ("Bass", "Sub bass", "FM bass")
bool PlaysBass(const HelperSong &Song, const HelperChannel &Channel, const std::vector<NoteInfo> &Notes)
{
	if (IsBassKind(Channel))
		return true;
	int Named = 0;
	for (size_t i = 0; i < Notes.size(); ++i) {
		const HelperInstrument *pInstrument = FindInstrument(Song, Notes[i].Instrument);
		if (pInstrument != 0 && Lowercase(pInstrument->Name).find("bass") != std::string::npos)
			++Named;
	}
	return !Notes.empty() && Named * 2 > static_cast<int>(Notes.size());
}

// The parts the chords keep room for: the melody's notes over them and the bass's under them, as they sound
struct Room {
	int Melody, Bass;					// channels, -1 for none
	std::vector<NoteInfo> Above, Below;
	int UsualLow;						// the melody's low notes, the ones it often dips to (a fifth of its notes are lower); -1
	Room() : Melody(-1), Bass(-1), UsualLow(-1) {}
};

Room RoomIn(const HelperSong &Song, int First, int End)
{
	Room R;
	R.Melody = GuessMelody(Song, First, End);
	if (R.Melody >= 0) {
		R.Above = SoundingIn(Song.Channels[static_cast<size_t>(R.Melody)], First, End);
		std::vector<int> Pitches;
		for (size_t i = 0; i < R.Above.size(); ++i)
			Pitches.push_back(R.Above[i].Pitch);
		std::sort(Pitches.begin(), Pitches.end());
		if (!Pitches.empty())
			R.UsualLow = Pitches[Pitches.size() / 5];
	}
	size_t Most = 0;
	for (size_t c = 0; c < Song.Channels.size(); ++c) {
		if (static_cast<int>(c) == R.Melody)
			continue;
		const std::vector<NoteInfo> Notes = SoundingIn(Song.Channels[c], First, End);
		if (Notes.empty() || !PlaysBass(Song, Song.Channels[c], Notes))
			continue;
		R.Below.insert(R.Below.end(), Notes.begin(), Notes.end());
		if (Notes.size() > Most) {
			Most = Notes.size();
			R.Bass = static_cast<int>(c);
		}
	}
	return R;
}

// The bars on rows [First, End), each cut to the range
std::vector<BarSpan> BarsIn(const Timing &T, int First, int End)
{
	std::vector<BarSpan> Out;
	for (int Row = First; Row < End; ) {
		const int Bar = T.BarOf(Row);
		const int To = std::min(End, T.BarStart(Bar) + T.BarLength(Bar));
		if (To <= Row)
			break;
		Out.push_back(BarSpan(Row, To - Row));
		Row = To;
	}
	return Out;
}

// The volume on a channel as loud as Volume on a pulse: the VRC7's and the 5B's steps are 3 dB each
int VolumeOn(const HelperChannel &Channel, int Volume)
{
	if (Volume < 0)
		return Volume;
	if (Channel.Kind == KIND_FM || Channel.Kind == KIND_SQUARE) {
		const double Db = 20.0 * std::log10(std::max(1, std::min(15, Volume)) / 15.0);
		return std::max(1, std::min(15, 15 + static_cast<int>(std::floor(Db / 3.0 + 0.5))));
	}
	return Volume;
}

// A pitch as it sounds, as written on a channel (an octave further where that's outside the notes it plays)
int WrittenOn(const HelperChannel &Channel, int Sounding)
{
	int Pitch = Sounding + Lift(Channel);
	while (Pitch > HighestWritten(Channel))
		Pitch -= 12;
	while (Pitch < 0)
		Pitch += 12;
	return Pitch;
}

// String pads the helper looks for and makes at most: "String pad" to "String pad 8" (the N163's memory holds 8 sounds)
const int MAX_PADS = 8;

const char *const CHORD_SOUND = "soft chords";	// held chords on the pulses, the 5B, the VRC7 (its reed organ) and the FDS
const char *const N163_SOUND = "string pad";	// on the N163, as voiced chords on the soft layer default to

// The name the instrument library gives a sound on a channel's chip, as it makes it (InstrumentLibrary.h)
std::string MadeName(const HelperChannel &Channel, const std::string &Sound)
{
	if (Channel.Kind == KIND_FM) {
		const Vrc7Patch *pPatch = FindVrc7Patch(Sound);
		return pPatch != 0 ? pPatch->InstrumentName() : std::string();
	}
	if (Channel.Chip == "FDS") {
		const FdsSound *pSound = VoiceFdsSound(Sound);
		return pSound != 0 ? pSound->InstrumentName() : std::string();
	}
	if (Channel.Kind == KIND_SQUARE) {
		const Preset *pSound = FindSoundFor5B(Sound);
		return pSound != 0 ? pSound->InstrumentName(false) : std::string();
	}
	const Preset *pPreset = FindPreset(Sound);
	return pPreset != 0 ? pPreset->InstrumentName(Channel.Kind == KIND_SAWTOOTH) : std::string();
}

std::string InstrumentChip(const HelperChannel &Channel)
{
	return Channel.Chip == "MMC5" ? "2A03" : Channel.Chip;
}

// The song's instrument for the chip named so (ignoring case), -1 if none
int NamedInstrument(const HelperSong &Song, const std::string &Chip, const std::string &Name)
{
	const std::string Wanted = Lowercase(Name);
	for (size_t i = 0; i < Song.Instruments.size(); ++i)
		if (Song.Instruments[i].Chip == Chip && Lowercase(Song.Instruments[i].Name) == Wanted)
			return Song.Instruments[i].Index;
	return -1;
}

// The N163 instruments the N163 channels but Voices play in [First, End): a voice can't share one (two channels on an
// instrument share the place of its wave in the chip's memory, and its wave sequence moves both)
std::set<int> TakenN163(const HelperSong &Song, int First, int End, const std::vector<int> &Voices)
{
	std::set<int> Taken;
	for (size_t c = 0; c < Song.Channels.size(); ++c) {
		if (Song.Channels[c].Chip != "N163" || std::find(Voices.begin(), Voices.end(), static_cast<int>(c)) != Voices.end())
			continue;
		const std::vector<NoteInfo> Notes = ReadNotes(Song.Channels[c].Cells, Song.Channels[c].EffectColumns);
		for (size_t i = 0; i < Notes.size(); ++i)
			if (Sounds(Notes[i], First, End) && Notes[i].Instrument != NO_INSTRUMENT)
				Taken.insert(Notes[i].Instrument);
	}
	return Taken;
}

// The sounds of the voices on Channels: on the N163 an instrument of its own for each (the song's string pad, its copies
// "String pad 2"... at free places of the wave memory, or another N163 instrument nothing else plays there), elsewhere
// soft chords. False with the channel that found none in Missing.
bool ChooseSounds(const HelperSong &Song, int First, int End, const std::vector<int> &Channels, std::vector<VoiceSound> &Out,
	int &Missing)
{
	Out.clear();
	Missing = -1;
	std::set<int> Taken = TakenN163(Song, First, End, Channels);
	std::vector<std::pair<int, int> > Used;		// the N163's waves: place, size
	std::set<std::string> Names;				// the N163 instruments' names, and those of the copies to make
	for (size_t i = 0; i < Song.Instruments.size(); ++i) {
		const HelperInstrument &I = Song.Instruments[i];
		if (I.Chip != "N163")
			continue;
		Names.insert(Lowercase(I.Name));
		if (I.WavePos >= 0 && I.WaveSize > 0)
			Used.push_back(std::make_pair(I.WavePos, I.WaveSize));
	}
	const WaveSound *pPad = FindWaveSound(N163_SOUND);
	const int Memory = std::min(N163WaveMemory(Song.N163Channels), N163_LOADABLE);
	for (size_t v = 0; v < Channels.size(); ++v) {
		const HelperChannel &Channel = Song.Channels[static_cast<size_t>(Channels[v])];
		VoiceSound Sound;
		if (Channel.Chip != "N163") {
			Sound.Sound = CHORD_SOUND;
			Sound.Instrument = NamedInstrument(Song, InstrumentChip(Channel), MadeName(Channel, CHORD_SOUND));
			Out.push_back(Sound);
			continue;
		}
		Sound.Sound = N163_SOUND;
		const std::string Base = pPad != 0 ? pPad->InstrumentName() : std::string("String pad");
		// The song's string pads first, then a new one
		std::string Make;
		for (int k = 1; k <= MAX_PADS && Sound.Instrument < 0; ++k) {
			const std::string Name = k == 1 ? Base : Base + Format(" %d", k);
			const int Index = NamedInstrument(Song, "N163", Name);
			if (Index >= 0 && Taken.count(Index) == 0) {
				Sound.Instrument = Index;
				Taken.insert(Index);
			}
			else if (Index < 0 && Make.empty() && Names.count(Lowercase(Name)) == 0)
				Make = Name;
		}
		if (Sound.Instrument < 0 && pPad != 0 && !Make.empty()) {
			const int Place = FindWavePlace(N163_WAVE_SIZE, Memory, Used);
			if (Place >= 0) {
				Sound.Name = Make;
				Sound.WavePos = Place;
				Used.push_back(std::make_pair(Place, N163_WAVE_SIZE));
				Names.insert(Lowercase(Make));
				Out.push_back(Sound);
				continue;
			}
		}
		// The wave memory is full: another of the song's N163 instruments nothing plays there
		for (size_t i = 0; i < Song.Instruments.size() && Sound.Instrument < 0; ++i)
			if (Song.Instruments[i].Chip == "N163" && Taken.count(Song.Instruments[i].Index) == 0)
				Sound.Instrument = Song.Instruments[i].Index;
		if (Sound.Instrument < 0) {
			Missing = Channels[v];
			return false;
		}
		Taken.insert(Sound.Instrument);
		Out.push_back(Sound);
	}
	return true;
}

// The channels voices go on, Count of Candidates (the last chord's first, Preferred of them): those in their order when
// there are enough of them, else channels of one kind where enough of it is free, else the first ones; from the left
std::vector<int> PickChannels(const HelperSong &Song, const std::vector<int> &Candidates, size_t Preferred, size_t Count)
{
	std::vector<int> Out;
	if (Count == 0 || Candidates.size() < Count)
		return Out;
	if (Preferred >= Count)
		return std::vector<int>(Candidates.begin(), Candidates.begin() + static_cast<long>(Count));
	for (int Group = 0; Group < VOICE_GROUPS && Out.empty(); ++Group) {
		std::vector<int> Kind;
		for (size_t i = 0; i < Candidates.size(); ++i)
			if (VoiceGroup(Song.Channels[static_cast<size_t>(Candidates[i])]) == Group)
				Kind.push_back(Candidates[i]);
		if (Kind.size() >= Count)
			Out.assign(Kind.begin(), Kind.begin() + static_cast<long>(Count));
	}
	if (Out.empty())
		Out.assign(Candidates.begin(), Candidates.begin() + static_cast<long>(Count));
	std::sort(Out.begin(), Out.end());
	return Out;
}

std::string BarsText(const Timing &T, int First, int End)
{
	const int From = T.BarOf(First), To = T.BarOf(std::max(First, End - 1));
	return From == To ? Format("bar %d", From) : Format("bars %d-%d", From, To);
}

// The volume of voices on a pulse (each a little under a melody at 15), and of an arpeggio, which plays a chord alone
const int VOICE_VOLUME = 10;
const int ARPEGGIO_VOLUME = 12;

}	// namespace

bool TakesVoice(const HelperChannel &Channel)
{
	switch (Channel.Kind) {
	case KIND_PULSE:
	case KIND_SAWTOOTH:
	case KIND_FM:
	case KIND_SQUARE:
		return true;
	case KIND_OTHER:
		return Channel.Chip == "N163" || Channel.Chip == "FDS";
	}
	return false;
}

const FdsSound *VoiceFdsSound(const std::string &Sound)
{
	const FdsSound *pSound = FindFdsSound(Sound);
	const Preset *pLike = pSound == 0 ? FindPreset(Sound) : 0;
	if (pLike != 0 && !pLike->Drum)
		pSound = FindFdsSound(pLike->Name);	// a preset's word: the FDS sound most like that preset, as the tools pick it
	if (pSound == 0 && !FdsSounds().empty())
		pSound = &FdsSounds().front();
	return pSound;
}

bool PlaysIn(const HelperChannel &Channel, int First, int End)
{
	const std::vector<NoteInfo> Notes = ReadNotes(Channel.Cells, Channel.EffectColumns);
	for (size_t i = 0; i < Notes.size(); ++i)
		if (Sounds(Notes[i], First, End))
			return true;
	return false;
}

std::vector<int> FreeChannels(const HelperSong &Song, int First, int End)
{
	std::vector<int> Out;
	for (int Group = 0; Group < VOICE_GROUPS; ++Group)
		for (size_t c = 0; c < Song.Channels.size(); ++c)
			if (VoiceGroup(Song.Channels[c]) == Group && !PlaysIn(Song.Channels[c], First, End))
				Out.push_back(static_cast<int>(c));
	return Out;
}

void WholeBars(const HelperSong &Song, int From, int To, int &First, int &End)
{
	const Timing T = Song.Time();
	From = std::max(0, From);
	To = std::max(From + 1, To);
	First = T.BarStart(T.BarOf(From));
	const int Last = T.BarOf(To - 1);
	End = T.BarStart(Last) + T.BarLength(Last);
}

int VoicesWanted(const std::vector<ChordAt> &Chords)
{
	size_t Most = 2;
	for (size_t i = 0; i < Chords.size(); ++i)
		Most = std::max(Most, Chords[i].C.PitchClasses().size());
	return static_cast<int>(std::min<size_t>(4, Most));
}

bool PlanChords(const HelperSong &Song, const ChordRequest &Request, ChordPlan &Plan)
{
	Plan = ChordPlan();
	const Timing T = Song.Time();
	const int R = std::max(1, T.RowsPerBeat);
	const int Limit = Request.Limit > 0 ? Request.Limit : Song.Rows();

	// The chords as written, bar by bar
	const std::vector<std::string> Written = SplitProgression(Request.Chords);
	if (Written.empty()) {
		Plan.Error = "Type a chord, like Am or G7, or a chord a bar, like C G Am F.";
		return false;
	}
	if (!Song.HasKey && WrittenAsDegrees(Written)) {
		Plan.Error = "Chords written as degrees (I, vi, V7...) need a key: choose one with the Key button, or write their names.";
		return false;
	}

	// The bars: the span's whole bars, and with Grow as many more as there are bars of chords
	int First = 0, End = 0;
	WholeBars(Song, Request.First, Request.End, First, End);
	End = std::min(End, std::max(First + 1, Limit));
	std::vector<BarSpan> Bars = BarsIn(T, First, End);
	if (Bars.empty()) {
		Plan.Error = "There are no bars there.";
		return false;
	}
	if (Request.Grow) {
		while (Bars.size() < Written.size() && End < Limit) {
			const int Bar = T.BarOf(End);
			const int To = std::min(Limit, T.BarStart(Bar) + T.BarLength(Bar));
			if (To <= End)
				break;
			Bars.push_back(BarSpan(End, To - End));
			End = To;
		}
	}
	if (Written.size() > Bars.size())
		Plan.Remarks.push_back(Format("The chords are %d bars and there are %d, so the last ones were left out.",
			static_cast<int>(Written.size()), static_cast<int>(Bars.size())));
	else if (Written.size() > 1 && Written.size() < Bars.size())
		Plan.Remarks.push_back(Format("The %d bars of chords repeat over the %d bars.", static_cast<int>(Written.size()),
			static_cast<int>(Bars.size())));
	std::string Error;
	if (!LayOutProgression(Written, Bars, R, Song.K, Plan.Chords, Error)) {
		Plan.Error = Error;
		return false;
	}
	Plan.First = First;
	Plan.End = End;
	Plan.Bars = Bars;
	Plan.Wanted = Request.Voices > 0 ? std::max(2, std::min(4, Request.Voices)) : VoicesWanted(Plan.Chords);

	// The free channels, the last chord's first
	const std::vector<int> Free = FreeChannels(Song, First, End);
	for (size_t i = 0; i < Request.Prefer.size(); ++i)
		if (std::find(Free.begin(), Free.end(), Request.Prefer[i]) != Free.end() &&
			std::find(Plan.Free.begin(), Plan.Free.end(), Request.Prefer[i]) == Plan.Free.end())
			Plan.Free.push_back(Request.Prefer[i]);
	size_t Preferred = Plan.Free.size();
	for (size_t i = 0; i < Free.size(); ++i)
		if (std::find(Plan.Free.begin(), Plan.Free.end(), Free[i]) == Plan.Free.end())
			Plan.Free.push_back(Free[i]);

	// The room: under the melody, over the bass (and where the voices lead from, before)
	const bool Leads = !Request.LeadFrom.empty() && Request.LeadFrom.front().Row < First &&
		Request.LeadFrom.back().Row + Request.LeadFrom.back().Rows == First;
	const int From = Leads ? Request.LeadFrom.front().Row : First;
	const Room Space = RoomIn(Song, From, End);
	Plan.Melody = Space.Melody;
	Plan.Bass = Space.Bass;

	// The channels and their sounds: an N163 channel that finds no sound of its own gives way to the next free one
	const size_t Count = static_cast<size_t>(Request.Arpeggio ? 1 : Plan.Wanted);
	std::vector<int> Chosen;
	std::vector<VoiceSound> Sounds;
	for (;;) {
		Chosen = PickChannels(Song, Plan.Free, Preferred, std::min(Count, Plan.Free.size()));
		if (Chosen.empty() || (!Request.Arpeggio && Chosen.size() < 2))
			break;
		int Missing = -1;
		if (ChooseSounds(Song, First, End, Chosen, Sounds, Missing))
			break;
		const std::vector<int>::iterator It = std::find(Plan.Free.begin(), Plan.Free.end(), Missing);
		if (It == Plan.Free.end())
			break;
		if (static_cast<size_t>(It - Plan.Free.begin()) < Preferred)
			--Preferred;
		Plan.Free.erase(It);
		Plan.Remarks.push_back(Song.Channels[static_cast<size_t>(Missing)].Name + " was left out: the N163's wave memory has no room "
			"for another sound, and each of its channels needs one of its own.");
		Chosen.clear();
	}
	if (Plan.Free.empty()) {
		Plan.Error = "No channel is free in " + BarsText(T, First, End) + ": every one that can play a chord's notes plays there. "
			"Clear one, or put the chord on a note as an arpeggio (Effects > Chord).";
		return false;
	}
	Plan.TooFew = !Request.Arpeggio && Plan.Free.size() < static_cast<size_t>(Plan.Wanted);
	if (Chosen.empty() || (!Request.Arpeggio && Chosen.size() < 2) || Sounds.size() != Chosen.size()) {
		Plan.TooFew = !Request.Arpeggio;
		return true;	// a single free channel: only the arpeggio fits
	}

	if (Request.Arpeggio) {
		// One channel: each chord the arpeggio of a held note, as add_chords_and_bass writes arpeggio chords
		const HelperChannel &Channel = Song.Channels[static_cast<size_t>(Chosen[0])];
		ChordPartOptions O;
		O.Style = CHORDS_ARPEGGIO;
		O.Rhythm = -1;
		O.RowsPerBeat = R;
		int Top = Space.Above.empty() ? 43 : MiddleOf(Space.Above, 52) - 7;
		Top = std::max(36, std::min(48, Top));
		O.High = Top;
		O.Low = Top - 12;
		O.Above = Space.Above;
		O.Below = Space.Below;
		O.Ceiling = Space.UsualLow >= 0 ? std::max(0, Space.UsualLow - CHORD_ROOM) : -1;
		O.RootInBass = !Space.Below.empty();
		O.Volume = ARPEGGIO_VOLUME;
		ChordVoice Voice;
		Voice.Channel = Chosen[0];
		Voice.Sound = Sounds[0];
		Voice.Notes = ChordPart(Plan.Chords, Plan.Bars, O);
		for (size_t i = 0; i < Voice.Notes.size(); ++i) {
			Voice.Notes[i].Pitch = WrittenOn(Channel, Voice.Notes[i].Pitch);
			Voice.Notes[i].Volume = VolumeOn(Channel, Voice.Notes[i].Volume);
		}
		Plan.Arpeggio = true;
		Plan.Voices.push_back(Voice);
		return true;
	}

	// The voices, led from one chord to the next, lowest first
	ChordPartOptions O;
	O.Style = CHORDS_VOICES;
	O.Rhythm = -1;		// held: struck once a bar and where the chord changes
	O.RowsPerBeat = R;
	int Top = Space.Above.empty() ? 50 : MiddleOf(Space.Above, 57) - 9;
	if (Space.UsualLow >= 0)
		Top = std::min(Top, Space.UsualLow - 4);
	Top = std::max(31, std::min(55, Top));
	O.Low = Top - 10;
	O.High = Top + 3;
	O.Above = Space.Above;
	O.Below = Space.Below;
	O.Ceiling = Space.UsualLow >= 0 ? std::max(0, Space.UsualLow - CHORD_ROOM) : -1;
	O.RootInBass = !Space.Below.empty();
	O.Volume = VOICE_VOLUME;
	std::vector<ChordAt> Chords = Plan.Chords;
	std::vector<BarSpan> VoiceBars = Plan.Bars;
	if (Leads && Chosen == std::vector<int>(Request.Prefer.begin(), Request.Prefer.begin() +
		static_cast<long>(std::min(Request.Prefer.size(), Chosen.size())))) {
		Chords.insert(Chords.begin(), Request.LeadFrom.begin(), Request.LeadFrom.end());
		const std::vector<BarSpan> Before = BarsIn(T, From, First);
		VoiceBars.insert(VoiceBars.begin(), Before.begin(), Before.end());
	}
	const std::vector<std::vector<PartNote> > Parts = VoicedChordParts(Chords, VoiceBars, O, static_cast<int>(Chosen.size()));
	for (size_t v = 0; v < Chosen.size(); ++v) {
		const HelperChannel &Channel = Song.Channels[static_cast<size_t>(Chosen[v])];
		ChordVoice Voice;
		Voice.Channel = Chosen[v];
		Voice.Sound = Sounds[v];
		if (v < Parts.size())
			for (size_t i = 0; i < Parts[v].size(); ++i) {
				if (Parts[v][i].Row < First)
					continue;	// the chords led from
				PartNote N = Parts[v][i];
				N.Pitch = WrittenOn(Channel, N.Pitch);
				N.Volume = VolumeOn(Channel, N.Volume);
				Voice.Notes.push_back(N);
			}
		Plan.Voices.push_back(Voice);
	}

	// A chord's note the melody rubs against a semitone away, held a while (the chord may not be the one for there)
	int Rubs = 0;
	for (size_t c = 0; c < Plan.Chords.size() && Rubs < 2 && !Space.Above.empty(); ++c) {
		const ChordAt &A = Plan.Chords[c];
		std::set<int> Classes;
		for (size_t v = 0; v < Plan.Voices.size(); ++v)
			for (size_t i = 0; i < Plan.Voices[v].Notes.size(); ++i) {
				const PartNote &N = Plan.Voices[v].Notes[i];
				if (N.Row >= A.Row && N.Row < A.Row + A.Rows)
					Classes.insert(Mod12(N.Pitch - Lift(Song.Channels[static_cast<size_t>(Plan.Voices[v].Channel)])));
			}
		for (std::set<int>::const_iterator It = Classes.begin(); It != Classes.end(); ++It) {
			if (MelodyRub(Space.Above, A.Row, A.Row + A.Rows, *It, R) < 2.0)
				continue;
			Plan.Remarks.push_back("In " + BarsText(T, A.Row, A.Row + A.Rows) + " the melody rubs a semitone against the chord's " +
				NoteClassName(*It, Song.K) + ": another chord may fit better there.");
			++Rubs;
			break;
		}
	}
	return true;
}

std::vector<VoiceCells> PlanCells(const HelperSong &Song, const ChordPlan &Plan, const std::vector<int> &Instruments,
	std::vector<std::string> &Warnings)
{
	std::vector<VoiceCells> Out;
	if (Plan.Voices.empty() || Plan.End <= Plan.First)
		return Out;
	// Where the song goes on after its end: a voice held to the end shouldn't ring on there
	std::vector<std::vector<Cell> > Cells;
	std::vector<int> Columns;
	for (size_t c = 0; c < Song.Channels.size(); ++c) {
		Cells.push_back(Song.Channels[c].Cells);
		Columns.push_back(Song.Channels[c].EffectColumns);
	}
	const SongFlow Flow = FollowSong(Cells, Columns, Song.PatternLength, Song.Frames);
	const int LoopRow = Flow.LoopRow(Song.PatternLength);
	std::vector<bool> Releases(static_cast<size_t>(NO_INSTRUMENT), false);
	for (size_t i = 0; i < Song.Instruments.size(); ++i)
		if (Song.Instruments[i].Index >= 0 && Song.Instruments[i].Index < NO_INSTRUMENT)
			Releases[static_cast<size_t>(Song.Instruments[i].Index)] = Song.Instruments[i].Releases;
	PartOptions Options;
	Options.Releases = Releases;
	for (size_t v = 0; v < Plan.Voices.size(); ++v) {
		const ChordVoice &Voice = Plan.Voices[v];
		if (Voice.Channel < 0 || Voice.Channel >= static_cast<int>(Song.Channels.size()))
			continue;
		const HelperChannel &Channel = Song.Channels[static_cast<size_t>(Voice.Channel)];
		const int Instrument = v < Instruments.size() && Instruments[v] >= 0 && Instruments[v] < NO_INSTRUMENT ? Instruments[v] :
			NO_INSTRUMENT;
		std::vector<PartNote> Notes = Voice.Notes;
		for (size_t i = 0; i < Notes.size(); ++i)
			Notes[i].Instrument = Instrument;
		if (!Notes.empty() && Plan.End >= Song.Rows() && LoopRow >= 0) {
			PartNote &Last = Notes.back();
			const Cell AtLoop = LoopRow < static_cast<int>(Channel.Cells.size()) ? Channel.Cells[static_cast<size_t>(LoopRow)] : Cell();
			if (Last.Row + Last.Rows >= Plan.End && Last.Rows > 1 && !AtLoop.HasNote() && !AtLoop.IsEnd())
				Last.Rows = Plan.End - 1 - Last.Row;
		}
		Options.EffectColumns = Channel.EffectColumns;
		VoiceCells Written;
		Written.Channel = Voice.Channel;
		Written.Row = Plan.First;
		BuildPart(Channel.Cells, Plan.First, Plan.End, Notes, Options, Written.Cells, Warnings);
		Out.push_back(Written);
	}
	return Out;
}

std::string PlanChordsText(const HelperSong &Song, const ChordPlan &Plan)
{
	std::string Names;
	int Shown = 0;
	for (size_t i = 0; i < Plan.Chords.size(); ++i) {
		if (i > 0 && Plan.Chords[i].C == Plan.Chords[i - 1].C)
			continue;	// held on
		if (++Shown > 8) {
			Names += " ...";
			break;
		}
		Names += (Names.empty() ? "" : " ") + ChordName(Plan.Chords[i].C, Song.K);
	}
	return Names + " on " + BarsText(Song.Time(), Plan.First, Plan.End);
}

std::string PlanVoicesText(const HelperSong &Song, const ChordPlan &Plan)
{
	std::vector<std::string> Pitches, Channels;
	for (size_t v = 0; v < Plan.Voices.size(); ++v) {
		const ChordVoice &Voice = Plan.Voices[v];
		if (Voice.Channel < 0 || Voice.Channel >= static_cast<int>(Song.Channels.size()))
			continue;
		Channels.push_back(Song.Channels[static_cast<size_t>(Voice.Channel)].Name);
		if (!Voice.Notes.empty())
			Pitches.push_back(PitchName(Voice.Notes.front().Pitch));
	}
	if (Channels.empty())
		return std::string();
	if (Plan.Arpeggio) {
		std::string Code;
		const ChordVoice &Voice = Plan.Voices.front();
		for (size_t i = 0; i < Voice.Notes.size() && Code.empty(); ++i)
			for (size_t e = 0; e < Voice.Notes[i].Effects.size(); ++e)
				if (Voice.Notes[i].Effects[e].first == EFF_ARPEGGIO)
					Code = EffectText(EFF_ARPEGGIO, Voice.Notes[i].Effects[e].second);
		return "an arpeggio" + (Code.empty() ? std::string() : " (" + Code + ")") + " on " + Channels.front();
	}
	bool OneChord = true;
	for (size_t i = 1; i < Plan.Chords.size(); ++i)
		OneChord = OneChord && Plan.Chords[i].C == Plan.Chords[0].C;
	return (OneChord && Pitches.size() == Channels.size() ? List(Pitches) + " on " : std::string("on ")) + List(Channels);
}

}	// namespace ftapi
