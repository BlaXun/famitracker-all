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

// Tests for what chords do in a key (ChordRoles.h): the common progressions
// in major and minor, the other modes the BF KEYS block keeps, borrowed and
// leading chords, slash chords, the chords right before and after, the words
// for every chord in every mode, and the chord tones under a row that the
// piano roll lights.

#include "../ChordRoles.h"
#include "../Chords.h"
#include "../Music.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace ftapi;

namespace {

int g_Checks = 0, g_Failures = 0;

void Check(bool Condition, const char *What, int Line)
{
	++g_Checks;
	if (!Condition) {
		++g_Failures;
		std::printf("FAIL line %d: %s\n", Line, What);
	}
}

#define CHECK(x) Check((x), #x, __LINE__)
#define CHECK_EQ(a, b) CheckEqual((a), (b), #a " == " #b, __LINE__)

void CheckEqual(const std::string &Got, const std::string &Wanted, const char *What, int Line)
{
	Check(Got == Wanted, What, Line);
	if (Got != Wanted)
		std::printf("    got:    %s\n    wanted: %s\n", Got.c_str(), Wanted.c_str());
}

// Music.h's modes by their place
const int DORIAN = 1, PHRYGIAN = 2, LYDIAN = 3, MIXOLYDIAN = 4, LOCRIAN = 6, HARMONIC_MINOR = 7;

Key KeyOf(const char *Text)
{
	Key K;
	const bool Read = ParseKey(Text, K);
	Check(Read, Text, __LINE__);
	return K;
}

// A chord a bar (16 rows), as written ("C Am F G"), each bar touching the next
std::vector<ChordAt> Progression(const std::string &Text, const Key &K)
{
	std::vector<ChordAt> Out;
	const std::vector<std::string> Bars = SplitProgression(Text);
	for (size_t i = 0; i < Bars.size(); ++i) {
		ChordAt A;
		A.Row = static_cast<int>(i) * 16;
		A.Rows = 16;
		const bool Read = ParseChord(Bars[i], K, A.C);
		Check(Read, Bars[i].c_str(), __LINE__);
		Out.push_back(A);
	}
	return Out;
}

// "home | away | tension | home"
std::string Roles(const std::string &Text, const char *pKey)
{
	const Key K = KeyOf(pKey);
	const std::vector<ChordRoleInKey> Found = RolesOfChords(Progression(Text, K), K);
	std::string Out;
	for (size_t i = 0; i < Found.size(); ++i)
		Out += (i > 0 ? " | " : "") + std::string(ChordRoleName(Found[i].Role));
	return Out;
}

// "home: settled, at rest | away: open, moving off"
std::string Words(const std::string &Text, const char *pKey)
{
	const Key K = KeyOf(pKey);
	const std::vector<ChordRoleInKey> Found = RolesOfChords(Progression(Text, K), K);
	std::string Out;
	for (size_t i = 0; i < Found.size(); ++i)
		Out += (i > 0 ? " | " : "") + Found[i].Words();
	return Out;
}

ChordRoleInKey One(const char *pChord, const char *pKey)
{
	const Key K = KeyOf(pKey);
	Chord C;
	const bool Read = ParseChord(pChord, K, C);
	Check(Read, pChord, __LINE__);
	return RoleOfChord(C, K);
}

bool Ascii(const std::string &Text)
{
	for (size_t i = 0; i < Text.size(); ++i)
		if (static_cast<unsigned char>(Text[i]) < 32 || static_cast<unsigned char>(Text[i]) > 126)
			return false;
	return true;
}

void TestNames()
{
	CHECK_EQ(std::string(ChordRoleName(CHORD_ROLE_HOME)), std::string("home"));
	CHECK_EQ(std::string(ChordRoleName(CHORD_ROLE_AWAY)), std::string("away"));
	CHECK_EQ(std::string(ChordRoleName(CHORD_ROLE_TENSION)), std::string("tension"));
	CHECK_EQ(std::string(ChordRoleName(CHORD_ROLE_COLOR)), std::string("color"));
	CHECK_EQ(std::string(ChordRoleMeaning(CHORD_ROLE_HOME)), std::string("settled, at rest"));
	CHECK_EQ(std::string(ChordRoleMeaning(CHORD_ROLE_TENSION)), std::string("leans toward home"));
	for (int r = 0; r < CHORD_ROLE_COUNT; ++r) {
		CHECK(Ascii(ChordRoleName(static_cast<chord_role_t>(r))) && *ChordRoleName(static_cast<chord_role_t>(r)) != 0);
		CHECK(Ascii(ChordRoleMeaning(static_cast<chord_role_t>(r))) && *ChordRoleMeaning(static_cast<chord_role_t>(r)) != 0);
		CHECK(Ascii(ChordRoleExplained(static_cast<chord_role_t>(r))) && std::string(ChordRoleExplained(static_cast<chord_role_t>(r))).size() > 40);
	}
	CHECK(*ChordRoleName(CHORD_ROLE_COUNT) == 0 && *ChordRoleMeaning(static_cast<chord_role_t>(-1)) == 0);
}

// The common progressions in major and minor
void TestCommonProgressions()
{
	// I IV V I: home, away, tension and back home
	CHECK_EQ(Roles("C F G C", "C major"), std::string("home | away | tension | home"));
	CHECK_EQ(Words("C F G C", "C major"), std::string("home: settled, at rest | away: open, moving off | "
		"tension: leans toward home | back home: the tension settles"));
	// I vi IV V
	CHECK_EQ(Roles("C Am F G", "C major"), std::string("home | home | away | tension"));
	CHECK_EQ(Words("C Am F G", "C major"), std::string("home: settled, at rest | near home: at rest, a little sad | "
		"away: open, moving off | tension: leans toward home"));
	// ii V I, with the seventh leaning harder
	CHECK_EQ(Words("Dm G7 C", "C major"), std::string("away: on the move | tension: leans hard toward home | "
		"back home: the tension settles"));
	// vi IV I V: home comes after away, not after tension
	CHECK_EQ(Words("Am F C G", "C major"), std::string("near home: at rest, a little sad | away: open, moving off | "
		"home: settled, at rest | tension: leans toward home"));
	// The same chords in A minor do the same: the relative key's home and its twin trade places
	CHECK_EQ(Roles("Am F C G", "A minor"), std::string("home | away | home | tension"));
	// i iv V i
	CHECK_EQ(Roles("Am Dm E Am", "A minor"), std::string("home | away | tension | home"));
	CHECK_EQ(Words("Am Dm E7 Am", "A minor"), std::string("home: settled, at rest | away: heavy, wistful | "
		"tension: leans hard toward home | back home: the tension settles"));
	// i VI III VII
	CHECK_EQ(Words("Am F C G", "A minor"), std::string("home: settled, at rest | away: warm, wide | "
		"near home: at rest, a little brighter | tension: bold, leads back home"));
	// The other chords of the keys
	CHECK_EQ(Words("Em Bdim", "C major"), std::string("near home: a little unsure | tension: restless, wants home"));
	CHECK_EQ(Words("Bdim Em G#dim", "A minor"), std::string("away: uneasy, on the move | tension: softly leans toward home | "
		"tension: restless, wants home"));
	CHECK_EQ(Words("Am Caug Am", "A minor"), std::string("home: settled, at rest | tension: eerie, leans toward home | "
		"back home: the tension settles"));
	// Sevenths and suspensions do what their chords do; one on the fifth waits
	CHECK_EQ(Words("Cmaj7 Fmaj7 Am7 Bm7b5", "C major"), std::string("home: settled, at rest | away: open, moving off | "
		"near home: at rest, a little sad | tension: restless, wants home"));
	CHECK_EQ(Words("Csus2 Gsus4 G7sus4 C5", "C major"), std::string("home: settled, at rest | tension: waiting, leans toward home | "
		"tension: waiting, leans toward home | back home: the tension settles"));
	// Back home only after tension, and only where the chords touch
	CHECK_EQ(Words("C F C", "C major"), std::string("home: settled, at rest | away: open, moving off | home: settled, at rest"));
	{
		const Key K = KeyOf("C major");
		std::vector<ChordAt> Apart = Progression("G C", K);
		Apart[1].Row = 32;
		const std::vector<ChordRoleInKey> Found = RolesOfChords(Apart, K);
		CHECK(Found.size() == 2u && Found[1].Words() == "home: settled, at rest");
	}
	// A surprise after home's own tension: V vi in major, V VI in minor
	CHECK_EQ(Words("C G Am", "C major"), std::string("home: settled, at rest | tension: leans toward home | "
		"near home: a gentle surprise after the tension"));
	CHECK_EQ(Words("Am E F", "A minor"), std::string("home: settled, at rest | tension: leans toward home | "
		"away: a surprise turn after the tension"));
	// ...but not after a tension of its own: VII III in minor is home's twin, as plain
	CHECK_EQ(Words("G C", "A minor"), std::string("tension: bold, leads back home | near home: at rest, a little brighter"));
}

// Borrowed chords, chords leading to another, and surprises
void TestColors()
{
	// From the minor key on the same home note, in a major key
	CHECK_EQ(Words("C Fm C", "C major"), std::string("home: settled, at rest | borrowed from minor: bittersweet | "
		"home: settled, at rest"));
	CHECK_EQ(Words("C Ab Bb C", "C major"), std::string("home: settled, at rest | borrowed from minor: big and dramatic | "
		"borrowed from minor: bold, heroic | home: settled, at rest"));
	CHECK(One("Eb", "C major").Words() == "borrowed from minor: dark and strong");
	CHECK(One("Cm", "C major").Words() == "borrowed from minor: home turned dark");
	CHECK(One("Gm", "C major").Words() == "borrowed from minor: a softer, darker tension");
	CHECK(One("Ddim", "C major").Words() == "borrowed from minor: a darker shade of away");
	CHECK(One("Dm7b5", "C major").Words() == "borrowed from minor: a darker shade of away");
	CHECK(One("Bb7", "C major").Words() == "borrowed from minor: bold, heroic");
	CHECK(One("Fm", "C major").Role == CHORD_ROLE_COLOR && One("Fm", "C major").Target == -1);
	CHECK(One("Fm7", "C major").Words() == "borrowed from minor: bittersweet");
	CHECK(One("Fm6", "C major").Words() == "borrowed from minor: bittersweet");
	CHECK(One("Bbadd9", "C major").Words() == "borrowed from minor: bold, heroic");
	// A major seventh softens them; and a chord borrowed with the key's own seventh added is borrowed all the same
	CHECK(One("Abmaj7", "C major").Words() == "borrowed from minor: dreamy, wistful");
	CHECK(One("Bbmaj7", "C major").Words() == "borrowed from minor: dreamy, wistful");
	CHECK(One("Dbmaj7", "Eb major").Words() == "borrowed from minor: dreamy, wistful");
	CHECK(One("Amaj7", "A minor").Words() == "borrowed from major: home made bright");
	CHECK(One("Fm(maj7)", "C major").Role == CHORD_ROLE_COLOR);
	// From the major key, in a minor key
	CHECK(One("A", "A minor").Words() == "borrowed from major: home made bright");
	CHECK(One("D", "A minor").Words() == "borrowed from major: bright, hopeful");
	CHECK(One("Bm", "A minor").Words() == "borrowed from major: a brighter shade of away");
	CHECK(One("F#m", "A minor").Words() == "borrowed from major: a brighter color");
	// The tension chord of another of the key's chords: it leads there
	CHECK_EQ(Words("C D7 G C", "C major"), std::string("home: settled, at rest | leads to G: a push on the way | "
		"tension: leans toward home | back home: the tension settles"));
	{
		const ChordRoleInKey D = One("D", "C major");
		CHECK(D.Role == CHORD_ROLE_COLOR && D.Target == 7 && D.Label == "leads to G");
	}
	CHECK(One("A7", "C major").Words() == "leads to Dm: a push on the way");
	CHECK(One("E", "C major").Words() == "leads to Am: a push on the way");
	CHECK(One("B7", "C major").Words() == "leads to Em: a push on the way");
	CHECK(One("C7", "C major").Words() == "leads to F: a push on the way");
	CHECK(One("F#dim", "C major").Words() == "leads to G: restless, a push on the way");
	CHECK(One("G#dim", "C major").Words() == "leads to Am: restless, a push on the way");
	CHECK(One("C#dim7", "C major").Words() == "leads to Dm: restless, a push on the way");
	CHECK(One("B7", "A minor").Words() == "leads to E: a push on the way");
	CHECK(One("C7", "A minor").Words() == "leads to F: a push on the way");
	CHECK(One("Eb", "Bb major").Role == CHORD_ROLE_AWAY);	// spelled in the key, found the same
	CHECK(One("C7", "F major").Words() == "tension: leans hard toward home");
	// A minor chord borrowed from major, or leading to the chord after it: the one after decides
	CHECK_EQ(Words("Am A Am", "A minor"), std::string("home: settled, at rest | borrowed from major: home made bright | "
		"home: settled, at rest"));
	CHECK_EQ(Words("Am A Dm", "A minor"), std::string("home: settled, at rest | leads to Dm: a push on the way | "
		"away: heavy, wistful"));
	CHECK_EQ(Words("D G", "A minor"), std::string("leads to G: a push on the way | tension: bold, leads back home"));
	CHECK_EQ(Words("D Am", "A minor"), std::string("borrowed from major: bright, hopeful | home: settled, at rest"));
	// A semitone over home, and the rest
	CHECK(One("Db", "C major").Words() == "color: dark and dramatic");
	CHECK(One("Bb", "A minor").Words() == "color: dark and dramatic");
	CHECK(One("F#", "C major").Words() == "color: a surprise from outside the key");
	CHECK(One("F#", "C major").Role == CHORD_ROLE_COLOR);
	// Home's own tension from outside the key is tension all the same
	CHECK(One("E7", "C minor").Words() == "color: a surprise from outside the key");	// (C minor has no chord on A to lead to)
	CHECK(One("G7", "C minor").Words() == "tension: leans hard toward home");
	CHECK(One("Bdim7", "C major").Words() == "tension: restless, wants home");
}

// The other modes of the BF KEYS block
void TestModes()
{
	// Dorian: minor with a bright IV
	CHECK_EQ(Words("Dm G Dm", "D dorian"), std::string("home: settled, at rest | away: bright, hopeful | home: settled, at rest"));
	CHECK_EQ(Words("Em F Am C Dm", "D dorian"), std::string("away: on the move | near home: at rest, a little brighter | "
		"tension: softly leans toward home | tension: bold, leads back home | back home: the tension settles"));
	CHECK(One("Bdim", "D dorian").Words() == "away: uneasy");
	CHECK(One("A7", "D dorian").Words() == "tension: leans hard toward home");
	CHECK(One("Bb", "D dorian").Words() == "borrowed from minor: a darker color");
	CHECK(One("D", "D dorian").Words() == "borrowed from major: home made bright");
	// Mixolydian: major with a bold bVII
	CHECK_EQ(Words("G F C G", "G mixolydian"), std::string("home: settled, at rest | tension: bold, leads back home | "
		"away: open, moving off | home: settled, at rest"));
	CHECK_EQ(Words("G F G", "G mixolydian"), std::string("home: settled, at rest | tension: bold, leads back home | "
		"back home: the tension settles"));
	CHECK(One("Dm", "G mixolydian").Words() == "tension: softly leans toward home");
	CHECK(One("Bdim", "G mixolydian").Words() == "tension: restless, wants home");
	CHECK(One("Em", "G mixolydian").Words() == "near home: at rest, a little sad");
	CHECK(One("D", "G mixolydian").Role == CHORD_ROLE_TENSION);
	CHECK(One("F#dim", "G mixolydian").Words() == "tension: restless, wants home");
	// Phrygian: a dark II that slides down home
	CHECK_EQ(Words("Em F Em", "E phrygian"), std::string("home: settled, at rest | tension: dark, slides down to home | "
		"back home: the tension settles"));
	CHECK_EQ(Words("Am G F E", "E phrygian"), std::string("away: heavy, wistful | near home: at rest, a little brighter | "
		"tension: dark, slides down to home | borrowed from major: home made bright"));
	CHECK(One("Bdim", "E phrygian").Words() == "tension: restless, wants home");
	CHECK(One("C", "E phrygian").Words() == "away: warm, wide");
	CHECK(One("Dm", "E phrygian").Words() == "tension: softly leans toward home");
	// Lydian: a dreamy II
	CHECK_EQ(Words("C D C", "C lydian"), std::string("home: settled, at rest | away: dreamy, floating | home: settled, at rest"));
	CHECK(One("F#dim", "C lydian").Words() == "away: uneasy");
	CHECK(One("Bm", "C lydian").Words() == "tension: softly leans toward home");
	CHECK(One("F", "C lydian").Words() == "borrowed from major: a darker color");
	CHECK(One("Bb", "C mixolydian").Role == CHORD_ROLE_TENSION);
	CHECK(One("B", "C mixolydian").Role == CHORD_ROLE_COLOR);	// (Em is no major or minor chord of C mixolydian's)
	// A chord leading to the one after it names that one: D to G (not the key's Gm) in C mixolydian, D7 to G7/B
	CHECK(One("D", "C mixolydian").Words() == "leads to Gm: a push on the way");
	CHECK_EQ(Words("D G", "C mixolydian"), std::string("leads to G: a push on the way | tension: leans toward home"));
	CHECK_EQ(Words("D7 G7/B", "C major"), std::string("leads to G7: a push on the way | tension: leans hard toward home"));
	// Locrian: an uneasy home
	CHECK_EQ(Words("Bdim C Bdim", "B locrian"), std::string("home: at rest, but uneasy | tension: dark, slides down to home | "
		"back home: the tension settles"));
	CHECK(One("F", "B locrian").Words() == "tension: restless, wants home");
	CHECK(One("G", "B locrian").Words() == "away: warm, wide");
	CHECK(One("Dm", "B locrian").Words() == "near home: at rest, a little sad");
	// Harmonic minor takes natural minor's chords too, as minor takes its
	CHECK_EQ(Words("Am G Am", "A harmonic minor"), std::string("home: settled, at rest | tension: bold, leads back home | "
		"back home: the tension settles"));
	CHECK(One("E7", "A harmonic minor").Words() == "tension: leans hard toward home");
	CHECK(One("Em", "A harmonic minor").Words() == "tension: softly leans toward home");
	CHECK(One("Caug", "A harmonic minor").Words() == "tension: eerie, leans toward home");
	CHECK(One("C", "A harmonic minor").Words() == "near home: at rest, a little brighter");
	CHECK(One("G#dim7", "A harmonic minor").Words() == "tension: restless, wants home");

	// Every mode's own chords are the key's: home, away or tension, never color; and every chord in every mode has words
	const char *const KINDS[] = { "", "m", "dim", "aug", "sus2", "sus4", "5", "7", "maj7", "m7", "m7b5", "dim7", "add9", "6", "9" };
	for (int Mode = 0; Mode < ModeCount(); ++Mode) {
		for (int Tonic = 0; Tonic < 12; Tonic += 5) {
			const Key K(Tonic, Mode);
			for (int d = 0; d < 7; ++d) {
				const ChordRoleInKey Plain = RoleOfChord(DiatonicChord(K, d, false), K);
				const ChordRoleInKey Seventh = RoleOfChord(DiatonicChord(K, d, true), K);
				Check(Plain.Role != CHORD_ROLE_COLOR && Seventh.Role != CHORD_ROLE_COLOR, ModeName(Mode), __LINE__);
				Check((d == 0) == (Plain.Label == "home"), ModeName(Mode), __LINE__);
			}
			for (int Root = 0; Root < 12; ++Root) {
				for (size_t k = 0; k < sizeof(KINDS) / sizeof(KINDS[0]); ++k) {
					Chord C;
					const std::string Name = std::string(NoteClassName(Root, K)) + KINDS[k];
					if (!ParseChord(Name, K, C)) {
						Check(false, Name.c_str(), __LINE__);
						continue;
					}
					const Chord G = DiatonicChord(K, 4, false);
					for (int Context = 0; Context < 3; ++Context) {
						const ChordRoleInKey R = Context == 0 ? RoleOfChord(C, K) : Context == 1 ? RoleOfChord(C, K, &G, &G) :
							RoleOfChord(C, K, &C, &C);
						const bool Fine = R.Role >= 0 && R.Role < CHORD_ROLE_COUNT && !R.Label.empty() && !R.Feel.empty() &&
							Ascii(R.Words()) && (R.Target < 0) == (R.Label.compare(0, 8, "leads to") != 0) &&
							(R.Role == CHORD_ROLE_COLOR) == (R.Label == "color" || R.Label.compare(0, 8, "borrowed") == 0 ||
								R.Target >= 0);
						if (!Fine)
							std::printf("    %s in %s: %s\n", Name.c_str(), KeyName(K).c_str(), R.Words().c_str());
						Check(Fine, Name.c_str(), __LINE__);
					}
				}
			}
		}
	}
	// The minor modes' fifth: a major chord there is home's tension in each of them
	const int MINORISH[] = { DORIAN, PHRYGIAN, MODE_MINOR, HARMONIC_MINOR };
	for (size_t m = 0; m < sizeof(MINORISH) / sizeof(MINORISH[0]); ++m) {
		const Key K(9, MINORISH[m]);
		Chord E;
		CHECK(ParseChord("E", K, E) && RoleOfChord(E, K).Role == CHORD_ROLE_TENSION);
	}
	(void)LYDIAN;
	(void)MIXOLYDIAN;
	(void)LOCRIAN;
}

// A slash chord does what its chord does, but one moving off over the tension's bass note is a soft tension
void TestSlashChords()
{
	CHECK(One("C/E", "C major").Words() == "home: settled, at rest");
	CHECK(One("C/G", "C major").Role == CHORD_ROLE_HOME);
	CHECK(One("G/B", "C major").Words() == "tension: leans toward home");
	CHECK(One("F/A", "C major").Words() == "away: open, moving off");
	CHECK(One("Am/C", "C major").Role == CHORD_ROLE_HOME);
	CHECK(One("F/G", "C major").Words() == "tension: softly leans toward home");
	CHECK(One("Dm7/G", "C major").Words() == "tension: softly leans toward home");
	CHECK(One("Dm/E", "A minor").Words() == "tension: softly leans toward home");
	CHECK_EQ(Words("C F/G C", "C major"), std::string("home: settled, at rest | tension: softly leans toward home | "
		"back home: the tension settles"));
	CHECK(One("E/G#", "A minor").Role == CHORD_ROLE_TENSION);
	CHECK(One("D/F#", "C major").Words() == "leads to G: a push on the way");
	CHECK(One("Fm/Ab", "C major").Words() == "borrowed from minor: bittersweet");
	// A bass from outside the key counts as one of the chord's notes
	CHECK(One("C/Bb", "C major").Role == CHORD_ROLE_COLOR);
}

// The chord tones under a row, which the piano roll lights
void TestStrongNotes()
{
	const Key K = KeyOf("C major");
	std::vector<ChordAt> Chords = Progression("C F G", K);
	Chords[1].Rows = 8;			// F on rows 16-23...
	Chords[2].Row = 32;			// ...nothing on 24-31, G from 32
	CHECK(ChordIndexAtRow(Chords, 0) == 0 && ChordIndexAtRow(Chords, 15) == 0);
	CHECK(ChordIndexAtRow(Chords, 16) == 1 && ChordIndexAtRow(Chords, 23) == 1);
	CHECK(ChordIndexAtRow(Chords, 24) == -1 && ChordIndexAtRow(Chords, 31) == -1);
	CHECK(ChordIndexAtRow(Chords, 32) == 2 && ChordIndexAtRow(Chords, 47) == 2);
	CHECK(ChordIndexAtRow(Chords, 48) == -1 && ChordIndexAtRow(Chords, -1) == -1);
	CHECK(ChordIndexAtRow(std::vector<ChordAt>(), 0) == -1);
	// Root first: A minor's A is the strongest, C and E the others
	const std::vector<int> Am = { 9, 0, 4 };
	CHECK(StrongNoteAt(Am, 9, 57) == 2 && StrongNoteAt(Am, 9, 9) == 2 && StrongNoteAt(Am, 9, 93) == 2);
	CHECK(StrongNoteAt(Am, 9, 48) == 1 && StrongNoteAt(Am, 9, 52) == 1);
	CHECK(StrongNoteAt(Am, 9, 50) == 0 && StrongNoteAt(Am, 9, 55) == 0 && StrongNoteAt(Am, 9, 56) == 0);
	// A bass of its own (F/G) is lit with the chord's notes
	Chord FoverG;
	CHECK(ParseChord("F/G", K, FoverG));
	CHECK(StrongNoteAt(FoverG.PitchClasses(), FoverG.Bass, 55) == 1 && StrongNoteAt(FoverG.PitchClasses(), FoverG.Bass, 53) == 2);
	CHECK(StrongNoteAt(FoverG.PitchClasses(), -1, 55) == 0);
	CHECK(StrongNoteAt(std::vector<int>(), 0, 48) == 0);
}

}	// namespace

int main()
{
	std::setvbuf(stdout, 0, _IONBF, 0);	// every line out at once, so a test that dies still shows what it found
	TestNames();
	TestCommonProgressions();
	TestColors();
	TestModes();
	TestSlashChords();
	TestStrongNotes();
	std::printf("%d checks, %d failed\n", g_Checks, g_Failures);
	return g_Failures == 0 ? 0 : 1;
}
