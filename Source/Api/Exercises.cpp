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

// The exercises and their checks (see Exercises.h)

#include "Exercises.h"
#include "Glossary.h"
#include "Json.h"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace ftapi {

namespace {

std::string Format(const char *pFormat, ...)
{
	char Buffer[1024];
	va_list Args;
	va_start(Args, pFormat);
	std::vsnprintf(Buffer, sizeof(Buffer), pFormat, Args);
	va_end(Args);
	return Buffer;
}

// The checks, in the exercises' order. Learned is said when one passes.
const ExerciseCheckInfo CHECKS[] = {
	{ "home-note", EXERCISE_MELODY, "Every [[key]] has a [[home note]] (C in C major). A tune that ends there sounds "
		"finished; one that ends on another note sounds like a question waiting for its answer." },
	{ "four-bars", EXERCISE_MELODY, "A [[melody]] that moves mostly by [[step|step-leap]], to the next note up or down, is easy "
		"to follow and to sing, and ending on the [[home note]] rounds it off." },
	{ "strong-notes", EXERCISE_MELODY, "Notes of the [[chord]] on the strong [[beats|beat]] (1 and 3) make a tune sit on its "
		"chords; the notes between them can pass from one chord note to the next." },
	{ "phrases", EXERCISE_MELODY, "A tune speaks in [[phrases|phrase]], like sentences: a held note or a [[rest]] at the end "
		"of each one lets the ear take it in before the next." },
	{ "bass-roots", EXERCISE_BASS, "The [[bass line]] holds the music up: each chord's [[root]] on the bar's first beat says "
		"clearly which chord it is." },
	{ "bass-two", EXERCISE_BASS, "Two notes a bar, the [[root]] and then the chord's [[fifth]] or the root again, make a bass "
		"that moves along without losing the chord." },
	{ "beat", EXERCISE_DRUMS, "That's the basic [[beat]] of pop music: the [[kick]] on 1, the [[snare]] on 2 and 4 (the "
		"backbeat) and the [[hi-hats|hi-hat]] keeping time in between." },
	{ "minor", EXERCISE_MELODY, "[[Major and minor|major-minor]] differ by one note: the [[third]] above the home note. Lower "
		"it a semitone and the same tune turns sad." },
	{ "hook", EXERCISE_MELODY, "A [[hook]] is a short idea the ear remembers. Playing it again and changing its end is how "
		"most tunes are built: [[repetition and variation|repetition-variation]]." },
	{ "song-shape", EXERCISE_SONG, "A song is made of parts, each with a job: an [[intro]] sets the scene, the [[A part|a-part]] "
		"is the main tune, the [[B part|b-part]] brings a change, and the [[loop]] takes it round again." },
	{ "first-song", EXERCISE_SONG, "That's a whole song: a shape, [[chords|chord]] and a [[bass line]], a [[melody]], and a "
		"[[loop]] that goes round as a game's music does. The What next panel takes any song on from here: drums, a second "
		"part, polish and the mix." },
};

}	// namespace

const std::vector<ExerciseInfo> &Exercises()
{
	static std::vector<ExerciseInfo> List;
	if (List.empty()) {
		struct Row { const char *Id, *Title, *About; int Minutes; bool NewSong; const char *Checks; };
		static const Row ROWS[] = {
			{ "home-note", "The home note", "A short tune stops too soon: give it a last note that sounds like the end.", 3, false,
				"home-note" },
			{ "four-bars", "Four bars over chords", "Write a tune of four bars over chords that are ready, mostly by step, and end "
				"it at home.", 5, false, "four-bars" },
			{ "strong-notes", "Strong notes", "Make the notes on beats 1 and 3 notes of the chord, so the tune sits on its chords.",
				4, false, "strong-notes" },
			{ "phrases", "A phrase that breathes", "Split a tune that never stops into two phrases, each ending with a rest or a "
				"held note.", 4, false, "phrases" },
			{ "bass", "A bass line", "Put the chords' roots on the triangle, one on each bar's first beat, then two notes a bar.", 5,
				false, "bass-roots bass-two" },
			{ "beat", "A beat", "A kick, a snare on beats 2 and 4, and hi-hats in between, with the drum grid.", 4, false, "beat" },
			{ "minor", "Major and minor", "Make a happy tune sad by lowering one note, its third, and hear the difference.", 4,
				false, "minor" },
			{ "hook", "A hook that comes back", "Play a two-bar hook again, with a different ending.", 4, false, "hook" },
			{ "song-shape", "The song's shape", "Lay out a new song's parts with Module > Song shape, and read what each one is "
				"for.", 3, true, "song-shape" },
			{ "first-song", "A first small song", "From an empty song to a loop that plays, a step at a time with the What next "
				"panel.", 10, true, "first-song" },
		};
		for (size_t i = 0; i < sizeof(ROWS) / sizeof(ROWS[0]); ++i) {
			ExerciseInfo E;
			E.Id = ROWS[i].Id;
			E.Title = ROWS[i].Title;
			E.About = ROWS[i].About;
			E.Minutes = ROWS[i].Minutes;
			E.NewSong = ROWS[i].NewSong;
			std::string Word;
			for (const char *p = ROWS[i].Checks; ; ++p) {
				if (*p == ' ' || *p == 0) {
					if (!Word.empty())
						E.Checks.push_back(Word);
					Word.clear();
					if (*p == 0)
						break;
				}
				else
					Word += *p;
			}
			List.push_back(E);
		}
	}
	return List;
}

const ExerciseInfo *FindExercise(const std::string &Id)
{
	const std::vector<ExerciseInfo> &List = Exercises();
	for (size_t i = 0; i < List.size(); ++i)
		if (Id == List[i].Id)
			return &List[i];
	return 0;
}

const ExerciseInfo *ExerciseAfter(const std::string &Id)
{
	const std::vector<ExerciseInfo> &List = Exercises();
	for (size_t i = 0; i + 1 < List.size(); ++i)
		if (Id == List[i].Id)
			return &List[i + 1];
	return 0;
}

const std::vector<ExerciseCheckInfo> &ExerciseChecks()
{
	static const std::vector<ExerciseCheckInfo> List(CHECKS, CHECKS + sizeof(CHECKS) / sizeof(CHECKS[0]));
	return List;
}

const ExerciseCheckInfo *FindExerciseCheck(const std::string &Id)
{
	const std::vector<ExerciseCheckInfo> &List = ExerciseChecks();
	for (size_t i = 0; i < List.size(); ++i)
		if (Id == List[i].Id)
			return &List[i];
	return 0;
}

const ExerciseInfo *ExerciseOfCheck(const std::string &Check)
{
	const std::vector<ExerciseInfo> &List = Exercises();
	for (size_t i = 0; i < List.size(); ++i)
		if (std::find(List[i].Checks.begin(), List[i].Checks.end(), Check) != List[i].Checks.end())
			return &List[i];
	return 0;
}

// Drums

int ExerciseDrumKind(const std::string &Instrument, int Pitch, bool Noise)
{
	const std::string Name = Lowercase(Instrument);
	const bool Hat = Name.find("hat") != std::string::npos;
	if (Hat && Name.find("open") != std::string::npos)
		return DRUM_HIT_OPEN_HIHAT;
	if (Hat)
		return DRUM_HIT_HIHAT;
	if (Name.find("kick") != std::string::npos || Name.find("bass drum") != std::string::npos)
		return DRUM_HIT_KICK;
	if (Name.find("snare") != std::string::npos || Name.find("clap") != std::string::npos)
		return DRUM_HIT_SNARE;
	if (Name.find("crash") != std::string::npos || Name.find("cymbal") != std::string::npos)
		return DRUM_HIT_CRASH;
	if (Name.find("tom") != std::string::npos)
		return DRUM_HIT_TOM;
	if (!Noise)
		return DRUM_HIT_OTHER;
	// By its pitch, as the drum grid puts them: low noise thumps, the middle cracks, the top ticks
	const int p = Pitch & 15;
	return p <= 6 ? DRUM_HIT_KICK : p <= 11 ? DRUM_HIT_SNARE : DRUM_HIT_HIHAT;
}

const char *ExerciseDrumName(int Kind)
{
	static const char *const NAMES[] = { "kick", "snare", "hi-hat", "open hi-hat", "crash", "tom", "drum" };
	return Kind >= 0 && Kind <= DRUM_HIT_OTHER ? NAMES[Kind] : "drum";
}

bool ExerciseChord::Has(int PitchClass) const
{
	return std::find(Notes.begin(), Notes.end(), (PitchClass % 12 + 12) % 12) != Notes.end();
}

ExerciseFacts::ExerciseFacts() : PatternLength(64), Frames(1), RowsPerBeat(4), RowsPerBar(16), Target(-1), Melody(-1),
	HasKey(false), KeyChosen(false), HasFound(false), Spans(0), Breaths(0), Halts(false), EndBar(1)
{
}

// Links

std::string UnmarkLinks(const std::string &Marked, std::vector<ExerciseLink> *pLinks)
{
	std::string Plain;
	if (pLinks != 0)
		pLinks->clear();
	size_t At = 0;
	while (At < Marked.size()) {
		const size_t Open = Marked.find("[[", At);
		const size_t Close = Open == std::string::npos ? std::string::npos : Marked.find("]]", Open + 2);
		if (Close == std::string::npos) {
			Plain += Marked.substr(At);
			break;
		}
		Plain += Marked.substr(At, Open - At);
		const std::string Inside = Marked.substr(Open + 2, Close - Open - 2);
		const size_t Bar = Inside.find('|');
		const std::string Words = Bar == std::string::npos ? Inside : Inside.substr(0, Bar);
		const GlossaryEntry *pEntry = FindGlossaryEntry(Bar == std::string::npos ? Inside : Inside.substr(Bar + 1));
		if (pEntry != 0 && pLinks != 0) {
			ExerciseLink Link;
			Link.First = Plain.size();
			Link.Length = Words.size();
			Link.Id = pEntry->Id;
			pLinks->push_back(Link);
		}
		Plain += Words;
		At = Close + 2;
	}
	return Plain;
}

std::vector<std::string> MarkedLinks(const std::string &Marked)
{
	std::vector<std::string> Out;
	size_t At = 0;
	while (true) {
		const size_t Open = Marked.find("[[", At);
		const size_t Close = Open == std::string::npos ? std::string::npos : Marked.find("]]", Open + 2);
		if (Close == std::string::npos)
			break;
		const std::string Inside = Marked.substr(Open + 2, Close - Open - 2);
		const size_t Bar = Inside.find('|');
		const GlossaryEntry *pEntry = FindGlossaryEntry(Bar == std::string::npos ? Inside : Inside.substr(Bar + 1));
		Out.push_back(pEntry != 0 ? pEntry->Id : std::string());
		At = Close + 2;
	}
	return Out;
}

namespace {

// Reading the facts

const ExerciseChannel *TargetOf(const ExerciseFacts &F)
{
	return F.Target >= 0 && F.Target < static_cast<int>(F.Channels.size()) ? &F.Channels[static_cast<size_t>(F.Target)] : 0;
}

// A melodic channel's notes that sound (a volume of 0 is a rest), in time order
std::vector<ExerciseNote> Sounding(const ExerciseChannel *pChannel)
{
	std::vector<ExerciseNote> Out;
	if (pChannel == 0)
		return Out;
	for (size_t i = 0; i < pChannel->Notes.size(); ++i)
		if (pChannel->Notes[i].Volume > 0)
			Out.push_back(pChannel->Notes[i]);
	std::sort(Out.begin(), Out.end(), [](const ExerciseNote &a, const ExerciseNote &b) { return a.Row < b.Row; });
	return Out;
}

int Tonic(const ExerciseFacts &F)
{
	return F.HasKey ? F.K.Tonic : F.HasFound ? F.Found.Tonic : 0;
}

Key KeyOf(const ExerciseFacts &F)
{
	return F.HasKey ? F.K : F.HasFound ? F.Found : Key();
}

std::string NoteWord(int PitchClass, const Key &K)
{
	return NoteClassName((PitchClass % 12 + 12) % 12, K);
}

// "C-4" as the pattern editor writes it
std::string Written(int Pitch)
{
	return PitchName(std::max(0, std::min(PITCHES - 1, Pitch)));
}

// The pitch of a pitch class nearest Near
int NearestPitch(int PitchClass, int Near)
{
	int Pitch = Near - ((Near - PitchClass) % 12 + 12) % 12;
	if (Near - Pitch > 6)
		Pitch += 12;
	return std::max(0, std::min(PITCHES - 1, Pitch));
}

const ExerciseChord *ChordAtRow(const ExerciseFacts &F, int Row)
{
	for (size_t i = 0; i < F.Chords.size(); ++i)
		if (Row >= F.Chords[i].Row && Row < F.Chords[i].Row + F.Chords[i].Rows)
			return &F.Chords[i];
	return 0;
}

// "A, C and E"
std::string ChordNotesText(const ExerciseChord &C, const Key &K)
{
	std::string Text;
	for (size_t i = 0; i < C.Notes.size(); ++i)
		Text += (i == 0 ? "" : i + 1 == C.Notes.size() ? " and " : ", ") + NoteWord(C.Notes[i], K);
	return Text;
}

// "bar 3", "bars 1 and 2", "bars 1, 2 and 4"
std::string BarsText(const std::vector<int> &Bars)
{
	if (Bars.empty())
		return std::string();
	std::string Text = Bars.size() == 1 ? "bar " : "bars ";
	for (size_t i = 0; i < Bars.size(); ++i)
		Text += Format("%s%d", i == 0 ? "" : i + 1 == Bars.size() ? " and " : ", ", Bars[i]);
	return Text;
}

std::string Plural(int Count, const char *One, const char *Many)
{
	return Format("%d %s", Count, Count == 1 ? One : Many);
}

// The sentence's first letter in upper case
std::string Capital(std::string Text)
{
	if (!Text.empty())
		Text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(Text[0])));
	return Text;
}

// The bars the song has (as far as it plays before it ends or goes round)
int SongBars(const ExerciseFacts &F)
{
	return std::max(1, std::min(F.EndBar, F.Time().BarCount()));
}

void PlayBars(ExerciseVerdict &V, const ExerciseFacts &F, int First, int Last, int Alone = -1)
{
	const int Bars = SongBars(F);
	V.PlayFirst = std::max(1, std::min(First, Bars));
	V.PlayLast = std::max(V.PlayFirst, std::min(Last, Bars));
	V.PlayChannels.clear();
	if (Alone >= 0)
		V.PlayChannels.push_back(Alone);
}

// How a target channel is named in the texts: "Pulse 1"
std::string TargetName(const ExerciseFacts &F)
{
	const ExerciseChannel *pTarget = TargetOf(F);
	return pTarget != 0 ? pTarget->Name : std::string("the tune's channel");
}

// What a tune that's gone is told
std::string NoTune(const ExerciseFacts &F)
{
	return TargetName(F) + " has no notes now, so there's no tune to work on. Ctrl+Z brings the notes back, or Help > Exercises "
		"opens the exercise again.";
}

// Notes all in the key: a line for what's right, "" when some aren't
std::string InKeyLine(const std::vector<ExerciseNote> &Notes, const ExerciseFacts &F)
{
	if (Notes.empty() || (!F.HasKey && !F.HasFound))
		return std::string();
	const Key K = KeyOf(F);
	for (size_t i = 0; i < Notes.size(); ++i)
		if (!InKey(Notes[i].Pitch, K))
			return std::string();
	return Format("Its %s all belong to the [[key]] of %s.", Plural(static_cast<int>(Notes.size()), "note", "notes").c_str(),
		KeyName(K).c_str());
}

// The checks

// The home note: the tune's last note on the key's home note
void CheckHomeNote(const ExerciseFacts &F, ExerciseVerdict &V)
{
	const std::vector<ExerciseNote> Notes = Sounding(TargetOf(F));
	if (Notes.empty()) {
		V.Next = NoTune(F);
		return;
	}
	const Timing T = F.Time();
	const Key K = KeyOf(F);
	const int Home = Tonic(F);
	const ExerciseNote &Last = Notes.back();
	const int LastBar = T.BarOf(Last.Row), Bars = SongBars(F);
	const std::string InKeyText = InKeyLine(Notes, F);
	if (!InKeyText.empty())
		V.Right.push_back(InKeyText);
	if (Last.Pitch % 12 == Home) {
		V.Passed = true;
		V.Right.push_back(Format("It ends on %s, the [[home note]] of %s, so it sounds finished.", NoteWord(Home, K).c_str(),
			KeyName(K).c_str()));
		if (Last.Heard >= 2 * F.RowsPerBeat)
			V.Right.push_back("Its last note rings for a while, which helps it sound like the end.");
		PlayBars(V, F, LastBar - 1, Bars);
		return;
	}
	const int Suggested = NearestPitch(Home, Last.Pitch);
	std::string Where;
	if (LastBar < Bars)
		Where = Format("give bar %d a %s on its first beat and let it ring (%s in the pattern editor).", Bars, NoteWord(Home, K).c_str(),
			Written(Suggested).c_str());
	else
		Where = Format("change that last note to %s (%s), or add one after it.", NoteWord(Home, K).c_str(), Written(Suggested).c_str());
	V.Next = Format("Your tune ends on %s (bar %d), which leaves it hanging, as if it wants to go on. End it on %s, the [[home "
		"note]]: %s", NoteWord(Last.Pitch, K).c_str(), LastBar, NoteWord(Home, K).c_str(), Where.c_str());
	PlayBars(V, F, LastBar - 1, Bars);
}

// Four bars over the chords: the tune fills them, ends at home, and moves mostly by step
const double STEP_SHARE = 0.6;		// of the moves from note to note, at the least
const int STEP_SEMITONES = 2;		// a step: the next note of the scale (or the same one again)

void CheckFourBars(const ExerciseFacts &F, ExerciseVerdict &V)
{
	const std::vector<ExerciseNote> Notes = Sounding(TargetOf(F));
	const Timing T = F.Time();
	const Key K = KeyOf(F);
	const int Bars = SongBars(F), Home = Tonic(F);
	if (Notes.size() < 4) {
		std::string Chords;
		int Shown = 0;
		for (size_t i = 0; i < F.Chords.size() && Shown < 8; ++i, ++Shown)
			Chords += (i == 0 ? "" : ", ") + F.Chords[i].Name;
		if (!Chords.empty())
			V.Right.push_back("The [[chords|chord]] are ready for your tune: " + Chords + ".");
		V.Next = Format("Write a tune on %s, %d bars long: click notes into the piano roll (or type them in the pattern editor) over "
			"the chords. Start on any note, then mostly move to the next note up or down.", TargetName(F).c_str(), Bars);
		return;
	}
	std::set<int> WithNotes;
	for (size_t i = 0; i < Notes.size(); ++i)
		WithNotes.insert(T.BarOf(Notes[i].Row));
	const ExerciseNote &Last = Notes.back();
	const int LastBar = T.BarOf(Last.Row), FirstBar = T.BarOf(Notes.front().Row);
	if (static_cast<int>(WithNotes.size()) < Bars - 1 || LastBar < Bars) {
		V.Right.push_back(Format("Your tune has %s.", Plural(static_cast<int>(Notes.size()), "note", "notes").c_str()));
		V.Next = Format("Your tune plays in %s. Carry it on to bar %d, so it fills all %d bars and ends where the chords come home.",
			FirstBar == LastBar ? Format("bar %d", FirstBar).c_str() : Format("bars %d-%d", FirstBar, LastBar).c_str(), Bars, Bars);
		PlayBars(V, F, 1, Bars, F.Target);
		return;
	}
	V.Right.push_back(Format("Your tune fills all %d bars.", Bars));
	int Steps = 0, Moves = 0, FirstLeap = -1;
	for (size_t i = 1; i < Notes.size(); ++i) {
		++Moves;
		if (std::abs(Notes[i].Pitch - Notes[i - 1].Pitch) <= STEP_SEMITONES)
			++Steps;
		else if (FirstLeap < 0)
			FirstLeap = static_cast<int>(i);
	}
	const bool Smooth = Moves == 0 || Steps >= STEP_SHARE * Moves;
	if (Last.Pitch % 12 != Home) {
		if (Smooth)
			V.Right.push_back(Format("It moves mostly by [[step|step-leap]] (%d of its %d moves), so it flows.", Steps, Moves));
		V.Next = Format("Your tune ends on %s (bar %d). End it on %s, the [[home note]], and it sounds finished: change its last "
			"note to %s (%s).", NoteWord(Last.Pitch, K).c_str(), LastBar, NoteWord(Home, K).c_str(), NoteWord(Home, K).c_str(),
			Written(NearestPitch(Home, Last.Pitch)).c_str());
		PlayBars(V, F, Bars - 1, Bars);
		return;
	}
	V.Right.push_back(Format("It ends on %s, the [[home note]].", NoteWord(Home, K).c_str()));
	if (!Smooth) {
		const ExerciseNote &From = Notes[static_cast<size_t>(FirstLeap - 1)], &To = Notes[static_cast<size_t>(FirstLeap)];
		V.Next = Format("Your tune jumps a lot: %d of its %d moves are [[leaps|step-leap]] (like %s to %s in bar %d). Move to the "
			"next note up or down more often (a [[step|step-leap]]), and keep a leap or two for a surprise.", Moves - Steps, Moves,
			NoteWord(From.Pitch, K).c_str(), NoteWord(To.Pitch, K).c_str(), T.BarOf(To.Row));
		PlayBars(V, F, 1, Bars, F.Target);
		return;
	}
	V.Passed = true;
	V.Right.push_back(Format("It moves mostly by [[step|step-leap]] (%d of its %d moves).", Steps, Moves));
	PlayBars(V, F, 1, Bars);
}

// Strong notes: every note that starts on beat 1 or 3 is a note of the chord there
const int STRONG_AT_LEAST = 3;		// notes on strong beats there must be, for the check to say anything

void CheckStrongNotes(const ExerciseFacts &F, ExerciseVerdict &V)
{
	const std::vector<ExerciseNote> Notes = Sounding(TargetOf(F));
	if (Notes.empty()) {
		V.Next = NoTune(F);
		return;
	}
	const Timing T = F.Time();
	const Key K = KeyOf(F);
	struct Strong { int Bar, Beat; const ExerciseNote *pNote; const ExerciseChord *pChord; };
	std::vector<Strong> Checked, Missed;
	for (int Bar = 1; Bar <= SongBars(F); ++Bar) {
		for (int Beat = 1; Beat <= T.BeatsPerBar(Bar); Beat += 2) {
			const int Row = T.BarStart(Bar) + (Beat - 1) * F.RowsPerBeat;
			const ExerciseNote *pNote = 0;
			for (size_t i = 0; i < Notes.size() && pNote == 0; ++i)
				if (Notes[i].Row == Row)
					pNote = &Notes[i];
			const ExerciseChord *pChord = ChordAtRow(F, Row);
			if (pNote == 0 || pChord == 0)
				continue;
			const Strong S = { Bar, Beat, pNote, pChord };
			Checked.push_back(S);
			if (!pChord->Has(pNote->Pitch))
				Missed.push_back(S);
		}
	}
	if (static_cast<int>(Checked.size()) < STRONG_AT_LEAST) {
		V.Next = Format("Your tune has %s that start on beat 1 or 3, the strong [[beats|beat]]. Put a note at the start of each "
			"bar and on beat 3, then make each one a note of the [[chord]] there.", Checked.empty() ? "no notes" :
			Plural(static_cast<int>(Checked.size()), "note", "notes").c_str());
		return;
	}
	if (Missed.empty()) {
		V.Passed = true;
		V.Right.push_back(Format("All %d notes on beats 1 and 3 are notes of their [[chords|chord]], so the tune sits on them.",
			static_cast<int>(Checked.size())));
		PlayBars(V, F, 1, SongBars(F));
		return;
	}
	if (Missed.size() < Checked.size())
		V.Right.push_back(Format("%d of the %d notes on beats 1 and 3 are notes of their chords already.",
			static_cast<int>(Checked.size() - Missed.size()), static_cast<int>(Checked.size())));
	const Strong &M = Missed.front();
	V.Next = Format("Bar %d, beat %d: the tune plays %s over %s, a note from outside the [[chord]], so it rubs. Try %s, the notes "
		"of %s (with the piano roll's strong notes on, their lanes light up there).", M.Bar, M.Beat,
		NoteWord(M.pNote->Pitch, K).c_str(), M.pChord->Name.c_str(), ChordNotesText(*M.pChord, K).c_str(), M.pChord->Name.c_str());
	if (Missed.size() > 1) {
		std::string Others;
		for (size_t i = 1; i < Missed.size() && i < 4; ++i)
			Others += Format("%sbar %d beat %d", i == 1 ? "" : i + 1 == std::min(Missed.size(), static_cast<size_t>(4)) ? " and " :
				", ", Missed[i].Bar, Missed[i].Beat);
		V.Next += Format(" %s like it: %s.", Missed.size() == 2 ? "One more" : Format("%d more", static_cast<int>(Missed.size() - 1)).c_str(),
			Others.c_str());
	}
	PlayBars(V, F, M.Bar, M.Bar);
}

// A phrase that breathes: both two-bar phrases pause at their end (a rest or a held note, as the review hears it)
void CheckPhrases(const ExerciseFacts &F, ExerciseVerdict &V)
{
	const std::vector<ExerciseNote> Notes = Sounding(TargetOf(F));
	if (Notes.empty()) {
		V.Next = NoTune(F);
		return;
	}
	const int Bars = SongBars(F), Wanted = std::max(1, Bars / 2);
	if (F.Spans < Wanted) {
		const Timing T = F.Time();
		const int First = T.BarOf(Notes.front().Row), Last = T.BarOf(Notes.back().Row);
		V.Next = Format("Your tune plays only in %s now. Keep both [[phrases|phrase]]: one in bars 1-2 and one in bars 3-4, each "
			"ending with a breath.", First == Last ? Format("bar %d", First).c_str() : Format("bars %d-%d", First, Last).c_str());
		return;
	}
	// The spans that pause, for what's right
	std::vector<int> Breathing;
	for (int Bar = 1; Bar <= Bars; Bar += 2)
		if (std::find(F.RunOn.begin(), F.RunOn.end(), Bar) == F.RunOn.end())
			Breathing.push_back(Bar);
	for (size_t i = 0; i < Breathing.size(); ++i)
		V.Right.push_back(Format("Bars %d-%d breathe at their end: that [[phrase]] has its pause.", Breathing[i], Breathing[i] + 1));
	if (F.Breaths >= F.Spans || F.RunOn.empty()) {
		V.Passed = true;
		V.Right.clear();
		V.Right.push_back(Format("Each of its %d phrases ends with a breath: a held note or a [[rest]].", F.Spans));
		PlayBars(V, F, 1, Bars, F.Target);
		return;
	}
	const int Bar = F.RunOn.front();
	V.Next = Format("Bars %d-%d run on without a breath: the tune never stops to rest there. End that [[phrase]] with a note "
		"held two beats or more, or a [[rest]] of a beat: in the piano roll, drag the last note of bar %d longer, or take away a "
		"note or two at its end.", Bar, Bar + 1, Bar + 1);
	PlayBars(V, F, Bar, Bar + 1, F.Target);
}

// The bass note suggested for a pitch class: in the triangle's best octave (written E-2 to D#3), or near the bass's
// other notes
int BassPitch(int PitchClass, const std::vector<ExerciseNote> &Bass)
{
	if (!Bass.empty()) {
		std::vector<int> Pitches;
		for (size_t i = 0; i < Bass.size(); ++i)
			Pitches.push_back(Bass[i].Pitch);
		std::sort(Pitches.begin(), Pitches.end());
		return NearestPitch(PitchClass, Pitches[Pitches.size() / 2]);
	}
	return PitchClass >= 4 ? 24 + PitchClass : 36 + PitchClass;
}

// The bass's notes that start in a bar, and the chord at its first row
struct BassBar {
	int Bar;
	const ExerciseChord *pChord;
	std::vector<ExerciseNote> Notes;
};

std::vector<BassBar> BassBars(const ExerciseFacts &F, const std::vector<ExerciseNote> &Bass)
{
	const Timing T = F.Time();
	std::vector<BassBar> Out;
	for (int Bar = 1; Bar <= SongBars(F); ++Bar) {
		BassBar B;
		B.Bar = Bar;
		B.pChord = ChordAtRow(F, T.BarStart(Bar));
		if (B.pChord == 0)
			continue;
		for (size_t i = 0; i < Bass.size(); ++i)
			if (T.BarOf(Bass[i].Row) == Bar)
				B.Notes.push_back(Bass[i]);
		Out.push_back(B);
	}
	return Out;
}

// The root on each bar's first beat; false with what to change in Next
bool RootsOnBeatOne(const ExerciseFacts &F, const std::vector<ExerciseNote> &Bass, ExerciseVerdict &V, std::vector<int> &Good)
{
	const Timing T = F.Time();
	const Key K = KeyOf(F);
	const std::vector<BassBar> Bars = BassBars(F, Bass);
	for (size_t b = 0; b < Bars.size(); ++b) {
		const BassBar &B = Bars[b];
		const int Start = T.BarStart(B.Bar);
		const ExerciseNote *pFirst = 0;
		for (size_t i = 0; i < B.Notes.size() && pFirst == 0; ++i)
			if (B.Notes[i].Row < Start + F.RowsPerBeat)
				pFirst = &B.Notes[i];
		const std::string Root = NoteWord(B.pChord->Root, K);
		const std::string Suggested = Written(BassPitch(B.pChord->Root, Bass));
		if (pFirst == 0) {
			V.Next = Format("Bar %d has no bass note on its first [[beat]]. The chord there is %s, built on %s, its [[root]]: put "
				"%s %s on %s at the start of bar %d (%s).", B.Bar, B.pChord->Name.c_str(), Root.c_str(),
				Root[0] == 'A' || Root[0] == 'E' || Root[0] == 'F' ? "an" : "a", Root.c_str(), TargetName(F).c_str(), B.Bar,
				Suggested.c_str());
			PlayBars(V, F, B.Bar, B.Bar, F.Target);
			return false;
		}
		if (pFirst->Pitch % 12 != B.pChord->Root) {
			V.Next = Format("Bar %d's bass plays %s on its first beat, but the chord there is %s. The bass sounds best on the "
				"chord's [[root]], the note it's built on: %s (%s).", B.Bar, NoteWord(pFirst->Pitch, K).c_str(), B.pChord->Name.c_str(),
				Root.c_str(), Suggested.c_str());
			PlayBars(V, F, B.Bar, B.Bar, F.Target);
			return false;
		}
		Good.push_back(B.Bar);
	}
	return true;
}

void CheckBassRoots(const ExerciseFacts &F, ExerciseVerdict &V)
{
	const std::vector<ExerciseNote> Bass = Sounding(TargetOf(F));
	if (F.Chords.empty()) {
		V.Next = "The chords are gone, so the bass has nothing to follow. Ctrl+Z brings them back, or Help > Exercises opens the "
			"exercise again.";
		return;
	}
	std::vector<int> Good;
	const bool Done = RootsOnBeatOne(F, Bass, V, Good);
	if (!Good.empty())
		V.Right.push_back(Format("%s %s the chord's [[root]] on the first beat.", Capital(BarsText(Good)).c_str(),
			Good.size() == 1 ? "has" : "have"));
	if (!Done)
		return;
	V.Passed = true;
	V.Right.clear();
	V.Right.push_back(Format("Every bar starts with its chord's [[root]] in the bass (%s).", BarsText(Good).c_str()));
	PlayBars(V, F, 1, SongBars(F), F.Target);
}

// Two notes a bar: the root first, then another note of the chord
void CheckBassTwo(const ExerciseFacts &F, ExerciseVerdict &V)
{
	const std::vector<ExerciseNote> Bass = Sounding(TargetOf(F));
	if (F.Chords.empty()) {
		V.Next = "The chords are gone, so the bass has nothing to follow. Ctrl+Z brings them back.";
		return;
	}
	std::vector<int> Good;
	if (!RootsOnBeatOne(F, Bass, V, Good))
		return;
	V.Right.push_back("Every bar starts with its chord's [[root]].");
	const Key K = KeyOf(F);
	const std::vector<BassBar> Bars = BassBars(F, Bass);
	std::vector<int> Two;
	for (size_t b = 0; b < Bars.size(); ++b) {
		const BassBar &B = Bars[b];
		if (B.Notes.size() < 2) {
			const ExerciseChord &C = *B.pChord;
			const int Fifth = (C.Root + 7) % 12;
			V.Next = Format("Bar %d has one bass note. Add a second on beat 3: the chord's [[fifth]] (%s) or its root again (%s). Two "
				"notes a bar make the bass move along.", B.Bar, C.Has(Fifth) ? NoteWord(Fifth, K).c_str() :
				NoteWord(C.Notes.size() > 2 ? C.Notes[2] : C.Root, K).c_str(), NoteWord(C.Root, K).c_str());
			PlayBars(V, F, B.Bar, B.Bar, F.Target);
			if (!Two.empty())
				V.Right.push_back(Format("%s %s two notes already.", Capital(BarsText(Two)).c_str(), Two.size() == 1 ? "has" : "have"));
			return;
		}
		for (size_t i = 1; i < B.Notes.size(); ++i) {
			const ExerciseChord *pChord = ChordAtRow(F, B.Notes[i].Row);
			if (pChord != 0 && !pChord->Has(B.Notes[i].Pitch)) {
				V.Next = Format("Bar %d's bass plays %s after its root, a note outside %s, so it rubs against the chord. Try %s.",
					B.Bar, NoteWord(B.Notes[i].Pitch, K).c_str(), pChord->Name.c_str(), ChordNotesText(*pChord, K).c_str());
				PlayBars(V, F, B.Bar, B.Bar, F.Target);
				return;
			}
		}
		Two.push_back(B.Bar);
	}
	V.Passed = true;
	V.Right.push_back(Format("Each bar has two bass notes or more, all of them notes of its chord (%s).", BarsText(Two).c_str()));
	PlayBars(V, F, 1, SongBars(F), F.Target);
}

// A beat: in every bar a kick on beat 1, a snare on beats 2 and 4, and hi-hats
const int HATS_A_BAR = 2;

void CheckBeat(const ExerciseFacts &F, ExerciseVerdict &V)
{
	const Timing T = F.Time();
	// Every drum the song plays (the noise's and the DPCM's samples)
	std::vector<std::pair<int, int> > Hits;	// row, exercise_drum
	int Drums = F.Target;
	for (size_t c = 0; c < F.Channels.size(); ++c) {
		const ExerciseChannel &C = F.Channels[c];
		if (C.Kind != KIND_NOISE && C.Kind != KIND_DPCM)
			continue;
		if (Drums < 0 && C.Kind == KIND_NOISE)
			Drums = static_cast<int>(c);
		for (size_t i = 0; i < C.Notes.size() && i < C.Hits.size(); ++i)
			if (C.Notes[i].Volume > 0)
				Hits.push_back(std::make_pair(C.Notes[i].Row, C.Hits[i]));
	}
	auto HitAt = [&](int Row, int Kind) {
		for (size_t i = 0; i < Hits.size(); ++i)
			if (Hits[i].first == Row && Hits[i].second == Kind)
				return true;
		return false;
	};
	std::vector<int> Done;
	const int Bars = SongBars(F);
	for (int Bar = 1; Bar <= Bars; ++Bar) {
		const int Start = T.BarStart(Bar), Beats = T.BeatsPerBar(Bar), Step = F.RowsPerBeat;
		std::string Missing;
		if (!HitAt(Start, DRUM_HIT_KICK))
			Missing = Format("Bar %d has no [[kick]] on beat 1. In the drum grid (View > Drum grid), click the Kick row at step 1: "
				"the kick starts the bar.", Bar);
		else {
			for (int Beat = 2; Beat <= Beats && Missing.empty(); Beat += 2)
				if (!HitAt(Start + (Beat - 1) * Step, DRUM_HIT_SNARE))
					Missing = Format("Bar %d has no [[snare]] on beat %d (step %d in the drum grid). The snare on beats 2 and 4 is "
						"the backbeat that makes you nod along.", Bar, Beat, (Beat - 1) * Step + 1);
		}
		if (Missing.empty()) {
			int Hats = 0;
			for (size_t i = 0; i < Hits.size(); ++i)
				if (T.BarOf(Hits[i].first) == Bar && (Hits[i].second == DRUM_HIT_HIHAT || Hits[i].second == DRUM_HIT_OPEN_HIHAT))
					++Hats;
			if (Hats < HATS_A_BAR) {
				std::string Between;
				for (int Beat = 1; Beat <= Beats; ++Beat)
					Between += Format("%s%d", Beat == 1 ? "" : Beat == Beats ? " and " : ", ", (Beat - 1) * Step + 1 + Step / 2);
				Missing = Format("Bar %d has %s. Click the Hi-hat row between the kicks and the snares (steps %s) to keep time.",
					Bar, Hats == 0 ? "no [[hi-hats|hi-hat]] yet" : "one [[hi-hat]] only", Between.c_str());
			}
		}
		if (!Missing.empty()) {
			if (!Done.empty())
				V.Right.push_back(Format("%s %s the whole beat: a kick on 1, snares on 2 and 4, and hi-hats.",
					Capital(BarsText(Done)).c_str(), Done.size() == 1 ? "has" : "have"));
			V.Next = Missing;
			PlayBars(V, F, Bar, Bar, Drums);
			return;
		}
		Done.push_back(Bar);
	}
	V.Passed = true;
	V.Right.push_back(Format("%s a [[kick]] on 1, [[snares|snare]] on 2 and 4, and [[hi-hats|hi-hat]] in between.",
		Bars == 1 ? "The bar has" : Bars == 2 ? "Both bars have" : "Every bar has"));
	PlayBars(V, F, 1, Bars, Drums);
}

// Major and minor: the tune's third above the home note lowered, so it sounds minor. The home note is where the music
// comes to rest: the bass's last note (the melody's when there's no bass), else the key's
int RestingNote(const ExerciseFacts &F, const std::vector<ExerciseNote> &Melody)
{
	for (size_t c = 0; c < F.Channels.size(); ++c) {
		if (F.Channels[c].Kind != KIND_TRIANGLE || static_cast<int>(c) == F.Target)
			continue;
		const std::vector<ExerciseNote> Bass = Sounding(&F.Channels[c]);
		if (!Bass.empty())
			return Bass.back().Pitch % 12;
	}
	if (!Melody.empty())
		return Melody.back().Pitch % 12;
	return Tonic(F);
}

void CheckMinor(const ExerciseFacts &F, ExerciseVerdict &V)
{
	const std::vector<ExerciseNote> Notes = Sounding(TargetOf(F));
	if (Notes.size() < 4) {
		V.Next = NoTune(F);
		return;
	}
	const Timing T = F.Time();
	const int Home = RestingNote(F, Notes);
	const Key Major(Home, MODE_MAJOR), Minor(Home, MODE_MINOR);
	std::vector<int> HappyBars, SadBars;
	int Happy = 0, Sad = 0;
	for (size_t i = 0; i < Notes.size(); ++i) {
		const int Interval = ((Notes[i].Pitch - Home) % 12 + 12) % 12;
		const int Bar = T.BarOf(Notes[i].Row);
		if (Interval == 4) {
			++Happy;
			if (std::find(HappyBars.begin(), HappyBars.end(), Bar) == HappyBars.end())
				HappyBars.push_back(Bar);
		}
		else if (Interval == 3) {
			++Sad;
			if (std::find(SadBars.begin(), SadBars.end(), Bar) == SadBars.end())
				SadBars.push_back(Bar);
		}
	}
	// Or the whole tune moved into a minor key: no major third over that key's home note, its minor third there
	bool MovedKey = false;
	if (F.HasFound && F.Found.Mode == MODE_MINOR && F.Found.Tonic != Home) {
		int Bright = 0, Dark = 0;
		for (size_t i = 0; i < Notes.size(); ++i) {
			const int Interval = ((Notes[i].Pitch - F.Found.Tonic) % 12 + 12) % 12;
			Bright += Interval == 4 ? 1 : 0;
			Dark += Interval == 3 ? 1 : 0;
		}
		MovedKey = Bright == 0 && Dark > 0;
	}
	const std::string Third = NoteWord(Home + 4, Major), Lowered = NoteWord(Home + 3, Minor), HomeName = NoteWord(Home, Major);
	if ((Happy == 0 && Sad > 0) || MovedKey) {
		V.Passed = true;
		V.Right.push_back(MovedKey ? Format("Your tune is in %s now: the same shape in a minor key.", KeyName(F.Found).c_str()) :
			Format("Your tune plays %s where it played %s: the same tune, now in %s minor.", Lowered.c_str(), Third.c_str(),
			HomeName.c_str()));
		PlayBars(V, F, 1, SongBars(F));
		return;
	}
	if (Happy == 0) {
		V.Next = Format("Your tune has no %s now, and no %s either, so it's neither happy nor sad. Put %s where the %s notes were: "
			"one semitone under %s.", Third.c_str(), Lowered.c_str(), Lowered.c_str(), Third.c_str(), Third.c_str());
		return;
	}
	if (Sad > 0)
		V.Right.push_back(Format("%s %s %s already (%s).", Plural(Sad, "note", "notes").c_str(), Sad == 1 ? "is" : "are",
			Lowered.c_str(), BarsText(SadBars).c_str()));
	V.Next = Format("Your tune still plays %s in %s: %s is the [[third]] above %s that makes it sound bright and happy ([[major "
		"and minor|major-minor]]). Lower each %s one semitone, to %s: in the piano roll, drag it one lane down (in the pattern "
		"editor, type it again a key lower).", Third.c_str(), BarsText(HappyBars).c_str(), Third.c_str(), HomeName.c_str(),
		Third.c_str(), Lowered.c_str());
	PlayBars(V, F, HappyBars.front(), HappyBars.front(), -1);
}

// A hook that comes back: bars 3-4 start like bars 1-2 and end otherwise
const int HOOK_NOTES = 4;		// of the hook's first bar, compared with the repeat's

struct BarNote { int Offset, Pitch; };

std::vector<BarNote> NotesInBar(const ExerciseFacts &F, const std::vector<ExerciseNote> &Notes, int Bar)
{
	const Timing T = F.Time();
	std::vector<BarNote> Out;
	for (size_t i = 0; i < Notes.size(); ++i)
		if (T.BarOf(Notes[i].Row) == Bar) {
			const BarNote N = { Notes[i].Row - T.BarStart(Bar), Notes[i].Pitch };
			Out.push_back(N);
		}
	return Out;
}

// How many of the first notes of A the same places of B play: the same notes, or the same moved up or down together (a
// step of the scale moves some notes a semitone and others two, so a semitone either way counts)
int Alike(const std::vector<BarNote> &A, const std::vector<BarNote> &B, size_t Count)
{
	int Same = 0, Moved = 0;
	const int Shift = !A.empty() && !B.empty() ? B[0].Pitch - A[0].Pitch : 0;
	for (size_t i = 0; i < A.size() && i < Count; ++i)
		for (size_t j = 0; j < B.size(); ++j)
			if (B[j].Offset == A[i].Offset) {
				Same += B[j].Pitch == A[i].Pitch ? 1 : 0;
				Moved += std::abs(B[j].Pitch - A[i].Pitch - Shift) <= 1 ? 1 : 0;
				break;
			}
	return std::max(Same, Moved);
}

bool SameNotes(const std::vector<BarNote> &A, const std::vector<BarNote> &B)
{
	if (A.size() != B.size())
		return false;
	for (size_t i = 0; i < A.size(); ++i)
		if (A[i].Offset != B[i].Offset || A[i].Pitch != B[i].Pitch)
			return false;
	return true;
}

void CheckHook(const ExerciseFacts &F, ExerciseVerdict &V)
{
	const std::vector<ExerciseNote> Notes = Sounding(TargetOf(F));
	const Key K = KeyOf(F);
	const std::vector<BarNote> Bar1 = NotesInBar(F, Notes, 1), Bar2 = NotesInBar(F, Notes, 2), Bar3 = NotesInBar(F, Notes, 3),
		Bar4 = NotesInBar(F, Notes, 4);
	if (Bar1.size() + Bar2.size() < 3) {
		V.Next = "Bars 1-2 have lost the [[hook]]. Ctrl+Z brings it back, or Help > Exercises opens the exercise again.";
		return;
	}
	std::string First;
	for (size_t i = 0; i < Bar1.size() && i < static_cast<size_t>(HOOK_NOTES); ++i)
		First += (i == 0 ? "" : i + 1 == std::min(Bar1.size(), static_cast<size_t>(HOOK_NOTES)) ? " and " : ", ") +
			NoteWord(Bar1[i].Pitch, K);
	if (Bar3.size() + Bar4.size() < 3) {
		V.Right.push_back("Your [[hook]] is in bars 1-2: it starts " + First + ".");
		V.Next = "Bars 3-4 have no tune yet: play your [[hook]] again there. In the piano roll, select the notes of bars 1-2, copy "
			"them (Ctrl+C), click at the start of bar 3 and paste (Ctrl+V).";
		PlayBars(V, F, 1, 2, F.Target);
		return;
	}
	const size_t Compared = std::min(Bar1.size(), static_cast<size_t>(HOOK_NOTES));
	if (Alike(Bar1, Bar3, Compared) < static_cast<int>(std::min(Compared, static_cast<size_t>(3)))) {
		V.Next = "Bar 3 doesn't start like your [[hook]], so the ear doesn't know it again. Start bar 3 with the hook's first notes, " +
			First + ", as bar 1 does.";
		PlayBars(V, F, 1, 4, F.Target);
		return;
	}
	V.Right.push_back("Bar 3 starts like the [[hook]], so it's heard again.");
	if (SameNotes(Bar2, Bar4) && SameNotes(Bar1, Bar3)) {
		V.Next = "Bars 3-4 repeat the hook note for note. Now give it a different ending: change the last note or two of bar 4. "
			"Ending on " + NoteWord(Tonic(F), K) + ", the [[home note]], makes it sound like an answer.";
		PlayBars(V, F, 1, 4, F.Target);
		return;
	}
	V.Passed = true;
	if (!Bar4.empty())
		V.Right.push_back(Format("Bar 4 ends differently (on %s), so the repeat goes somewhere new.",
			NoteWord(Bar4.back().Pitch, K).c_str()));
	PlayBars(V, F, 1, 4);
}

// The song's shape: named parts laid out
void CheckSongShape(const ExerciseFacts &F, ExerciseVerdict &V)
{
	if (F.Sections.empty()) {
		V.Next = "Your song has no named parts yet. Open Module > Song shape (or right-click the frame list), pick a shape, like a "
			"lively stage or a calm night, and press OK: it lays out the parts, each a [[section|sections]] named for what it does.";
		return;
	}
	if (F.Shape.empty() && F.Sections.size() < 2) {
		V.Right.push_back("Your song has a part named " + F.Sections.front().Name + ".");
		V.Next = "A whole song has several parts. Module > Song shape lays them all out at once: pick a shape there and press OK.";
		return;
	}
	V.Passed = true;
	V.Right.push_back(F.Shape.empty() ? Format("Your song has %s:", Plural(static_cast<int>(F.Sections.size()), "part", "parts").c_str()) :
		Format("Your song is laid out as a %s, in %s:", Lowercase(F.Shape).c_str(),
		Plural(static_cast<int>(F.Sections.size()), "part", "parts").c_str()));
	for (size_t i = 0; i < F.Sections.size() && i < 10; ++i) {
		const ExerciseSection &S = F.Sections[i];
		const std::string Where = S.FirstBar == S.LastBar ? Format("bar %d", S.FirstBar) : Format("bars %d-%d", S.FirstBar, S.LastBar);
		V.Right.push_back(S.Line.empty() ? S.Name + " (" + Where + ")." : S.Name + " (" + Where + "): " + S.Line);
	}
}

// A first small song: chords or a bass, a tune, and a loop
const int SONG_TUNE_NOTES = 8;		// a tune's notes at the least
const int SONG_PART_NOTES = 4;		// another part's

void CheckFirstSong(const ExerciseFacts &F, ExerciseVerdict &V)
{
	int Melody = F.Melody, Notes = 0;
	std::vector<std::string> Others;
	for (size_t c = 0; c < F.Channels.size(); ++c) {
		const int Count = static_cast<int>(Sounding(&F.Channels[c]).size());
		Notes += Count;
		const int Kind = F.Channels[c].Kind;
		if (static_cast<int>(c) != Melody && Kind != KIND_NOISE && Kind != KIND_DPCM && Count >= SONG_PART_NOTES)
			Others.push_back(F.Channels[c].Name);
	}
	const int TuneNotes = Melody >= 0 && Melody < static_cast<int>(F.Channels.size()) ?
		static_cast<int>(Sounding(&F.Channels[static_cast<size_t>(Melody)]).size()) : 0;
	if (Notes == 0) {
		V.Next = "Your song is empty so far. Open View > What next and press Do it: each time, it makes the next step (the song's "
			"shape, its key, its chords and bass, a tune...) and shows the one after.";
		return;
	}
	if (!F.Sections.empty())
		V.Right.push_back(Format("Your song has its parts laid out (%s).", Plural(static_cast<int>(F.Sections.size()), "part",
			"parts").c_str()));
	std::string OthersText;
	for (size_t i = 0; i < Others.size(); ++i)
		OthersText += (i == 0 ? "" : i + 1 == Others.size() ? " and " : ", ") + Others[i];
	if (Others.empty()) {
		V.Next = "Next come the [[chords|chord]] and the [[bass line]]: in the What next panel, press Do it on that step.";
		return;
	}
	V.Right.push_back("It has [[chords|chord]] and a [[bass line]] (" + OthersText + ").");
	if (TuneNotes < SONG_TUNE_NOTES) {
		V.Next = "Now a tune: when the What next panel gets to the [[melody]], press Grow one and pick the version you like, "
			"or Draw it to write your own in the piano roll.";
		return;
	}
	V.Right.push_back(Format("It has a tune (%s).", F.Channels[static_cast<size_t>(Melody)].Name.c_str()));
	if (F.Halts) {
		V.Next = "Your song stops at its end. A game's music goes round: let it [[loop]] (the What next panel's loop step, or take "
			"the halt (Cxx) out of the last row).";
		return;
	}
	V.Passed = true;
	V.Right.push_back("It goes round when it ends: a [[loop]], ready for a game.");
	PlayBars(V, F, 1, std::min(8, SongBars(F)));
}

}	// namespace

bool CheckExercise(const std::string &Check, const ExerciseFacts &Facts, ExerciseVerdict &Out)
{
	const ExerciseCheckInfo *pCheck = FindExerciseCheck(Check);
	if (pCheck == 0)
		return false;
	Out = ExerciseVerdict();
	Out.Check = pCheck->Id;
	if (Check == "home-note")
		CheckHomeNote(Facts, Out);
	else if (Check == "four-bars")
		CheckFourBars(Facts, Out);
	else if (Check == "strong-notes")
		CheckStrongNotes(Facts, Out);
	else if (Check == "phrases")
		CheckPhrases(Facts, Out);
	else if (Check == "bass-roots")
		CheckBassRoots(Facts, Out);
	else if (Check == "bass-two")
		CheckBassTwo(Facts, Out);
	else if (Check == "beat")
		CheckBeat(Facts, Out);
	else if (Check == "minor")
		CheckMinor(Facts, Out);
	else if (Check == "hook")
		CheckHook(Facts, Out);
	else if (Check == "song-shape")
		CheckSongShape(Facts, Out);
	else if (Check == "first-song")
		CheckFirstSong(Facts, Out);
	if (Out.Passed) {
		Out.Next.clear();
		Out.Learned = pCheck->Learned;
		const ExerciseInfo *pExercise = ExerciseOfCheck(Check);
		if (pExercise != 0 && !pExercise->Checks.empty() && pExercise->Checks.back() == Check) {
			const ExerciseInfo *pAfter = ExerciseAfter(pExercise->Id);
			if (pAfter != 0)
				Out.NextExercise = pAfter->Id;
		}
	}
	return true;
}

// The verdict as JSON and text

Json ExerciseVerdictJson(const ExerciseVerdict &V)
{
	Json Out = Json::MakeObject();
	Out.Set("check", V.Check);
	const ExerciseInfo *pExercise = ExerciseOfCheck(V.Check);
	if (pExercise != 0) {
		Out.Set("exercise", pExercise->Id);
		Out.Set("title", pExercise->Title);
	}
	Out.Set("passed", V.Passed);
	Json Right = Json::MakeArray();
	for (size_t i = 0; i < V.Right.size(); ++i)
		Right.Push(V.Right[i]);
	Out.Set("right", Right);
	if (!V.Next.empty())
		Out.Set("next", V.Next);
	if (!V.Learned.empty())
		Out.Set("learned", V.Learned);
	if (V.PlayFirst > 0) {
		Json Play = Json::MakeObject();
		Play.Set("first_bar", V.PlayFirst);
		Play.Set("last_bar", V.PlayLast);
		Json Channels = Json::MakeArray();
		for (size_t i = 0; i < V.PlayChannels.size(); ++i)
			Channels.Push(V.PlayChannels[i]);
		Play.Set("channels", Channels);
		Out.Set("play", Play);
	}
	if (!V.NextExercise.empty())
		Out.Set("next_exercise", V.NextExercise);
	return Out;
}

bool ReadExerciseVerdict(const std::string &Text, ExerciseVerdict &Out, std::string &Error)
{
	Out = ExerciseVerdict();
	Json Value;
	if (!ParseJson(Text, Value, &Error) || !Value.IsObject()) {
		if (Error.empty())
			Error = "The check's answer isn't what it should be.";
		return false;
	}
	const Json *pCheck = Value.Get("check"), *pPassed = Value.Get("passed");
	if (pCheck == 0 || !pCheck->IsString() || pPassed == 0 || !pPassed->IsBool()) {
		Error = "The check's answer says neither which check it is nor whether it passed.";
		return false;
	}
	Out.Check = pCheck->String();
	Out.Passed = pPassed->Bool();
	const Json *pRight = Value.Get("right");
	for (size_t i = 0; pRight != 0 && pRight->IsArray() && i < pRight->Size(); ++i)
		if (pRight->At(i).IsString())
			Out.Right.push_back(pRight->At(i).String());
	const Json *pNext = Value.Get("next"), *pLearned = Value.Get("learned"), *pAfter = Value.Get("next_exercise");
	if (pNext != 0 && pNext->IsString())
		Out.Next = pNext->String();
	if (pLearned != 0 && pLearned->IsString())
		Out.Learned = pLearned->String();
	if (pAfter != 0 && pAfter->IsString())
		Out.NextExercise = pAfter->String();
	const Json *pPlay = Value.Get("play");
	if (pPlay != 0 && pPlay->IsObject()) {
		const Json *pFirst = pPlay->Get("first_bar"), *pLast = pPlay->Get("last_bar"), *pChannels = pPlay->Get("channels");
		if (pFirst != 0 && pFirst->IsInteger() && pLast != 0 && pLast->IsInteger() && pFirst->Integer() >= 1 &&
			pLast->Integer() >= pFirst->Integer()) {
			Out.PlayFirst = static_cast<int>(pFirst->Integer());
			Out.PlayLast = static_cast<int>(pLast->Integer());
			for (size_t i = 0; pChannels != 0 && pChannels->IsArray() && i < pChannels->Size(); ++i)
				if (pChannels->At(i).IsInteger() && pChannels->At(i).Integer() >= 0)
					Out.PlayChannels.push_back(static_cast<int>(pChannels->At(i).Integer()));
		}
	}
	return true;
}

std::string ExerciseVerdictText(const ExerciseVerdict &V)
{
	const ExerciseInfo *pExercise = ExerciseOfCheck(V.Check);
	const std::string Title = pExercise != 0 ? pExercise->Title : V.Check;
	std::string Text = V.Passed ? "Passed: " + Title + "." : "Not yet: " + Title + ".";
	if (!V.Right.empty()) {
		Text += "\nWhat's right:";
		for (size_t i = 0; i < V.Right.size(); ++i)
			Text += " " + UnmarkLinks(V.Right[i]);
	}
	if (!V.Next.empty())
		Text += "\nWhat to change next: " + UnmarkLinks(V.Next);
	if (V.PlayFirst > 0)
		Text += V.PlayFirst == V.PlayLast ? Format("\nBar %d shows it best: play it for the user.", V.PlayFirst) :
			Format("\nBars %d-%d show it best: play them for the user.", V.PlayFirst, V.PlayLast);
	if (!V.Learned.empty())
		Text += "\nWhat it taught: " + UnmarkLinks(V.Learned);
	if (!V.NextExercise.empty()) {
		const ExerciseInfo *pAfter = FindExercise(V.NextExercise);
		Text += "\nThe next exercise: " + V.NextExercise + (pAfter != 0 ? std::string(" (") + pAfter->Title + ")" : std::string()) +
			", from Help > Exercises.";
	}
	return Text;
}

}	// namespace ftapi
