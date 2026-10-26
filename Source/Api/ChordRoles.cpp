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

// What a chord does in a key (see ChordRoles.h)

#include "ChordRoles.h"
#include "Music.h"

namespace ftapi {

namespace {

// Music.h's modes by their place (MODE_MAJOR and MODE_MINOR are named there; dorian is 1 and phrygian 2)
const int LYDIAN = 3, MIXOLYDIAN = 4, LOCRIAN = 6, HARMONIC_MINOR = 7;

const char *const ROLE_NAMES[CHORD_ROLE_COUNT] = { "home", "away", "tension", "color" };
const char *const ROLE_MEANINGS[CHORD_ROLE_COUNT] = {
	"settled, at rest", "moving off", "leans toward home", "from outside the key"
};

int Wrap(int Value)
{
	return ((Value % 12) + 12) % 12;
}

bool MajorFamily(int Mode)
{
	return Mode == MODE_MAJOR || Mode == LYDIAN || Mode == MIXOLYDIAN;
}

// Minor and harmonic minor take each other's chords: the natural seventh's and the raised one's
bool BothSevenths(int Mode)
{
	return Mode == MODE_MINOR || Mode == HARMONIC_MINOR;
}

// The notes a mode's chords are made of, as bits of their semitones above the tonic
unsigned ScaleBits(int Mode)
{
	const int *pSteps = ModeSteps(Mode);
	unsigned Bits = 0;
	for (int d = 0; d < 7; ++d)
		Bits |= 1u << pSteps[d];
	if (BothSevenths(Mode))
		Bits |= (1u << 10) | (1u << 11);
	return Bits;
}

// The chord's notes and its bass, the same way
unsigned NoteBits(const Chord &C, int Tonic)
{
	unsigned Bits = 1u << Wrap(C.Bass - Tonic);
	for (size_t i = 0; i < C.Tones.size(); ++i)
		Bits |= 1u << Wrap(C.Root + C.Tones[i] - Tonic);
	return Bits;
}

bool Within(unsigned Bits, unsigned Scale)
{
	return (Bits & ~Scale) == 0;
}

enum quality_t { QUALITY_MAJOR, QUALITY_MINOR, QUALITY_DIMINISHED, QUALITY_AUGMENTED, QUALITY_SUSPENDED, QUALITY_BARE };

quality_t QualityOf(const Chord &C)
{
	const int Third = C.Third();
	if (Third == 4)
		return C.Fifth() == 8 ? QUALITY_AUGMENTED : QUALITY_MAJOR;
	if (Third == 3)
		return C.Fifth() == 6 ? QUALITY_DIMINISHED : QUALITY_MINOR;
	if (Third == 2 || Third == 5)
		return QUALITY_SUSPENDED;
	return QUALITY_BARE;	// a power chord, or a root alone
}

// The mode's own triad on Offset semitones over the tonic (minor and harmonic minor: natural minor's, with the raised
// seventh's diminished chord); QUALITY_BARE where the scale has no note
quality_t TriadOn(int Mode, int Offset)
{
	if (BothSevenths(Mode) && Offset == 11)
		return QUALITY_DIMINISHED;
	const int *pSteps = ModeSteps(BothSevenths(Mode) ? MODE_MINOR : Mode);
	for (int d = 0; d < 7; ++d) {
		if (pSteps[d] != Offset)
			continue;
		const int Third = Wrap(pSteps[(d + 2) % 7] - Offset), Fifth = Wrap(pSteps[(d + 4) % 7] - Offset);
		if (Third == 4)
			return Fifth == 8 ? QUALITY_AUGMENTED : QUALITY_MAJOR;
		return Fifth == 6 ? QUALITY_DIMINISHED : QUALITY_MINOR;
	}
	return QUALITY_BARE;
}

ChordRoleInKey Made(chord_role_t Role, const std::string &Label, const char *pFeel, int Target = -1)
{
	ChordRoleInKey Out;
	Out.Role = Role;
	Out.Label = Label;
	Out.Feel = pFeel;
	Out.Target = Target;
	return Out;
}

// The tension chord of home, on the fifth: a suspended one waits, a seventh leans harder
ChordRoleInKey FifthTension(quality_t Quality, bool Seventh)
{
	switch (Quality) {
	case QUALITY_SUSPENDED: return Made(CHORD_ROLE_TENSION, "tension", "waiting, leans toward home");
	case QUALITY_MINOR: return Made(CHORD_ROLE_TENSION, "tension", "softly leans toward home");
	case QUALITY_DIMINISHED: return Made(CHORD_ROLE_TENSION, "tension", "restless, wants home");
	case QUALITY_AUGMENTED: return Made(CHORD_ROLE_TENSION, "tension", "eager, leans toward home");
	default: return Made(CHORD_ROLE_TENSION, "tension", Seventh ? "leans hard toward home" : "leans toward home");
	}
}

// A chord of the key's own notes, by where its root is over the tonic. Every mode's chords are here: major's I ii iii
// IV V vi vii, lydian's II and #iv, mixolydian's iii and bVII, dorian's IV and vi, phrygian's and locrian's II, minor's
// both sevenths (v and V, VII and vii, III and III+), locrian's V on the lowered fifth
ChordRoleInKey InKey(int Mode, int Offset, quality_t Quality, bool Seventh)
{
	const bool Major = Quality == QUALITY_MAJOR, Minor = Quality == QUALITY_MINOR, Diminished = Quality == QUALITY_DIMINISHED;
	switch (Offset) {
	case 0:
		return Made(CHORD_ROLE_HOME, "home", Mode == LOCRIAN ? "at rest, but uneasy" : "settled, at rest");
	case 1:		// phrygian's and locrian's II, a semitone over home
		return Made(CHORD_ROLE_TENSION, "tension", "dark, slides down to home");
	case 2:
		if (Major)	// lydian's II
			return Made(CHORD_ROLE_AWAY, "away", "dreamy, floating");
		return Made(CHORD_ROLE_AWAY, "away", Diminished ? "uneasy, on the move" : "on the move");
	case 3:
		if (Quality == QUALITY_AUGMENTED)	// minor's III+, with the raised seventh
			return Made(CHORD_ROLE_TENSION, "tension", "eerie, leans toward home");
		return Made(CHORD_ROLE_HOME, "near home", Minor ? "at rest, a little sad" : "at rest, a little brighter");
	case 4:
		if (Diminished)		// mixolydian's iii
			return Made(CHORD_ROLE_TENSION, "tension", "restless, wants home");
		return Made(CHORD_ROLE_HOME, "near home", "a little unsure");
	case 5:
		if (Minor || Diminished)
			return Made(CHORD_ROLE_AWAY, "away", "heavy, wistful");
		return Made(CHORD_ROLE_AWAY, "away", MajorFamily(Mode) ? "open, moving off" : "bright, hopeful");	// dorian's IV
	case 6:
		if (Major)		// locrian's V, on the lowered fifth
			return Made(CHORD_ROLE_TENSION, "tension", "restless, wants home");
		return Made(CHORD_ROLE_AWAY, "away", "uneasy");	// lydian's #iv
	case 7:
		return FifthTension(Quality, Seventh);
	case 8:
		return Made(CHORD_ROLE_AWAY, "away", "warm, wide");
	case 9:
		if (Diminished)		// dorian's vi
			return Made(CHORD_ROLE_AWAY, "away", "uneasy");
		return Made(CHORD_ROLE_HOME, "near home", "at rest, a little sad");
	case 10:
		if (Minor || Diminished)	// phrygian's and locrian's vii
			return Made(CHORD_ROLE_TENSION, "tension", "softly leans toward home");
		return Made(CHORD_ROLE_TENSION, "tension", "bold, leads back home");
	default:
		if (Minor)		// lydian's vii
			return Made(CHORD_ROLE_TENSION, "tension", "softly leans toward home");
		return Made(CHORD_ROLE_TENSION, "tension", "restless, wants home");
	}
}

// Where a chord leading to another goes: the root a fifth under a major or augmented chord's (D7 to G), a semitone
// over a diminished one's (C#dim to Dm), when the key has a major or minor chord there other than home; -1 otherwise
int TargetOf(const Chord &C, quality_t Quality, const Key &K, int Mode)
{
	int Target = -1;
	if (Quality == QUALITY_MAJOR || Quality == QUALITY_AUGMENTED)
		Target = Wrap(C.Root + 5);
	else if (Quality == QUALITY_DIMINISHED)
		Target = Wrap(C.Root + 1);
	if (Target < 0)
		return -1;
	const int Offset = Wrap(Target - K.Tonic);
	const quality_t There = TriadOn(Mode, Offset);
	return Offset != 0 && (There == QUALITY_MAJOR || There == QUALITY_MINOR) ? Target : -1;
}

// The major or minor key on the same home note whose notes a chord takes. A mode of the major family looks to major
// first, the minor family to minor.
enum borrowed_t { BORROWED_NONE, BORROWED_MINOR, BORROWED_MAJOR };

borrowed_t BorrowedFrom(unsigned Bits, int Mode)
{
	const unsigned Minor = ScaleBits(MODE_MINOR), Major = ScaleBits(MODE_MAJOR);
	if (MajorFamily(Mode)) {
		if (Mode != MODE_MAJOR && Within(Bits, Major))
			return BORROWED_MAJOR;
		return Within(Bits, Minor) ? BORROWED_MINOR : BORROWED_NONE;
	}
	if (!BothSevenths(Mode) && Within(Bits, Minor))
		return BORROWED_MINOR;
	return Within(Bits, Major) ? BORROWED_MAJOR : BORROWED_NONE;
}

// Darker or brighter than the key: the chord's notes against the key's at the same place in the scale they come from
const char *Shade(const Chord &C, const Key &K, int Mode, borrowed_t From)
{
	const bool FromMinor = From == BORROWED_MINOR;
	const int *pKey = ModeSteps(Mode), *pSource = ModeSteps(FromMinor ? MODE_MINOR : MODE_MAJOR);
	int Darker = 0, Brighter = 0;
	const std::vector<int> Classes = C.PitchClasses();
	for (size_t i = 0; i < Classes.size(); ++i) {
		const int Offset = Wrap(Classes[i] - K.Tonic);
		for (int d = 0; d < 7; ++d)
			if (pSource[d] == Offset) {
				if (Offset < pKey[d])
					++Darker;
				else if (Offset > pKey[d])
					++Brighter;
			}
	}
	if (Darker != Brighter)
		return Darker > Brighter ? "a darker color" : "a brighter color";
	return FromMinor ? "a darker color" : "a brighter color";
}

// How the best-known borrowed chords feel, else darker or brighter. A major seventh softens the big ones (Abmaj7 and
// Bbmaj7 in C major, as calm songs have them).
const char *BorrowedFeel(const Chord &C, const Key &K, int Mode, int Offset, quality_t Quality, borrowed_t From)
{
	const bool FromMinor = From == BORROWED_MINOR;
	if (FromMinor && Mode == MODE_MAJOR) {
		if (Quality == QUALITY_MAJOR && C.Has(11) && (Offset == 3 || Offset == 8 || Offset == 10))
			return "dreamy, wistful";
		switch (Offset) {
		case 0: if (Quality == QUALITY_MINOR) return "home turned dark"; break;
		case 2: if (Quality == QUALITY_DIMINISHED) return "a darker shade of away"; break;
		case 3: if (Quality == QUALITY_MAJOR) return "dark and strong"; break;
		case 5: if (Quality == QUALITY_MINOR) return "bittersweet"; break;
		case 7: if (Quality == QUALITY_MINOR) return "a softer, darker tension"; break;
		case 8: if (Quality == QUALITY_MAJOR) return "big and dramatic"; break;
		case 10: if (Quality == QUALITY_MAJOR) return "bold, heroic"; break;
		}
	}
	else if (!FromMinor && !MajorFamily(Mode)) {
		switch (Offset) {
		case 0: if (Quality == QUALITY_MAJOR) return "home made bright"; break;
		case 2: if (Quality == QUALITY_MINOR) return "a brighter shade of away"; break;
		case 5: if (Quality == QUALITY_MAJOR) return "bright, hopeful"; break;
		}
	}
	return Shade(C, K, Mode, From);
}

// A chord with notes from outside the key: home's own tension from outside the mode, a chord leading to another of the
// key's, borrowed from the major or minor key on the same note, or a color of its own
ChordRoleInKey OutsideKey(const Chord &C, const Key &K, int Mode, int Offset, quality_t Quality, const Chord *pAfter)
{
	const bool Seventh = Quality == QUALITY_MAJOR && C.Has(10);
	// Home's tension all the same: a major chord on the fifth (dorian's, mixolydian's, phrygian's), the leading tone's
	// diminished chord
	if (Offset == 7 && Quality != QUALITY_MINOR && Quality != QUALITY_DIMINISHED)
		return FifthTension(Quality, Seventh);
	if (Offset == 11 && Quality == QUALITY_DIMINISHED)
		return Made(CHORD_ROLE_TENSION, "tension", "restless, wants home");
	const int Target = TargetOf(C, Quality, K, Mode);
	const borrowed_t From = BorrowedFrom(NoteBits(C, K.Tonic), Mode);
	// A chord both borrowed and leading on (A or D in A minor) leads on where the next chord is the one it leads to; in
	// the major family the only such chords are leading tones' (G#dim before Am in C major), so they lead on
	const bool LeadsOn = Target >= 0 && (From == BORROWED_NONE || MajorFamily(Mode) || (pAfter != 0 && pAfter->Root == Target));
	if (LeadsOn) {
		// Named as the chord after it where that's the one, else as the key's chord there
		const int There = Wrap(Target - K.Tonic);
		const quality_t Kind = (!MajorFamily(Mode) && There == 7) ? QUALITY_MAJOR : TriadOn(Mode, There);	// minor's V
		Chord Goal(Target, std::vector<int>({ 0, Kind == QUALITY_MAJOR ? 4 : 3, 7 }));
		if (pAfter != 0 && pAfter->Root == Target) {
			Goal = *pAfter;
			Goal.Bass = Goal.Root;
			Goal.BassSpelling = Goal.RootSpelling;
		}
		return Made(CHORD_ROLE_COLOR, "leads to " + ChordName(Goal, K),
			Quality == QUALITY_DIMINISHED ? "restless, a push on the way" : "a push on the way", Target);
	}
	// Borrowed whole, or its chord borrowed with the key's own notes added (Dbmaj7 in Eb major: Db, F and Ab are Eb
	// minor's, C the key's)
	borrowed_t Whose = From;
	if (Whose == BORROWED_NONE) {
		Chord Plain(C.Root, std::vector<int>({ 0 }));
		if (C.Third() >= 0)
			Plain = Chord(C.Root, std::vector<int>({ 0, C.Third(), C.Fifth() >= 0 ? C.Fifth() : 0 }));
		else if (C.Fifth() >= 0)
			Plain = Chord(C.Root, std::vector<int>({ 0, C.Fifth() }));
		const unsigned Added = NoteBits(C, K.Tonic) & ~NoteBits(Plain, K.Tonic);
		if (Within(Added, ScaleBits(Mode)))
			Whose = BorrowedFrom(NoteBits(Plain, K.Tonic), Mode);
	}
	if (Whose != BORROWED_NONE)
		return Made(CHORD_ROLE_COLOR, Whose == BORROWED_MINOR ? "borrowed from minor" : "borrowed from major",
			BorrowedFeel(C, K, Mode, Offset, Quality, Whose));
	if (Offset == 1 && Quality == QUALITY_MAJOR)	// a semitone over home (the Neapolitan)
		return Made(CHORD_ROLE_COLOR, "color", "dark and dramatic");
	return Made(CHORD_ROLE_COLOR, "color", "a surprise from outside the key");
}

}	// namespace

const char *ChordRoleName(chord_role_t Role)
{
	return Role >= 0 && Role < CHORD_ROLE_COUNT ? ROLE_NAMES[Role] : "";
}

const char *ChordRoleMeaning(chord_role_t Role)
{
	return Role >= 0 && Role < CHORD_ROLE_COUNT ? ROLE_MEANINGS[Role] : "";
}

ChordRoleInKey RoleOfChord(const Chord &C, const Key &K, const Chord *pBefore, const Chord *pAfter)
{
	const int Mode = (K.Mode >= 0 && K.Mode < ModeCount()) ? K.Mode : MODE_MAJOR;
	const int Offset = Wrap(C.Root - K.Tonic), BassOffset = Wrap(C.Bass - K.Tonic);
	const quality_t Quality = QualityOf(C);
	// A chord moving off from home over the tension's bass note, which isn't one of its own (F/G, Dm7/G in C major)
	if (BassOffset == 7 && !C.HasPitchClass(C.Bass) && (Offset == 5 || Offset == 2))
		return Made(CHORD_ROLE_TENSION, "tension", "softly leans toward home");
	ChordRoleInKey Role;
	if (Within(NoteBits(C, K.Tonic), ScaleBits(Mode))) {
		// A suspended or bare chord does what the key's own chord there does (one on the fifth waits)
		const bool Plain = Quality == QUALITY_SUSPENDED || Quality == QUALITY_BARE;
		const quality_t Like = Plain && !(Quality == QUALITY_SUSPENDED && Offset == 7) ? TriadOn(Mode, Offset) : Quality;
		Role = InKey(Mode, Offset, Like, Like == QUALITY_MAJOR && C.Has(10));
	}
	else
		Role = OutsideKey(C, K, Mode, Offset, Quality, pAfter);
	// After tension: home again, or a surprise after home's own tension (V to vi, V to VI in minor)
	if (pBefore != 0) {
		const ChordRoleInKey Was = RoleOfChord(*pBefore, K);
		const int BeforeOffset = Wrap(pBefore->Root - K.Tonic);
		const bool Dominant = Was.Role == CHORD_ROLE_TENSION && (BeforeOffset == 7 || BeforeOffset == 11);
		if (Was.Role == CHORD_ROLE_TENSION && Role.Role == CHORD_ROLE_HOME && Offset == 0)
			Role = Made(CHORD_ROLE_HOME, "back home", "the tension settles");
		else if (Dominant && Role.Role == CHORD_ROLE_HOME && Offset != 0)
			Role = Made(CHORD_ROLE_HOME, "near home", "a gentle surprise after the tension");
		else if (Dominant && Role.Role == CHORD_ROLE_AWAY && Offset == 8)
			Role = Made(CHORD_ROLE_AWAY, "away", "a surprise turn after the tension");
	}
	return Role;
}

std::vector<ChordRoleInKey> RolesOfChords(const std::vector<ChordAt> &Chords, const Key &K)
{
	std::vector<ChordRoleInKey> Out;
	for (size_t i = 0; i < Chords.size(); ++i) {
		const ChordAt &This = Chords[i];
		const Chord *pBefore = (i > 0 && Chords[i - 1].Row + Chords[i - 1].Rows == This.Row) ? &Chords[i - 1].C : 0;
		const Chord *pAfter = (i + 1 < Chords.size() && This.Row + This.Rows == Chords[i + 1].Row) ? &Chords[i + 1].C : 0;
		Out.push_back(RoleOfChord(This.C, K, pBefore, pAfter));
	}
	return Out;
}

int StrongNoteAt(const std::vector<int> &PitchClasses, int Bass, int Pitch)
{
	if (PitchClasses.empty())
		return 0;
	const int Class = Wrap(Pitch);
	if (Wrap(PitchClasses[0]) == Class)
		return 2;
	for (size_t i = 1; i < PitchClasses.size(); ++i)
		if (Wrap(PitchClasses[i]) == Class)
			return 1;
	return Bass >= 0 && Wrap(Bass) == Class ? 1 : 0;
}

}	// namespace ftapi
