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

#include "Flavours.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ftapi {

namespace {

// The flavours: what each leans to among the kinds' pools (names of entries the pools have: design_instrument's lead kinds,
// add_chords_and_bass's chord and bass styles, add_drums's grooves), what it gives a pulse lead, the drums, the chords'
// colours and the ambience. Their words: on a tie, the first of the table (cold, hot, wet, mechanical, nature, spooky,
// airy, ancient, magical)
const FlavourInfo FLAVOURS[] = {
	{ FLAVOUR_NONE, "none", "", "", 0, "", 0, 0, 0, 0, 0, 0, 0, "", 0, false, false, 0, "", "", "" },
	{ FLAVOUR_COLD, "cold", "icy frozen wintry snowy chilly", "cold chilly ice icy frozen frost frosty freezing snow snowy snowfall "
		"winter wintry glacier glacial arctic polar tundra blizzard crystal crystals", 0,
		"ice and snow: thin, glassy, bell-like sounds, high and sparse, with a little echo",
		"music box|falling strike|keys", "broken|arpeggio", "half|pedal|roots", "calm|lively", -1,
		"12.5%", "none", "thin and glassy (12.5%), without a vibrato", "light", true, true, 0, "wind",
		"Cold (ice, snow): thin, glassy, bell-like sounds up high (a struck lead at 12.5% without a vibrato), sparse broken chords "
		"or arpeggios, a bass on half bars or a pedal, a little slower than the kind's own pace; once the tune is there the "
		"chords' added ninths sparkle like frost; an echo and a little Space (set_mixer); wind on the noise in a quiet place; "
		"light drums on the noise alone, ticks rather than thumps.",
		"the lead thin and glassy, the chords sparse and sparkling" },
	{ FLAVOUR_HOT, "hot", "heat fiery scorching", "hot heat lava magma volcano volcanic desert deserts dune dunes sand sandy forge "
		"forges furnace furnaces smithy blacksmith inferno scorching sweltering oasis mirage embers", 0,
		"heat, lava, a desert or a forge: warm, buzzing, dusty sounds with a slow vibrato",
		"brass|breath|bloom lead", "lush|stabs", "walking|root_fifth|octaves", "shuffle|basic", -1,
		"50%", "gentle", "warm and round (50%), with a gentle vibrato", 0, false, false, 0, "fire|wind",
		"Hot (lava, a desert, a forge): warm, buzzing sounds (a brass or a breath lead, round at 50% with a gentle, slow "
		"vibrato; the VRC6's sawtooth buzzes best for a part of its own), lush chords that shimmer like heat or stabs, a bass "
		"that walks, a lazy pace a little slower than the kind's own, a shuffle where drums play; fire or a dry wind on the "
		"noise in a quiet place.",
		"the lead warm and wavering, the chords shimmering, the pace unhurried" },
	{ FLAVOUR_WET, "wet", "damp watery soggy", "wet damp sewer sewers swamp swamps marsh marshes bog bogs mire rain rainy drizzle "
		"puddle puddles flooded flood drip drips dripping waterlogged soggy", 0,
		"water, rain or a sewer: round, dripping sounds and an echo",
		"falling strike|keys|music box", "broken|arpeggio", "root_fifth|half", "lively|calm", 0,
		"50%", 0, "round (50%), struck like a drip", 0, false, false, 0, "rain|stream",
		"Wet (rain, a sewer, a swamp): round sounds that drip (a struck lead at 50%), broken chords or arpeggios that trickle, a "
		"bass on root and fifth, an echo a beat behind that answers like water off the walls; rain or a stream on the noise in a "
		"quiet place; drums light and soft.",
		"the lead round and dripping, the echo answering, the water underneath" },
	{ FLAVOUR_MECHANICAL, "mechanical", "machine machinery industrial clockwork", "mechanical factory factories clockwork machine "
		"machines machinery gear gears cog cogs industrial foundry ironworks engine engines robot robots conveyor",
		" mine cart, mine carts, steam engine, power plant,",
		"machines, a factory or clockwork: rhythmic ostinatos, a steady pulse, clanks on the noise",
		"stepped lead|lead|keys", "stabs|arpeggio", "driving|octaves|root_fifth", "four_on_floor|driving|basic", 1,
		"25%", "none", "bright and exact (25%), without a vibrato", 0, true, false, 0, "",
		"Mechanical (a factory, clockwork, a mine cart): a steady pulse a little faster than the kind's own, a driving or octave "
		"bass and stabs or arpeggios that repeat like machinery (an ostinato), a square-ish lead at 25% without a vibrato, drums "
		"on the noise alone that clank: four on the floor or a driving beat; a noise instrument whose duty sequence is 1 plays "
		"the noise's short, metallic mode, a clank of its own.",
		"the ostinato ticking on, the bass driving, the clanks in time" },
	{ FLAVOUR_NATURE, "nature", "natural green outdoors", "nature forest forests woods woodland woodlands meadow meadows garden "
		"gardens grove groves glade glades jungle grassland grassy flowers blossom blossoms orchard countryside birds birdsong", 0,
		"forests and meadows: open fifths, gentle tunes, birds and water",
		"breath|lead|bloom lead", "broken", "root_fifth|half", "lively|calm", 0,
		0, "faint", "with a faint vibrato, like a voice outdoors", "light", false, false, 0, "stream|wind",
		"Nature (a forest, a meadow): open fifths in the bass (root and fifth), broken chords, a gentle tune that leans on the "
		"pentatonic notes (leave out the fourth and the seventh on the strong beats), a breath or a plain lead with a faint "
		"vibrato, birdsong in the rests (a quick high figure on a free pulse, or make_sound_effect birds for a cue); a stream "
		"or wind on the noise in a quiet place; light drums.",
		"the tune gentle and open, the fifths under it, the stream or the wind behind" },
	{ FLAVOUR_SPOOKY, "spooky", "haunted creepy eerie ghostly", "spooky haunted ghost ghosts ghostly graveyard graveyards cemetery "
		"cemeteries creepy phantom phantoms spectre specter witch witches vampire vampires zombie zombies skeleton skeletons ghoul "
		"ghouls", 0,
		"a haunted place: chromatic touches, tritones and minor seconds, a slow vibrato and a pad that wavers",
		"breath|stepped lead", "lush", "pedal|roots", "|calm", -1,
		"12.5%", "gentle", "thin (12.5%) with a wavering vibrato", "light", true, false, 1, "wind",
		"Spooky (haunted, a graveyard): chromatic touches (a borrowed chord: bII before the dominant in minor, the minor iv in "
		"major), a tritone or a minor second leaned on in the tune and resolved, a thin lead with a slow, wavering vibrato, slow "
		"lush chords that waver like a pad, a pedal bass, a little slower; wind on the noise in a quiet place; drums few and "
		"light, ticks on the noise alone.",
		"the borrowed chord's chill, the lead wavering, nothing too loud" },
	{ FLAVOUR_AIRY, "airy", "high windy breezy", "airy sky skies cloud clouds heights mountaintop mountaintops summit summits breeze "
		"breezy windy", 0,
		"the sky and the heights: light, high sounds, wide space and wind",
		"breath|bloom lead", "broken|lush", "half|pedal", "lively|calm|", 0,
		"25%", "faint", "light (25%) with a faint vibrato", "light", false, true, 0, "wind",
		"Airy (the sky, the heights): light, high sounds (a breath or bloom lead at 25% with a faint vibrato), broken or lush "
		"chords spread wide, the bass on half bars or a pedal, the chords' rich colours once the tune is there, an echo and Space "
		"for width; wind on the noise in a quiet place; drums light or none.",
		"the lead light and high, the chords wide, the wind behind" },
	{ FLAVOUR_ANCIENT, "ancient", "old ancestral archaic", "ancient temple temples pyramid pyramids shrine shrines relic relics "
		"ancestral primeval archaic sanctum ruin ruins", 0,
		"temples and ruins: modal chords, a drone and a solemn, slow tune",
		"brass|breath|stepped lead", "lush|broken", "pedal|roots", "basic|calm", -1,
		"50%", 0, "round and solemn (50%)", 0, false, false, 2, "wind",
		"Ancient (a temple, ruins): modal colour (bVII for a V inside a major progression, mixolydian; dorian or aeolian in "
		"minor), a drone under it (a pedal bass), a solemn tune of fourths and fifths, a round brass or breath lead, a little "
		"slower; wind on the noise in a quiet place; drums like a ritual's, sparse.",
		"the drone under the modal chords, the tune solemn" },
	{ FLAVOUR_MAGICAL, "magical", "magic enchanted arcane", "magic magical enchanted fairy fairies faerie wizard wizards sorcery "
		"sorcerer sorcerers spell spells arcane mage mages potion potions", 0,
		"magic: sparkling arpeggios, bells and rich chords",
		"music box|bloom lead|falling strike", "arpeggio|lush", "arpeggio|half", "lively|shuffle", 0,
		0, "gentle", "with a gentle vibrato", 0, false, true, 0, "",
		"Magical (enchanted, a wizard's tower): sparkling fast arpeggios and bell-like leads (a music box or a bloom lead with a "
		"gentle vibrato), the chords' rich colours (added ninths, major sevenths) once the tune is there, a bass that rolls in "
		"arpeggios, a glint of a high echo.",
		"the arpeggios sparkling, the chords rich, the lead ringing" },
};

const size_t FLAVOUR_ROWS = sizeof(FLAVOURS) / sizeof(FLAVOURS[0]);

std::vector<std::string> WordsIn(const std::string &Text)
{
	std::vector<std::string> Out;
	std::string Word;
	for (size_t i = 0; i <= Text.size(); ++i) {
		const unsigned char c = i < Text.size() ? static_cast<unsigned char>(Text[i]) : ' ';
		if (std::isalnum(c))
			Word += static_cast<char>(std::tolower(c));
		else if (!Word.empty()) {
			Out.push_back(Word);
			Word.clear();
		}
	}
	return Out;
}

std::vector<std::string> Entries(const char *Pool)
{
	std::vector<std::string> Out;
	if (Pool == 0)
		return Out;
	std::string Item;
	for (const char *p = Pool; ; ++p) {
		if (*p == '|' || *p == 0) {
			Out.push_back(Item);
			Item.clear();
			if (*p == 0)
				break;
		}
		else
			Item += *p;
	}
	return Out;
}

// A word said not to be: "not cold", "no snow", "without ice"
bool Negated(const std::vector<std::string> &Words, size_t i)
{
	return i > 0 && (Words[i - 1] == "not" || Words[i - 1] == "no" || Words[i - 1] == "without" || Words[i - 1] == "never");
}

std::string BpmText(double Bpm)
{
	char Text[32];
	std::snprintf(Text, sizeof(Text), "%.1f", Bpm);
	std::string Out = Text;
	if (Out.size() > 2 && Out.compare(Out.size() - 2, 2, ".0") == 0)
		Out.erase(Out.size() - 2);
	return Out;
}

}	// namespace

const FlavourInfo &FlavourOf(int Flavour)
{
	for (size_t i = 0; i < FLAVOUR_ROWS; ++i)
		if (FLAVOURS[i].Id == Flavour)
			return FLAVOURS[i];
	return FLAVOURS[0];
}

bool ParseFlavour(const std::string &Text, int &Flavour)
{
	const std::vector<std::string> Words = WordsIn(Text);
	if (Words.size() != 1)
		return false;
	for (size_t i = 0; i < FLAVOUR_ROWS; ++i) {
		const std::string Own = std::string(" ") + FLAVOURS[i].Name + " " + FLAVOURS[i].Aliases + " ";
		if (Own.find(" " + Words[0] + " ") != std::string::npos) {
			Flavour = FLAVOURS[i].Id;
			return true;
		}
	}
	if (Words[0] == "plain" || Words[0] == "neutral") {
		Flavour = FLAVOUR_NONE;
		return true;
	}
	return false;
}

std::string FlavourNames()
{
	std::string Out;
	for (int f = FLAVOUR_COLD; f < FLAVOUR_COUNT; ++f)
		Out += (f == FLAVOUR_COLD ? "" : f + 1 == FLAVOUR_COUNT ? " or " : ", ") + std::string(FlavourOf(f).Name);
	return Out;
}

FlavourWords ReadFlavourWords(const std::string &Text)
{
	FlavourWords Out;
	const std::vector<std::string> Words = WordsIn(Text);
	int Best = 0;
	for (size_t r = 0; r < FLAVOUR_ROWS; ++r) {
		const FlavourInfo &F = FLAVOURS[r];
		if (F.Id == FLAVOUR_NONE)
			continue;
		const std::vector<std::string> Own = WordsIn(F.Words);
		std::vector<std::string> Found;
		for (size_t i = 0; i < Words.size(); ++i)
			if (!Negated(Words, i) && std::find(Own.begin(), Own.end(), Words[i]) != Own.end() && std::find(Found.begin(),
				Found.end(), Words[i]) == Found.end())
				Found.push_back(Words[i]);
		int Score = static_cast<int>(Found.size());
		for (size_t i = 0; F.Pairs != 0 && i + 1 < Words.size(); ++i)
			if ((std::string(" ") + F.Pairs).find(" " + Words[i] + " " + Words[i + 1] + ",") != std::string::npos && !Negated(Words, i)) {
				Found.push_back(Words[i] + " " + Words[i + 1]);
				Score += 2;		// (a pair says more than either word)
			}
		if (Score > Best) {
			Best = Score;
			Out.Flavour = F.Id;
			Out.Words = Found;
		}
	}
	return Out;
}

std::string FlavourLine(int Flavour, const std::vector<std::string> &Words, const std::string &Where)
{
	const FlavourInfo &F = FlavourOf(Flavour);
	if (F.Id == FLAVOUR_NONE)
		return std::string();
	std::string From;
	for (size_t i = 0; i < Words.size(); ++i)
		From += (i == 0 ? "" : ", ") + Words[i];
	return "Flavour: " + std::string(F.Name) + (Where.empty() ? std::string(" (given)") : " (from " + Where + (From.empty() ?
		std::string() : " " + From) + ")") + ": " + F.Material + "; it leans the kind's picks that way, any kind goes with it. "
		"Pass flavour \"" + F.Name + "\" to what_next, song_shape, get_recipe and finish_song.";
}

bool FlavourLeansTo(const char *Leans, const std::string &Entry)
{
	if (Leans == 0)
		return false;
	const std::vector<std::string> All = Entries(Leans);
	return std::find(All.begin(), All.end(), Entry) != All.end();
}

std::string WeightedPick(const std::vector<std::string> &Pool, const char *Leans, unsigned int Hash, bool Seeded, bool &Leaned)
{
	Leaned = false;
	if (Pool.empty())
		return std::string();
	std::vector<int> Weights;
	int Total = 0, FirstLeaned = -1;
	for (size_t i = 0; i < Pool.size(); ++i) {
		const bool Leans_ = Leans != 0 && *Leans != 0 && FlavourLeansTo(Leans, Pool[i]);
		if (Leans_ && FirstLeaned < 0)
			FirstLeaned = static_cast<int>(i);
		Weights.push_back(Leans_ ? FLAVOUR_WEIGHT : 1);
		Total += Weights.back();
	}
	if (FirstLeaned < 0) {
		// (no lean here: the pool's own pick, as a song without a flavour has it)
		return Seeded ? Pool[Hash % Pool.size()] : Pool[0];
	}
	size_t At = static_cast<size_t>(FirstLeaned);
	if (Seeded) {
		int Left = static_cast<int>(Hash % static_cast<unsigned int>(Total));
		for (At = 0; At < Pool.size(); ++At) {
			if (Left < Weights[At])
				break;
			Left -= Weights[At];
		}
	}
	Leaned = Weights[At] > 1;
	return Pool[At];
}

void FlavourTempos(const std::vector<std::string> &Pool, int Lean, double Slowest, double Fastest, std::vector<std::string> &Out,
	std::string &Leans)
{
	Out = Pool;
	Leans.clear();
	if (Pool.empty() || Lean == 0)
		return;
	const double Own = std::atof(Pool[0].c_str());
	double Edge = Own;
	for (size_t i = 0; i < Pool.size(); ++i) {
		const double Bpm = std::atof(Pool[i].c_str());
		Edge = Lean < 0 ? std::min(Edge, Bpm) : std::max(Edge, Bpm);
		if ((Lean < 0 && Bpm < Own - 0.05) || (Lean > 0 && Bpm > Own + 0.05))
			Leans += (Leans.empty() ? "" : "|") + Pool[i];
	}
	// One even tempo a speed past the pool's edge (900 / speed at tempo 150, four rows a beat), within the kind's tempos
	const int Speed = static_cast<int>(std::floor(900.0 / Edge + 0.5)) + (Lean < 0 ? 1 : -1);
	const double Next = Speed >= 3 ? 900.0 / Speed : 0.0;
	const bool Inside = Next > 0.0 && (Slowest <= 0.0 || Next >= Slowest - 0.05) && (Fastest <= 0.0 || Next <= Fastest + 0.05);
	if (Inside && std::find(Out.begin(), Out.end(), BpmText(Next)) == Out.end()) {
		Out.push_back(BpmText(Next));
		Leans += (Leans.empty() ? "" : "|") + BpmText(Next);
	}
}

std::string FlavourProgression(int Flavour, const std::string &Progression, bool Minor)
{
	const FlavourInfo &F = FlavourOf(Flavour);
	if (F.Progression == 0)
		return Progression;
	// The bars' chords
	std::vector<std::string> Bars;
	std::string Item;
	for (size_t i = 0; i < Progression.size(); ++i) {
		if (Progression[i] == '|') {
			const size_t First = Item.find_first_not_of(' '), Last = Item.find_last_not_of(' ');
			if (First != std::string::npos)
				Bars.push_back(Item.substr(First, Last - First + 1));
			Item.clear();
		}
		else
			Item += Progression[i];
	}
	if (Bars.size() < 4)
		return Progression;
	bool Changed = false;
	for (size_t b = 1; b + 1 < Bars.size() && !Changed; ++b) {
		if (F.Progression == 1 && Minor && (Bars[b + 1] == "v" || Bars[b + 1] == "V") && Bars[b] != "i") {
			Bars[b] = "bII";
			Changed = true;
		}
		else if (F.Progression == 1 && !Minor && Bars[b] == "IV") {
			Bars[b] = "iv";
			Changed = true;
		}
		else if (F.Progression == 2 && !Minor && Bars[b] == "V") {
			Bars[b] = "bVII";
			Changed = true;
		}
	}
	if (!Changed)
		return Progression;
	std::string Out = "|";
	for (size_t b = 0; b < Bars.size(); ++b)
		Out += " " + Bars[b] + " |";
	return Out;
}

std::string FlavourAmbience(int Flavour, unsigned int Hash)
{
	const std::vector<std::string> All = Entries(FlavourOf(Flavour).Ambience);
	if (All.empty() || All[0].empty())
		return std::string();
	return All[Hash % All.size()];
}

std::string FlavourRecipe(int Flavour, bool Core)
{
	const FlavourInfo &F = FlavourOf(Flavour);
	if (F.Id == FLAVOUR_NONE)
		return std::string();
	if (Core)
		return "Flavour " + std::string(F.Name) + ": " + F.Material + ". Pass flavour \"" + F.Name + "\" to what_next: "
			"it leans the lead, the chords, the bass, the drums, the tempo and the ambience that way.";
	return "Flavour (the place's material and colour, beside the kind's mood): " + std::string(F.Recipe) + " These lean the "
		"kind's options, never replace them: what_next with flavour \"" + F.Name + "\" weighs the kind's leads, chord and bass "
		"styles, grooves and tempos toward them and says which pick came from the flavour; anything else that fits is right "
		"too.";
}

}	// namespace ftapi
