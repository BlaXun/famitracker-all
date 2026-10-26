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

// The Ask panel's model: the settings, the requests, the streamed replies and
// the conversation (see Assistant.h)

#include "Assistant.h"
#include "Exercises.h"
#include "Glossary.h"
#include "Http.h"
#include "Network.h"
#include "Tools.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ftapi {

namespace {

std::string Trim(const std::string &Text)
{
	size_t First = 0, Last = Text.size();
	while (First < Last && std::isspace(static_cast<unsigned char>(Text[First])))
		++First;
	while (Last > First && std::isspace(static_cast<unsigned char>(Text[Last - 1])))
		--Last;
	return Text.substr(First, Last - First);
}

std::string Lower(const std::string &Text)
{
	std::string Out = Text;
	for (size_t i = 0; i < Out.size(); ++i)
		Out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(Out[i])));
	return Out;
}

bool StartsWith(const std::string &Text, const char *pStart)
{
	return Text.compare(0, std::strlen(pStart), pStart) == 0;
}

bool EndsWith(const std::string &Text, const char *pEnd)
{
	const size_t Length = std::strlen(pEnd);
	return Text.size() >= Length && Text.compare(Text.size() - Length, Length, pEnd) == 0;
}

bool Has(const std::string &Text, const char *pPart)
{
	return Text.find(pPart) != std::string::npos;
}

std::string Number(long long Value)
{
	char Text[32];
	std::snprintf(Text, sizeof(Text), "%lld", Value);
	return Text;
}

// Cut to Most bytes, at a space when there's one near, without splitting a UTF-8 character, with "..." after
std::string Cut(const std::string &Text, size_t Most)
{
	if (Text.size() <= Most)
		return Text;
	size_t End = Most > 3 ? Most - 3 : 0;
	while (End > 0 && (static_cast<unsigned char>(Text[End]) & 0xC0) == 0x80)
		--End;
	const size_t Space = Text.rfind(' ', End);
	if (Space != std::string::npos && Space + 24 > End && Space > 0)
		End = Space;
	return Trim(Text.substr(0, End)) + "...";
}

const std::string *StringOf(const Json &Object, const char *pKey)
{
	const Json *pValue = Object.IsObject() ? Object.Get(pKey) : 0;
	return pValue != 0 && pValue->IsString() ? &pValue->String() : 0;
}

// ---- The tools

const AssistantToolKind TOOL_KINDS[] = {
	// Reading (the tools' read-only hint): they run at once in both modes
	{ "get_song", ASSISTANT_READS }, { "what_next", ASSISTANT_READS }, { "read_notes", ASSISTANT_READS },
	{ "check_loop", ASSISTANT_READS }, { "measure_mix", ASSISTANT_READS }, { "listen", ASSISTANT_READS },
	{ "compare_versions", ASSISTANT_READS }, { "effects", ASSISTANT_READS }, { "find_sound", ASSISTANT_READS },
	{ "review_arrangement", ASSISTANT_READS }, { "check_song", ASSISTANT_READS }, { "look_at_song", ASSISTANT_READS },
	{ "play", ASSISTANT_READS }, { "stop", ASSISTANT_READS }, { "get_recipe", ASSISTANT_READS }, { "explain", ASSISTANT_READS },
	{ "check_exercise", ASSISTANT_READS },
	// Reading or changing as their arguments say (AssistantCallChanges)
	{ "grow_melody", ASSISTANT_DEPENDS }, { "soundtrack_notes", ASSISTANT_DEPENDS }, { "references", ASSISTANT_DEPENDS },
	{ "study_nsf", ASSISTANT_DEPENDS }, { "study_module", ASSISTANT_DEPENDS },
	// Changing the song, its files or what FamiTracker shows: proposed in Ask first
	{ "write_notes", ASSISTANT_CHANGES }, { "place_clip", ASSISTANT_CHANGES }, { "add_instrument", ASSISTANT_CHANGES },
	{ "design_instrument", ASSISTANT_CHANGES }, { "set_instrument", ASSISTANT_CHANGES },
	{ "remove_unused_instruments", ASSISTANT_CHANGES }, { "transpose", ASSISTANT_CHANGES }, { "set_tempo", ASSISTANT_CHANGES },
	{ "set_meter", ASSISTANT_CHANGES }, { "name_song", ASSISTANT_CHANGES }, { "mix", ASSISTANT_CHANGES },
	{ "set_swing", ASSISTANT_CHANGES }, { "add_chords_and_bass", ASSISTANT_CHANGES }, { "add_drums", ASSISTANT_CHANGES },
	{ "add_harmony", ASSISTANT_CHANGES }, { "add_counter_melody", ASSISTANT_CHANGES }, { "polish", ASSISTANT_CHANGES },
	{ "add_echo", ASSISTANT_CHANGES }, { "set_pan", ASSISTANT_CHANGES }, { "add_vibrato", ASSISTANT_CHANGES },
	{ "change_mood", ASSISTANT_CHANGES }, { "make_version", ASSISTANT_CHANGES }, { "make_jingle", ASSISTANT_CHANGES },
	{ "make_sound_effect", ASSISTANT_CHANGES }, { "fix_loop", ASSISTANT_CHANGES }, { "set_loop", ASSISTANT_CHANGES },
	{ "insert_bars", ASSISTANT_CHANGES }, { "remove_bars", ASSISTANT_CHANGES }, { "name_section", ASSISTANT_CHANGES },
	{ "song_shape", ASSISTANT_CHANGES }, { "set_key", ASSISTANT_CHANGES }, { "set_mixer", ASSISTANT_CHANGES },
	{ "export_for_game", ASSISTANT_CHANGES }, { "finish_song", ASSISTANT_CHANGES }, { "select_track", ASSISTANT_CHANGES },
	{ "keep_version", ASSISTANT_CHANGES }, { "set_expansion", ASSISTANT_CHANGES }, { "make_groove", ASSISTANT_CHANGES },
	{ "copy_bars", ASSISTANT_CHANGES }, { "vary_part", ASSISTANT_CHANGES }, { "reference_sound", ASSISTANT_CHANGES },
	{ "add_sample_drums", ASSISTANT_CHANGES }, { "import_nsf", ASSISTANT_CHANGES }, { "send_file", ASSISTANT_CHANGES },
	{ "undo", ASSISTANT_CHANGES }, { "save_song", ASSISTANT_CHANGES },
};

std::string ArgText(const Json &Arguments, const char *pKey)
{
	const std::string *pValue = StringOf(Arguments, pKey);
	return pValue != 0 ? Lower(Trim(*pValue)) : std::string();
}

bool Given(const Json &Arguments, const char *pKey)
{
	const Json *pValue = Arguments.IsObject() ? Arguments.Get(pKey) : 0;
	return pValue != 0 && !pValue->IsNull();
}

// ---- What the model is told

const char *const GUIDE =
	"You are the helper in FamiTracker's Ask panel (View > Ask). FamiTracker is a music tracker for NES (Famicom) "
	"chiptunes, and this one is made for people new to music. The person asking may never have made music, read notes "
	"or used a tracker before; they're making music for their own game.\n"
	"\n"
	"How to help:\n"
	"- Plain words and short sentences. When a musical or tracker word is needed, say what it means in a few words.\n"
	"- One step at a time: give the next step, then stop and let them try it or ask. Don't pile up options.\n"
	"- Name FamiTracker's real menus and windows as they're written here, never invented ones: Help > Exercises (short "
	"exercises with feedback, the best start for a newcomer), Help > Tutorials, Help > Glossary, Help > Example songs; "
	"View > What next (the song's next step, with a Do it button), View > Problems, View > Piano roll (draw notes with "
	"the mouse; its Chords, Groove and Key buttons), View > Drum grid (draw a beat), View > Mixer, View > Clips panel; "
	"Edit > Record from microphone (sing, hum or whistle a tune), Edit > Record from your phone, Edit > Start from a "
	"groove (drums, a bass and chords in a mood), Edit > Add chords and bass line, Edit > Sound effect from your voice, "
	"Edit > Undo; Module > Song shape (a song's parts laid out), Module > Module Properties (the sound chips); "
	"Instrument > Sound browser (pick a sound and hear it); Tracker > Play and Tracker > Stop; File > Save and File > "
	"Export for games (OGG) (the music for the game, looping).\n"
	"- Write the words FamiTracker's glossary explains as [[term]], like [[chord]], [[melody]] or [[vibrato]]: in the "
	"panel they become links that open Help > Glossary at that word. [[shown words|term]] links other words to a term.\n"
	"- Each question comes with the song as it is then (\"The song now\"): its overview, what View > What next would "
	"do next, the exercise they're doing, where the cursor is, and the mode. Use it; don't ask for what it already says.\n"
	"- Keep answers short: a few sentences, or a short list. No tables.\n";

const char *const WITH_TOOLS =
	"\n"
	"FamiTracker's tools:\n"
	"- You can call FamiTracker's tools, described below. Tools that only read (get_song, read_notes, check_song, "
	"review_arrangement, what_next, play...) run at once: use them whenever they help you answer.\n"
	"- Ask first (the usual mode): a call that would change the song is shown to them as a proposal, with Do it and "
	"Skip. Before such a call, say in a sentence what it will do and why; make one change at a time and wait. If they "
	"skip it, don't make it again: ask what they'd like instead. Never change the song any other way.\n"
	"- Let it work: they asked you to do the work (\"compose a calm night song in D minor\", \"add drums to bars "
	"5-12\"). Make the changes yourself, step by step (for a whole song, what_next gives each step: call it, make the "
	"call it gives, ask it again), and say in a line what each did. Every change can be undone (Edit > Undo, or the "
	"panel's Take back all). Stop when it's done, or when you need them to choose, and tell them what to listen to.\n"
	"- Each question says which mode is on. Your changes go straight into the song (not as clips).\n"
	"\n"
	"The tools' own instructions follow.\n"
	"\n";

const char *const WITHOUT_TOOLS =
	"\n"
	"Here you have no tools: the model's server didn't take them, so you can't read the song beyond the summary that "
	"comes with each question, nor change it. Answer from that summary, and tell them where in FamiTracker to click.\n";

// ---- Answers as the panel shows them

void AddRun(std::vector<AssistantRun> &Runs, const std::string &Text, bool Bold, const std::string &Link)
{
	if (Text.empty())
		return;
	if (!Runs.empty() && Runs.back().Bold == Bold && Runs.back().Link == Link && Link.empty()) {
		Runs.back().Text += Text;
		return;
	}
	AssistantRun Run;
	Run.Text = Text;
	Run.Bold = Bold;
	Run.Link = Link;
	Runs.push_back(Run);
}

// A link's target: a glossary entry's id, or the panel's own ("exercise:...", "tutorial:...", "settings"); "" when it
// names nothing
std::string LinkTarget(const std::string &Words, const std::string &Named)
{
	const std::string Target = Trim(Named.empty() ? Words : Named);
	if (StartsWith(Target, "exercise:")) {
		const ExerciseInfo *pExercise = FindExercise(Target.substr(9));
		return pExercise != 0 ? "exercise:" + std::string(pExercise->Id) : std::string();
	}
	if (StartsWith(Target, "tutorial:"))
		return Target.size() > 9 ? Target : std::string();
	if (Target == "settings")
		return Target;
	const GlossaryEntry *pEntry = FindGlossaryEntry(Target);
	return pEntry != 0 ? pEntry->Id : std::string();
}

// A line's runs: [[links]], **bold**, `code`, [text](url), *emphasis*
std::vector<AssistantRun> InlineRuns(const std::string &Line, bool Bold0)
{
	std::vector<AssistantRun> Runs;
	bool Bold = Bold0;
	std::string Plain;
	size_t i = 0;
	while (i < Line.size()) {
		if (Line.compare(i, 2, "[[") == 0) {
			const size_t End = Line.find("]]", i + 2);
			if (End != std::string::npos) {
				const std::string Inside = Line.substr(i + 2, End - i - 2);
				const size_t Bar = Inside.find('|');
				const std::string Words = Bar == std::string::npos ? Inside : Inside.substr(0, Bar);
				const std::string Target = LinkTarget(Words, Bar == std::string::npos ? std::string() : Inside.substr(Bar + 1));
				AddRun(Runs, Plain, Bold, std::string());
				Plain.clear();
				AddRun(Runs, Trim(Words), Bold, Target);
				i = End + 2;
				continue;
			}
		}
		if (Line.compare(i, 2, "**") == 0 || Line.compare(i, 2, "__") == 0) {
			AddRun(Runs, Plain, Bold, std::string());
			Plain.clear();
			Bold = !Bold;
			i += 2;
			continue;
		}
		if (Line[i] == '`') {
			++i;		// code: its text as it is
			continue;
		}
		if (Line[i] == '[') {
			// [text](url): the text, and the address after it
			const size_t Close = Line.find("](", i + 1);
			const size_t End = Close != std::string::npos ? Line.find(')', Close + 2) : std::string::npos;
			if (Close != std::string::npos && End != std::string::npos && Line.find('[', i + 1) > Close) {
				const std::string Url = Line.substr(Close + 2, End - Close - 2);
				Plain += Line.substr(i + 1, Close - i - 1);
				if (StartsWith(Url, "http"))
					Plain += " (" + Url + ")";
				i = End + 1;
				continue;
			}
		}
		if (Line[i] == '*' && i + 1 < Line.size() && Line[i + 1] != ' ' && Line[i + 1] != '*' &&
			(i == 0 || Line[i - 1] == ' ' || Line[i - 1] == '(')) {
			// *emphasis*: the words without the stars
			const size_t End = Line.find('*', i + 1);
			if (End != std::string::npos && Line[End - 1] != ' ') {
				Plain += Line.substr(i + 1, End - i - 1);
				i = End + 1;
				continue;
			}
		}
		Plain += Line[i];
		++i;
	}
	AddRun(Runs, Plain, Bold, std::string());
	return Runs;
}

// A list item's mark at the line's start ("- ", "* ", "+ ", "12. ", "3) "): its length, Bullet the mark as shown
size_t ListMark(const std::string &Line, std::string &Bullet)
{
	if (Line.size() >= 2 && (Line[0] == '-' || Line[0] == '*' || Line[0] == '+') && Line[1] == ' ') {
		Bullet = "-";
		return 2;
	}
	size_t Digits = 0;
	while (Digits < Line.size() && Digits < 3 && std::isdigit(static_cast<unsigned char>(Line[Digits])))
		++Digits;
	if (Digits > 0 && Digits + 1 < Line.size() && (Line[Digits] == '.' || Line[Digits] == ')') && Line[Digits + 1] == ' ') {
		Bullet = Line.substr(0, Digits) + ".";
		return Digits + 2;
	}
	return 0;
}

// ---- Fitting the conversation into the model's context


std::string Shortened(const std::string &Result)
{
	return Cut(Result, ASSISTANT_SHORT_RESULT) + "\n(Shortened here to keep the conversation in your context; call the tool "
		"again for all of it.)";
}

std::string OwnId(int Count)
{
	char Text[16];
	std::snprintf(Text, sizeof(Text), "ft%07d", Count);	// nine letters and digits, as some chat templates want them
	return Text;
}

// ---- Help without a model

const char *const STOP_WORDS[] = {
	"the", "and", "for", "with", "how", "does", "did", "you", "your", "are", "was", "can", "could", "should", "would",
	"what", "which", "who", "why", "when", "where", "its", "this", "that", "these", "those", "make", "get", "use",
	"using", "want", "need", "help", "please", "about", "from", "into", "some", "any", "there", "here", "then", "than",
	"too", "very", "just", "way", "thing", "things", "song", "music", "famitracker", "have", "has", "had", "will",
	"not", "but", "all", "out", "more", "much", "many", "one", "like", "know", "tell", "show", "give", "let", "put",
	"set", "add", "new", "own", "my", "me", "is", "it", "do", "to", "of", "in", "on", "an", "or", "be", "so", "if",
	"at", "as", "by", "up", "we", "us", "am", "no", "yes"
};

bool StopWord(const std::string &Word)
{
	for (size_t i = 0; i < sizeof(STOP_WORDS) / sizeof(STOP_WORDS[0]); ++i)
		if (Word == STOP_WORDS[i])
			return true;
	return false;
}

std::vector<std::string> QuestionWords(const std::string &Question)
{
	std::vector<std::string> Words;
	std::string Word;
	const std::string Text = Lower(Question) + " ";
	for (size_t i = 0; i < Text.size(); ++i) {
		const unsigned char c = static_cast<unsigned char>(Text[i]);
		if (std::isalnum(c) || c >= 0x80 || (c == '-' && !Word.empty())) {
			Word += static_cast<char>(c);
			continue;
		}
		while (!Word.empty() && Word[Word.size() - 1] == '-')
			Word.erase(Word.size() - 1);
		// an effect as written ("4xy", "0xy") counts however short
		const bool Effect = Word.size() == 3 && std::isxdigit(static_cast<unsigned char>(Word[0])) && Word[1] == 'x';
		if (!Word.empty() && !StopWord(Word) && (Word.size() >= 3 || Effect) &&
			std::find(Words.begin(), Words.end(), Word) == Words.end())
			Words.push_back(Word);
		Word.clear();
	}
	return Words;
}

int CountIn(const std::string &Text, const std::vector<std::string> &Words)
{
	const std::string Low = Lower(Text);
	int Count = 0;
	for (size_t i = 0; i < Words.size(); ++i)
		if (Low.find(Words[i]) != std::string::npos)
			++Count;
	return Count;
}

}	// namespace

// ---- Settings and the server's address

AssistantSettings::AssistantSettings() : SmallSet(false), AskRounds(ASSISTANT_ASK_ROUNDS), WorkRounds(ASSISTANT_WORK_ROUNDS),
	ContextTokens(0)
{
}

AssistantSettings SaneAssistantSettings(const AssistantSettings &Settings)
{
	AssistantSettings Out = Settings;
	Out.Endpoint = Trim(Out.Endpoint);
	Out.Model = Trim(Out.Model);
	Out.Key = Trim(Out.Key);
	Out.AskRounds = std::max(1, std::min(ASSISTANT_MOST_ROUNDS, Out.AskRounds));
	Out.WorkRounds = std::max(1, std::min(ASSISTANT_MOST_ROUNDS, Out.WorkRounds));
	if (Out.ContextTokens < 0)
		Out.ContextTokens = 0;
	else if (Out.ContextTokens > 0 && Out.ContextTokens < ASSISTANT_LEAST_CONTEXT)
		Out.ContextTokens = ASSISTANT_LEAST_CONTEXT;
	return Out;
}

std::string AssistantEndpoint::HostHeader() const
{
	return Port == 80 ? Host : Host + ":" + Number(Port);
}

std::string AssistantEndpoint::Text() const
{
	return "http://" + Host + ":" + Number(Port);
}

bool ParseAssistantEndpoint(const std::string &Url, AssistantEndpoint &Out, std::string &Error)
{
	std::string Text = Trim(Url);
	if (Text.empty()) {
		Error = "No address is given for the model's server.";
		return false;
	}
	const std::string Low = Lower(Text);
	if (StartsWith(Low, "https://")) {
		Error = "FamiTracker talks plain http to your model's server on your home network; https isn't supported. Use the "
			"server's http:// address, like http://192.168.1.50:8080.";
		return false;
	}
	if (StartsWith(Low, "http://"))
		Text = Text.substr(7);
	else if (Text.find("://") != std::string::npos) {
		Error = "The address should start with http://, like http://192.168.1.50:8080.";
		return false;
	}
	const size_t Slash = Text.find('/');
	const std::string Authority = Text.substr(0, Slash);
	std::string Path = Slash == std::string::npos ? std::string() : Text.substr(Slash);
	if (Authority.find('@') != std::string::npos || Authority.find('[') != std::string::npos) {
		Error = "Give the server's computer as its IPv4 address or its name, like http://192.168.1.50:8080 or http://my-pc:8080.";
		return false;
	}
	AssistantEndpoint E;
	const size_t Colon = Authority.rfind(':');
	E.Host = Authority.substr(0, Colon);
	if (Colon != std::string::npos) {
		const std::string Port = Authority.substr(Colon + 1);
		long Value = 0;
		bool Digits = !Port.empty() && Port.size() <= 5;
		for (size_t i = 0; i < Port.size() && Digits; ++i)
			Digits = std::isdigit(static_cast<unsigned char>(Port[i])) != 0;
		if (Digits)
			Value = std::strtol(Port.c_str(), 0, 10);
		if (!Digits || Value < 1 || Value > 65535) {
			Error = "The port after the colon should be a number from 1 to 65535, like :8080.";
			return false;
		}
		E.Port = static_cast<int>(Value);
	}
	bool Plain = !E.Host.empty();
	for (size_t i = 0; i < E.Host.size() && Plain; ++i) {
		const unsigned char c = static_cast<unsigned char>(E.Host[i]);
		Plain = std::isalnum(c) || c == '.' || c == '-' || c == '_';
	}
	if (!Plain) {
		Error = E.Host.empty() ? "The address has no computer in it, like http://192.168.1.50:8080." :
			"\"" + E.Host + "\" isn't a computer's address or name.";
		return false;
	}
	while (!Path.empty() && Path[Path.size() - 1] == '/')
		Path.erase(Path.size() - 1);
	const size_t Query = Path.find('?');
	if (Query != std::string::npos)
		Path.erase(Query);
	if (EndsWith(Path, "/chat/completions"))
		Path.erase(Path.size() - 17);
	if (EndsWith(Path, "/v1")) {
		E.Base = Path;
		E.Root = Path.substr(0, Path.size() - 3);
	}
	else {
		E.Base = Path + "/v1";
		E.Root = Path;
	}
	Out = E;
	return true;
}

std::string AssistantPrivacyNote(const AssistantEndpoint &Where, const std::string &Address, const std::vector<std::string> &Own)
{
	if (Lower(Where.Host) == "localhost")
		return std::string();
	unsigned long Value = 0;
	const std::string At = Address.empty() && ParseIpv4(Where.Host, Value) ? Where.Host : Address;
	if (At.empty() || MayConnect(At, Own))
		return std::string();
	return "Your questions and a summary of the song go to " + Where.Host + (At != Where.Host ? " (" + At + ")" : std::string()) +
		", which isn't this PC or on your home network.";
}

// ---- The tools

const std::vector<AssistantToolKind> &AssistantToolKinds()
{
	static const std::vector<AssistantToolKind> Kinds(TOOL_KINDS, TOOL_KINDS + sizeof(TOOL_KINDS) / sizeof(TOOL_KINDS[0]));
	return Kinds;
}

bool AssistantCallChanges(const std::string &Tool, const Json &Arguments)
{
	if (Tool == "grow_melody")
		return Given(Arguments, "pick");
	if (Tool == "soundtrack_notes") {
		const std::string Action = ArgText(Arguments, "action");
		return Action == "add" || Action == "replace";
	}
	if (Tool == "references") {
		const std::string Action = ArgText(Arguments, "action");
		return Action == "tag" || Action == "delete";
	}
	if (Tool == "study_nsf" || Tool == "study_module") {
		const Json *pSave = Arguments.IsObject() ? Arguments.Get("save") : 0;
		return !(pSave != 0 && pSave->IsBool() && !pSave->Bool());
	}
	const std::vector<AssistantToolKind> &Kinds = AssistantToolKinds();
	for (size_t i = 0; i < Kinds.size(); ++i)
		if (Tool == Kinds[i].Name)
			return Kinds[i].Kind != ASSISTANT_READS;
	return true;
}

bool AssistantLeavesOut(const std::string &Tool)
{
	return Tool == "look_at_song" || Tool == "send_file";
}

Json AssistantFunctions(const std::vector<ToolInfo> &Tools, bool SmallSet)
{
	Json List = Json::MakeArray();
	for (size_t i = 0; i < Tools.size(); ++i) {
		const ToolInfo &Tool = Tools[i];
		if (AssistantLeavesOut(Tool.Name) || (SmallSet && !ToolSet::IsCoreTool(Tool.Name)))
			continue;
		Json Function = Json::MakeObject();
		Function.Set("name", Tool.Name);
		Function.Set("description", Tool.Description);
		Function.Set("parameters", Tool.InputSchema);
		Json Item = Json::MakeObject();
		Item.Set("type", "function");
		Item.Set("function", Function);
		List.Push(Item);
	}
	return List;
}

std::string AssistantCallWords(const std::string &Title, const Json &Arguments)
{
	std::vector<std::string> Parts;
	const Json *pFrom = Arguments.IsObject() ? Arguments.Get("from_bar") : 0;
	const Json *pTo = Arguments.IsObject() ? Arguments.Get("to_bar") : 0;
	bool Bars = false;
	if (Arguments.IsObject()) {
		for (size_t i = 0; i < Arguments.Members().size() && Parts.size() < 5; ++i) {
			const std::string &Key = Arguments.Members()[i].first;
			const Json &Value = Arguments.Members()[i].second;
			std::string Name = Key;
			std::replace(Name.begin(), Name.end(), '_', ' ');
			if (Key == "from_bar" || Key == "to_bar") {
				if (Bars)
					continue;
				Bars = true;
				const bool HasFrom = pFrom != 0 && pFrom->IsNumber(), HasTo = pTo != 0 && pTo->IsNumber();
				if (HasFrom && HasTo)
					Parts.push_back(pFrom->Integer() == pTo->Integer() ? "bar " + Number(pFrom->Integer()) :
						"bars " + Number(pFrom->Integer()) + "-" + Number(pTo->Integer()));
				else if (HasFrom)
					Parts.push_back("from bar " + Number(pFrom->Integer()));
				else if (HasTo)
					Parts.push_back("to bar " + Number(pTo->Integer()));
				continue;
			}
			if (Key == "format" || Value.IsNull())
				continue;
			if (Value.IsString()) {
				const std::string Text = Trim(Value.String());
				if (Key == "channel" || Key == "section" || Key == "track")
					Parts.push_back(Text);
				else if (Text.size() <= 28 && Text.find('\n') == std::string::npos)
					Parts.push_back(Name + " " + Text);
				else
					Parts.push_back(Name);
			}
			else if (Value.IsNumber())
				Parts.push_back((Key == "channel" ? std::string("channel") : Name) + " " +
					(Value.IsInteger() ? Number(Value.Integer()) : WriteJson(Value)));
			else if (Value.IsBool()) {
				if (Value.Bool())
					Parts.push_back(Name);
			}
			else
				Parts.push_back(Name);
		}
	}
	std::string Words = Title;
	for (size_t i = 0; i < Parts.size(); ++i)
		Words += (i == 0 ? ": " : ", ") + Parts[i];
	return Words;
}

std::string AssistantCallText(const std::string &Tool, const std::string &Arguments, size_t Most)
{
	Json Parsed;
	const std::string Args = ParseJson(Arguments, Parsed) ? WriteJson(Parsed) : Trim(Arguments);
	return Cut(Tool + " " + (Args.empty() ? std::string("{}") : Args), Most);
}

std::string AssistantResultLine(const std::string &Result, size_t Most)
{
	size_t Start = 0;
	std::string Line;
	while (Start <= Result.size() && Line.empty()) {
		size_t End = Result.find('\n', Start);
		if (End == std::string::npos)
			End = Result.size();
		Line = Trim(Result.substr(Start, End - Start));
		Start = End + 1;
	}
	return Cut(Line, Most);
}

// ---- What the model is told

std::string AssistantSystemPrompt(bool SmallSet, bool WithTools)
{
	std::string Text = GUIDE;
	// The glossary's terms, so links lead somewhere
	std::string Terms;
	const std::vector<GlossaryEntry> &Entries = GlossaryEntries();
	for (size_t i = 0; i < Entries.size(); ++i)
		if (Entries[i].Category != GLOSSARY_EFFECTS)
			Terms += (Terms.empty() ? "" : ", ") + Entries[i].Term;
	Text += "- The glossary's terms: " + Terms + "; and every effect, like [[4xy]] or [[0xy]].\n";
	if (!WithTools)
		return Text + WITHOUT_TOOLS;
	return Text + WITH_TOOLS + (SmallSet ? ToolSet::CoreInstructions() : ToolSet::Instructions());
}

std::string AssistantContext(const AssistantFacts &Facts)
{
	std::string Text = "The song now (as the question is asked):\n";
	std::string Song = Trim(Facts.Song);
	if (Song.size() > ASSISTANT_SONG_MOST) {
		size_t End = Song.rfind('\n', ASSISTANT_SONG_MOST);
		if (End == std::string::npos || End < ASSISTANT_SONG_MOST / 2)
			End = ASSISTANT_SONG_MOST;
		Song = Song.substr(0, End) + "\n..." + (Facts.WithTools ? " (get_song has the rest)" : "");
	}
	Text += Song.empty() ? std::string("(no song is open)") : Song;
	if (!Trim(Facts.Next).empty())
		Text += "\n\nWhat next (View > What next) says:\n" + Trim(Facts.Next);
	if (!Facts.Exercise.empty()) {
		Text += "\n\nThey're doing an exercise (Help > Exercises): \"" + Facts.Exercise + "\".";
		if (!Trim(Facts.ExerciseStep).empty())
			Text += " Its note asks: " + Trim(Facts.ExerciseStep);
		if (!Facts.ExerciseCheck.empty() && Facts.WithTools)
			Text += "\ncheck_exercise {\"exercise\": \"" + Facts.ExerciseCheck + "\"} checks it as the note's Check button does. "
				"Help them do it themselves: hints and the next small step, not the answer written for them.";
	}
	if (Facts.Bar > 0)
		Text += "\n\nThe cursor: bar " + Number(Facts.Bar) + (Facts.Channel.empty() ? std::string() : ", " + Facts.Channel) + ".";
	if (!Facts.WithTools)
		Text += "\n\nNo tools here: answer from this, and say where to click.";
	else if (Facts.Mode == ASSISTANT_LET_IT_WORK)
		Text += "\n\nMode: Let it work. They asked you to do it: your changes go in as you make them, each one undoable.";
	else
		Text += "\n\nMode: Ask first. Tools that read run at once; a change you call is shown to them to do or skip.";
	return Text;
}

// ---- Answers as the panel shows them

std::vector<AssistantParagraph> AssistantRichText(const std::string &Text)
{
	std::vector<AssistantParagraph> Out;
	bool Code = false, Gap = false;
	size_t Start = 0;
	while (Start <= Text.size()) {
		size_t End = Text.find('\n', Start);
		if (End == std::string::npos)
			End = Text.size();
		std::string Line = Text.substr(Start, End - Start);
		Start = End + 1;
		if (!Line.empty() && Line[Line.size() - 1] == '\r')
			Line.erase(Line.size() - 1);
		const std::string Trimmed = Trim(Line);
		if (StartsWith(Trimmed, "```")) {
			Code = !Code;
			continue;
		}
		AssistantParagraph P;
		if (Code) {
			P.Code = true;
			AddRun(P.Runs, Line.empty() ? std::string(" ") : Line, false, std::string());
			P.Gap = Gap;
			Gap = false;
			Out.push_back(P);
			continue;
		}
		if (Trimmed.empty() || Trimmed == "---" || Trimmed == "***" || Trimmed == "___") {
			Gap = !Out.empty();
			continue;
		}
		std::string Body = Trimmed;
		if (Body[0] == '#') {
			size_t Hashes = 0;
			while (Hashes < Body.size() && Body[Hashes] == '#')
				++Hashes;
			if (Hashes <= 6 && Hashes < Body.size() && Body[Hashes] == ' ') {
				P.Heading = true;
				Body = Trim(Body.substr(Hashes));
			}
		}
		if (!P.Heading) {
			const size_t Mark = ListMark(Body, P.Bullet);
			if (Mark > 0)
				Body = Trim(Body.substr(Mark));
		}
		if (StartsWith(Body, "> "))
			Body = Body.substr(2);
		P.Runs = InlineRuns(Body, P.Heading);
		P.Gap = Gap || (P.Heading && !Out.empty());
		Gap = false;
		if (!P.Runs.empty())
			Out.push_back(P);
	}
	return Out;
}

std::string AssistantPlainText(const std::string &Text)
{
	const std::vector<AssistantParagraph> Paragraphs = AssistantRichText(Text);
	std::string Out;
	for (size_t i = 0; i < Paragraphs.size(); ++i) {
		const AssistantParagraph &P = Paragraphs[i];
		if (i > 0)
			Out += P.Gap ? "\n\n" : "\n";
		if (!P.Bullet.empty())
			Out += P.Bullet + " ";
		for (size_t r = 0; r < P.Runs.size(); ++r)
			Out += P.Runs[r].Text;
	}
	return Out;
}

// ---- The server's replies

HttpStreamReader::HttpStreamReader() : m_State(HEAD), m_iStatus(0), m_bChunked(false), m_bHasLength(false), m_iLeft(0), m_iChunk(0)
{
}

HttpStreamReader::state_t HttpStreamReader::Feed(const char *pData, size_t Size, std::string &Body)
{
	if (m_State == DONE || m_State == BAD)
		return m_State;
	if (Size > 0)
		m_Buffer.append(pData, Size);
	return Read(Body);
}

HttpStreamReader::state_t HttpStreamReader::Read(std::string &Body)
{
	while (m_State == HEAD) {
		const size_t End = m_Buffer.find("\r\n\r\n");
		if (End == std::string::npos) {
			if (m_Buffer.size() > (64u << 10))
				m_State = BAD;
			return m_State;
		}
		const std::string Head = m_Buffer.substr(0, End + 2);
		m_Buffer.erase(0, End + 4);
		const size_t LineEnd = Head.find("\r\n");
		const std::string Status = Head.substr(0, LineEnd);
		const size_t Space = Status.find(' ');
		if (!StartsWith(Status, "HTTP/") || Space == std::string::npos || Status.size() < Space + 4) {
			m_State = BAD;
			return m_State;
		}
		m_iStatus = 0;
		for (size_t i = Space + 1; i < Space + 4; ++i) {
			if (!std::isdigit(static_cast<unsigned char>(Status[i]))) {
				m_State = BAD;
				return m_State;
			}
			m_iStatus = m_iStatus * 10 + (Status[i] - '0');
		}
		m_bChunked = m_bHasLength = false;
		m_ContentType.clear();
		for (size_t Position = LineEnd + 2; Position < Head.size(); ) {
			const size_t Next = Head.find("\r\n", Position);
			const std::string Line = Head.substr(Position, Next - Position);
			Position = Next + 2;
			const size_t Colon = Line.find(':');
			if (Colon == std::string::npos)
				continue;
			const std::string Name = Trim(Line.substr(0, Colon)), Value = Trim(Line.substr(Colon + 1));
			if (EqualsNoCase(Name, "Content-Type"))
				m_ContentType = Value;
			else if (EqualsNoCase(Name, "Transfer-Encoding"))
				m_bChunked = Lower(Value).find("chunked") != std::string::npos;
			else if (EqualsNoCase(Name, "Content-Length")) {
				bool Digits = !Value.empty() && Value.size() < 16;
				for (size_t i = 0; i < Value.size() && Digits; ++i)
					Digits = std::isdigit(static_cast<unsigned char>(Value[i])) != 0;
				if (!Digits) {
					m_State = BAD;
					return m_State;
				}
				m_bHasLength = true;
				m_iLeft = static_cast<size_t>(std::strtoull(Value.c_str(), 0, 10));
			}
		}
		if (m_iStatus >= 100 && m_iStatus < 200)
			continue;		// "100 Continue" comes before the answer
		m_State = BODY;
		if (m_iStatus == 204 || m_iStatus == 304 || (!m_bChunked && m_bHasLength && m_iLeft == 0))
			m_State = DONE;
		m_iChunk = 0;
	}
	while (m_State == BODY) {
		if (!m_bChunked) {
			if (!m_bHasLength) {
				Body += m_Buffer;
				m_Buffer.clear();
				return m_State;
			}
			const size_t Take = std::min(m_iLeft, m_Buffer.size());
			Body.append(m_Buffer, 0, Take);
			m_Buffer.erase(0, Take);
			m_iLeft -= Take;
			if (m_iLeft == 0)
				m_State = DONE;
			return m_State;
		}
		if (m_iChunk == 0 || m_iChunk == 3) {
			const size_t End = m_Buffer.find("\r\n");
			if (End == std::string::npos) {
				if (m_Buffer.size() > 4096)
					m_State = BAD;
				return m_State;
			}
			const std::string Line = m_Buffer.substr(0, End);
			m_Buffer.erase(0, End + 2);
			if (m_iChunk == 3) {
				if (Line.empty())
					m_State = DONE;		// the trailers end
				continue;
			}
			const std::string Size = Trim(Line.substr(0, Line.find(';')));
			bool Hex = !Size.empty() && Size.size() <= 8;
			for (size_t i = 0; i < Size.size() && Hex; ++i)
				Hex = std::isxdigit(static_cast<unsigned char>(Size[i])) != 0;
			if (!Hex) {
				m_State = BAD;
				return m_State;
			}
			m_iLeft = static_cast<size_t>(std::strtoul(Size.c_str(), 0, 16));
			m_iChunk = m_iLeft == 0 ? 3 : 1;
			continue;
		}
		if (m_iChunk == 1) {
			const size_t Take = std::min(m_iLeft, m_Buffer.size());
			Body.append(m_Buffer, 0, Take);
			m_Buffer.erase(0, Take);
			m_iLeft -= Take;
			if (m_iLeft > 0)
				return m_State;
			m_iChunk = 2;
		}
		if (m_Buffer.size() < 2)
			return m_State;
		if (m_Buffer.compare(0, 2, "\r\n") != 0) {
			m_State = BAD;
			return m_State;
		}
		m_Buffer.erase(0, 2);
		m_iChunk = 0;
	}
	return m_State;
}

HttpStreamReader::state_t HttpStreamReader::Close(std::string &Body)
{
	if (m_State == BODY && !m_bChunked && !m_bHasLength) {
		Body += m_Buffer;
		m_Buffer.clear();
		m_State = DONE;
	}
	else if (m_State != DONE)
		m_State = BAD;
	return m_State;
}

void SseReader::Feed(const std::string &Bytes, std::vector<std::string> &Events)
{
	for (size_t i = 0; i < Bytes.size(); ++i) {
		const char c = Bytes[i];
		if (m_bCr) {
			m_bCr = false;
			if (c == '\n')
				continue;	// the LF of a CRLF
		}
		if (c == '\r') {
			m_bCr = true;
			Line(Events);
		}
		else if (c == '\n')
			Line(Events);
		else
			m_Line += c;
	}
}

void SseReader::Line(std::vector<std::string> &Events)
{
	if (m_Line.empty()) {
		// a blank line: the event is whole
		if (m_bData)
			Events.push_back(m_Data);
		m_Data.clear();
		m_bData = false;
		return;
	}
	if (m_Line[0] != ':') {		// a comment otherwise
		const size_t Colon = m_Line.find(':');
		const std::string Field = m_Line.substr(0, Colon);
		std::string Value = Colon == std::string::npos ? std::string() : m_Line.substr(Colon + 1);
		if (!Value.empty() && Value[0] == ' ')
			Value.erase(0, 1);
		if (Field == "data") {
			if (m_bData)
				m_Data += '\n';
			m_Data += Value;
			m_bData = true;
		}
		else if (Field == "error") {
			// llama.cpp's server sends an error in a stream as a field of its own
			const std::string Inside = Trim(Value);
			m_Data = "{\"error\":" + (!Inside.empty() && (Inside[0] == '{' || Inside[0] == '"') ? Inside :
				WriteJson(Json(Inside))) + "}";
			m_bData = true;
		}
	}
	m_Line.clear();
}

void SseReader::End(std::vector<std::string> &Events)
{
	if (!m_Line.empty())
		Line(Events);
	if (m_bData)
		Events.push_back(m_Data);
	m_Data.clear();
	m_bData = false;
}

AssistantStream::AssistantStream() : m_bEvents(true), m_bThinking(false)
{
}

void AssistantStream::Start(int Status, const std::string &ContentType)
{
	m_Reply.Status = Status;
	m_bEvents = Lower(ContentType).find("text/event-stream") != std::string::npos;
}

void AssistantStream::Feed(const std::string &Bytes, std::string &NewText, std::string &NewThinking)
{
	if (!m_bEvents) {
		m_Body += Bytes;
		return;
	}
	std::vector<std::string> Events;
	m_Events.Feed(Bytes, Events);
	for (size_t i = 0; i < Events.size(); ++i)
		Event(Events[i], NewText, NewThinking);
}

void AssistantStream::End()
{
	std::string NewText, NewThinking;
	if (m_bEvents) {
		std::vector<std::string> Events;
		m_Events.End(Events);
		for (size_t i = 0; i < Events.size(); ++i)
			Event(Events[i], NewText, NewThinking);
		if (!m_Reply.Finish.empty())
			m_Reply.Ended = true;
	}
	else {
		Json Whole;
		if (ParseJson(m_Body, Whole) && Whole.IsObject()) {
			const Json *pError = Whole.Get("error");
			if (pError != 0 && !pError->IsNull())
				m_Reply.Error = AssistantErrorMessage(m_Body);
			const Json *pChoices = Whole.Get("choices");
			if (pChoices != 0 && pChoices->IsArray() && pChoices->Size() > 0) {
				Choice(pChoices->At(0), true, NewText, NewThinking);
				m_Reply.Ended = true;
			}
			const Json *pUsage = Whole.Get("usage");
			if (pUsage != 0 && pUsage->IsObject()) {
				const Json *pPrompt = pUsage->Get("prompt_tokens"), *pReply = pUsage->Get("completion_tokens");
				if (pPrompt != 0 && pPrompt->IsNumber())
					m_Reply.PromptTokens = static_cast<int>(pPrompt->Integer());
				if (pReply != 0 && pReply->IsNumber())
					m_Reply.ReplyTokens = static_cast<int>(pReply->Integer());
			}
		}
		else if (m_Reply.Status >= 400 || !Trim(m_Body).empty())
			m_Reply.Error = AssistantErrorMessage(m_Body);
		if (m_Reply.Status >= 400 && m_Reply.Error.empty())
			m_Reply.Error = "HTTP " + Number(m_Reply.Status);
	}
	// What was held back in case it began a tag
	if (!m_Held.empty()) {
		if (m_bThinking)
			m_Reply.Reasoning += m_Held;
		else
			m_Reply.Content += m_Held;
		m_Held.clear();
	}
	while (!m_Reply.Content.empty() && std::isspace(static_cast<unsigned char>(m_Reply.Content[m_Reply.Content.size() - 1])))
		m_Reply.Content.erase(m_Reply.Content.size() - 1);
}

void AssistantStream::Event(const std::string &Data, std::string &NewText, std::string &NewThinking)
{
	const std::string Text = Trim(Data);
	if (Text == "[DONE]") {
		m_Reply.Ended = true;
		return;
	}
	Json Event;
	if (!ParseJson(Text, Event) || !Event.IsObject())
		return;
	const Json *pError = Event.Get("error");
	if (pError != 0 && !pError->IsNull()) {
		m_Reply.Error = AssistantErrorMessage(Text);
		return;
	}
	const Json *pChoices = Event.Get("choices");
	if (pChoices != 0 && pChoices->IsArray())
		for (size_t i = 0; i < pChoices->Size(); ++i) {
			const Json *pIndex = pChoices->At(i).Get("index");
			if (pIndex == 0 || !pIndex->IsNumber() || pIndex->Integer() == 0)
				Choice(pChoices->At(i), false, NewText, NewThinking);
		}
	const Json *pUsage = Event.Get("usage");
	if (pUsage != 0 && pUsage->IsObject()) {
		const Json *pPrompt = pUsage->Get("prompt_tokens"), *pReply = pUsage->Get("completion_tokens");
		if (pPrompt != 0 && pPrompt->IsNumber())
			m_Reply.PromptTokens = static_cast<int>(pPrompt->Integer());
		if (pReply != 0 && pReply->IsNumber())
			m_Reply.ReplyTokens = static_cast<int>(pReply->Integer());
	}
}

void AssistantStream::Choice(const Json &Choice, bool Whole, std::string &NewText, std::string &NewThinking)
{
	if (!Choice.IsObject())
		return;
	const Json *pDelta = Choice.Get(Whole ? "message" : "delta");
	if (pDelta != 0 && pDelta->IsObject()) {
		const std::string *pContent = StringOf(*pDelta, "content");
		if (pContent != 0)
			Text(*pContent, NewText, NewThinking);
		const char *const THINKING[] = { "reasoning_content", "reasoning" };
		for (size_t k = 0; k < 2; ++k) {
			const std::string *pThinking = StringOf(*pDelta, THINKING[k]);
			if (pThinking != 0) {
				m_Reply.Reasoning += *pThinking;
				NewThinking += *pThinking;
				break;
			}
		}
		const Json *pCalls = pDelta->Get("tool_calls");
		if (pCalls != 0 && pCalls->IsArray())
			for (size_t i = 0; i < pCalls->Size(); ++i) {
				const Json &Piece = pCalls->At(i);
				if (!Piece.IsObject())
					continue;
				const Json *pIndex = Piece.Get("index");
				const int Index = pIndex != 0 && pIndex->IsNumber() ? static_cast<int>(pIndex->Integer()) :
					(Whole ? static_cast<int>(i) : -1);
				const std::string *pId = StringOf(Piece, "id");
				// A piece belongs to the call of its index; without one, to the last call unless it starts another
				size_t Slot = m_Reply.Calls.size();
				for (size_t c = 0; c < m_Indexes.size(); ++c)
					if (Index >= 0 && m_Indexes[c] == Index)
						Slot = c;
				if (Index < 0 && !m_Reply.Calls.empty() && (pId == 0 || pId->empty() || *pId == m_Reply.Calls.back().Id))
					Slot = m_Reply.Calls.size() - 1;
				if (Slot == m_Reply.Calls.size()) {
					m_Reply.Calls.push_back(AssistantCall());
					m_Indexes.push_back(Index);
				}
				AssistantCall &Call = m_Reply.Calls[Slot];
				if (pId != 0 && !pId->empty())
					Call.Id = *pId;
				const Json *pFunction = Piece.Get("function");
				if (pFunction != 0 && pFunction->IsObject()) {
					const std::string *pName = StringOf(*pFunction, "name");
					if (pName != 0 && *pName != Call.Name)
						Call.Name += *pName;	// in pieces, or whole once (a server repeating it changes nothing)
					const Json *pArguments = pFunction->Get("arguments");
					if (pArguments != 0 && pArguments->IsString())
						Call.Arguments += pArguments->String();
					else if (pArguments != 0 && pArguments->IsObject())
						Call.Arguments = WriteJson(*pArguments);
				}
			}
	}
	const std::string *pFinish = StringOf(Choice, "finish_reason");
	if (pFinish != 0 && !pFinish->empty())
		m_Reply.Finish = *pFinish;
}

// Thinking written in the text between <think> and </think> (a server that doesn't take it apart) goes with the
// thinking; a tag cut between two pieces waits for the next
void AssistantStream::Text(const std::string &Piece, std::string &NewText, std::string &NewThinking)
{
	m_Held += Piece;
	for (;;) {
		const char *pTag = m_bThinking ? "</think>" : "<think>";
		const size_t At = m_Held.find(pTag);
		std::string Out;
		if (At != std::string::npos) {
			Out = m_Held.substr(0, At);
			m_Held.erase(0, At + std::strlen(pTag));
		}
		else {
			// keep the end that may begin the tag
			size_t Keep = 0;
			const size_t Length = std::strlen(pTag);
			for (size_t k = std::min(Length - 1, m_Held.size()); k > 0; --k)
				if (m_Held.compare(m_Held.size() - k, k, pTag, k) == 0) {
					Keep = k;
					break;
				}
			Out = m_Held.substr(0, m_Held.size() - Keep);
			m_Held.erase(0, m_Held.size() - Keep);
		}
		if (m_bThinking) {
			m_Reply.Reasoning += Out;
			NewThinking += Out;
		}
		else {
			if (m_Reply.Content.empty()) {
				size_t Lead = 0;	// the text starts with what it says, not the line breaks after the thinking
				while (Lead < Out.size() && std::isspace(static_cast<unsigned char>(Out[Lead])))
					++Lead;
				Out.erase(0, Lead);
			}
			m_Reply.Content += Out;
			NewText += Out;
		}
		if (At == std::string::npos)
			return;
		m_bThinking = !m_bThinking;
	}
}

int AssistantContextOfProps(const std::string &Body)
{
	Json Props;
	if (!ParseJson(Body, Props) || !Props.IsObject())
		return 0;
	const Json *pSettings = Props.Get("default_generation_settings");
	const Json *pContext = pSettings != 0 && pSettings->IsObject() ? pSettings->Get("n_ctx") : 0;
	if (pContext == 0 || !pContext->IsNumber())
		pContext = Props.Get("n_ctx");
	return pContext != 0 && pContext->IsNumber() && pContext->Integer() > 0 ? static_cast<int>(pContext->Integer()) : 0;
}

std::vector<std::string> AssistantModelsOf(const std::string &Body)
{
	std::vector<std::string> Names;
	Json List;
	if (!ParseJson(Body, List) || !List.IsObject())
		return Names;
	const Json *pData = List.Get("data");
	if (pData != 0 && pData->IsArray())
		for (size_t i = 0; i < pData->Size(); ++i) {
			const std::string *pId = StringOf(pData->At(i), "id");
			if (pId != 0 && !pId->empty() && std::find(Names.begin(), Names.end(), *pId) == Names.end())
				Names.push_back(*pId);
		}
	return Names;
}

std::string AssistantErrorMessage(const std::string &Body)
{
	Json Reply;
	if (ParseJson(Body, Reply) && Reply.IsObject()) {
		const Json *pError = Reply.Get("error");
		if (pError != 0 && pError->IsString())
			return Cut(Trim(pError->String()), 300);
		if (pError != 0 && pError->IsObject()) {
			const std::string *pMessage = StringOf(*pError, "message");
			if (pMessage != 0)
				return Cut(Trim(*pMessage), 300);
		}
		const char *const KEYS[] = { "detail", "message" };
		for (size_t k = 0; k < 2; ++k) {
			const std::string *pText = StringOf(Reply, KEYS[k]);
			if (pText != 0)
				return Cut(Trim(*pText), 300);
		}
	}
	// A page: its words without the tags
	std::string Words;
	bool Tag = false;
	const std::string Page = ValidUtf8(Body);
	for (size_t i = 0; i < Page.size(); ++i) {
		const char c = Page[i];
		if (c == '<' && Page.find('>', i) != std::string::npos)
			Tag = true;
		else if (c == '>' && Tag)
			Tag = false;
		else if (!Tag) {
			const bool Space = std::isspace(static_cast<unsigned char>(c)) != 0;
			if (!Space || (!Words.empty() && Words[Words.size() - 1] != ' '))
				Words += Space ? ' ' : c;
			continue;
		}
		if (!Words.empty() && Words[Words.size() - 1] != ' ')
			Words += ' ';
	}
	return Cut(Trim(Body.find('<') != std::string::npos ? Words : AssistantResultLine(Page, 300)), 300);
}

bool AssistantToolsRefused(int Status, const std::string &Message)
{
	if (Status < 400)
		return false;
	const std::string Low = Lower(Message);
	return Has(Low, "jinja") || Has(Low, "does not support tools") || Has(Low, "doesn't support tools") ||
		(Has(Low, "tool") && (Has(Low, "not support") || Has(Low, "unsupported") || Has(Low, "not supported") ||
		Has(Low, "not enabled")));
}

std::string AssistantHttpProblem(const AssistantEndpoint &Where, const std::string &Model, int Status, const std::string &Message)
{
	const std::string Said = Message.empty() ? std::string() : " (it said: " + Message + ")";
	const std::string Low = Lower(Message);
	if (Status == 401 || Status == 403)
		return "The server at " + Where.Text() + " wants a key, or didn't take the one given" + Said + ". Set it in the Ask "
			"panel's settings.";
	const bool AboutModel = Has(Low, "model") || (!Model.empty() && Has(Low, Lower(Model).c_str()));
	if (AboutModel && (Has(Low, "not found") || Has(Low, "no such") || Has(Low, "unknown") || Has(Low, "could not find") ||
		Has(Low, "does not exist") || Has(Low, "not exist") || Has(Low, "no model")))
		return "The server has no model called \"" + Model + "\"" + Said + ". Check the model's name in the settings: Test "
			"connection lists the ones it has.";
	if (Status == 404)
		return "There's no chat at " + Where.Text() + Where.ChatPath() + Said + ". Check the address: llama-server and "
			"llama-swap are http://<computer>:<port> (8080 unless you chose another), Ollama's port is 11434 and LM Studio's 1234.";
	if (Has(Low, "context") || Has(Low, "too long") || Has(Low, "exceed") || Has(Low, "too large"))
		return "The conversation is longer than the model can read at once" + Said + ". Press New question, pick the small set "
			"of tools in the settings, or give the model a bigger context (llama-server's -c, like -c 65536).";
	if (Status >= 500)
		return "The server had a problem answering (HTTP " + Number(Status) + Said + "). Its log says more: a model that's "
			"still loading or out of memory says so there.";
	return "The server answered HTTP " + Number(Status) + Said + ".";
}

// ---- Requests

double AssistantRequestRoom(int ContextTokens, double BytesPerToken)
{
	const double Context = double(std::max(ASSISTANT_LEAST_CONTEXT, ContextTokens));
	return (Context - std::min(6144.0, Context / 5)) * BytesPerToken;
}

std::string AssistantHttpRequest(const AssistantEndpoint &Where, const std::string &Path, const std::string &Key,
	const std::string &Body, bool Stream)
{
	std::string Clean;
	for (size_t i = 0; i < Key.size(); ++i)
		if (static_cast<unsigned char>(Key[i]) >= 0x20 && Key[i] != 0x7F)
			Clean += Key[i];
	std::string Out = (Body.empty() ? "GET " : "POST ") + Path + " HTTP/1.1\r\nHost: " + Where.HostHeader() + "\r\n"
		"User-Agent: FamiTracker\r\nAccept: " + (Stream ? "text/event-stream" : "application/json") + "\r\nConnection: close\r\n";
	if (!Clean.empty())
		Out += "Authorization: Bearer " + Clean + "\r\n";
	if (!Body.empty())
		Out += "Content-Type: application/json\r\nContent-Length: " + Number(static_cast<long long>(Body.size())) + "\r\n";
	return Out + "\r\n" + Body;
}

std::string AssistantProbeBody(const std::string &Model)
{
	Json Body = Json::MakeObject();
	if (!Model.empty())
		Body.Set("model", Model);
	Json Messages = Json::MakeArray();
	Json Message = Json::MakeObject();
	Message.Set("role", "user");
	Message.Set("content", "Say OK.");
	Messages.Push(Message);
	Body.Set("messages", Messages);
	Json Parameters = Json::MakeObject();
	Parameters.Set("type", "object");
	Parameters.Set("properties", Json::MakeObject());
	Json Function = Json::MakeObject();
	Function.Set("name", "get_song");
	Function.Set("description", "Overview of the song open in FamiTracker.");
	Function.Set("parameters", Parameters);
	Json Tool = Json::MakeObject();
	Tool.Set("type", "function");
	Tool.Set("function", Function);
	Json Tools = Json::MakeArray();
	Tools.Push(Tool);
	Body.Set("tools", Tools);
	Body.Set("max_tokens", 1);
	Body.Set("stream", false);
	return WriteJson(Body);
}

// ---- The conversation

AssistantChat::AssistantChat() : m_State(IDLE), m_iQuestion(0), m_iMode(ASSISTANT_ASK_FIRST), m_iRounds(0),
	m_iMostRounds(ASSISTANT_ASK_ROUNDS), m_iCalls(0), m_bToolsOff(false), m_dBytesPerToken(ASSISTANT_BYTES_PER_TOKEN), m_iSent(0)
{
}

void AssistantChat::Clear()
{
	m_Messages.clear();
	m_State = IDLE;
	m_iRounds = 0;
	m_bToolsOff = false;
}

void AssistantChat::SetTools(const std::vector<ToolInfo> &Tools, bool SmallSet)
{
	m_Titles.clear();
	for (size_t i = 0; i < Tools.size(); ++i)
		if (!AssistantLeavesOut(Tools[i].Name) && (!SmallSet || ToolSet::IsCoreTool(Tools[i].Name)))
			m_Titles.push_back(std::make_pair(Tools[i].Name, Tools[i].Title));
}

void AssistantChat::Ask(const std::string &Question, const std::string &Context, int Mode, int MostRounds)
{
	for (size_t m = 0; m < m_Messages.size(); ++m)
		for (size_t c = 0; c < m_Messages[m].Calls.size(); ++c) {
			AssistantCall &Call = m_Messages[m].Calls[c];
			if (Call.State == CALL_PROPOSED || Call.State == CALL_NEW) {
				Call.State = CALL_SKIPPED;
				Call.Result = "Not done: the user asked something else instead.";
			}
		}
	++m_iQuestion;
	m_iMode = Mode;
	m_iRounds = 0;
	m_iMostRounds = std::max(1, MostRounds);
	AssistantMessage M;
	M.Role = ASSISTANT_USER;
	M.Text = Trim(Question);
	M.Context = Context;
	M.Question = m_iQuestion;
	M.Mode = Mode;
	m_Messages.push_back(M);
	m_State = MODEL;
}

std::string AssistantChat::Build(const AssistantSetup &Setup) const
{
	Json Body = Json::MakeObject();
	if (!Setup.Model.empty())
		Body.Set("model", Setup.Model);
	Json Messages = Json::MakeArray();
	Json System = Json::MakeObject();
	System.Set("role", "system");
	System.Set("content", Setup.SystemPrompt);
	Messages.Push(System);
	for (size_t m = 0; m < m_Messages.size(); ++m) {
		const AssistantMessage &M = m_Messages[m];
		if (M.LeftOut || M.Role == ASSISTANT_NOTE)
			continue;
		Json Message = Json::MakeObject();
		if (M.Role == ASSISTANT_USER) {
			Message.Set("role", "user");
			Message.Set("content", M.ContextLeftOut || M.Context.empty() ? M.Text : M.Context + "\n\nThe question: " + M.Text);
			Messages.Push(Message);
			continue;
		}
		Message.Set("role", "assistant");
		Json Calls = Json::MakeArray();
		std::string Told;		// without tools, what the calls did, as text
		for (size_t c = 0; c < M.Calls.size(); ++c) {
			const AssistantCall &Call = M.Calls[c];
			if (Call.State == CALL_NEW || Call.State == CALL_PROPOSED)
				continue;
			if (m_bToolsOff) {
				Told += "\n(" + Call.Words + ": " + AssistantResultLine(Call.Result) + ")";
				continue;
			}
			Json Function = Json::MakeObject();
			Function.Set("name", Call.Name);
			Function.Set("arguments", Call.Arguments);
			Json Item = Json::MakeObject();
			Item.Set("id", Call.Id);
			Item.Set("type", "function");
			Item.Set("function", Function);
			Calls.Push(Item);
		}
		std::string Text = M.Text + Told;
		if (Text.empty() && Calls.Size() == 0)
			Text = M.Stopped ? "(the answer was cut short before it began)" : "(no answer)";
		Message.Set("content", Text);
		if (Calls.Size() > 0)
			Message.Set("tool_calls", Calls);
		Messages.Push(Message);
		for (size_t c = 0; c < M.Calls.size() && !m_bToolsOff; ++c) {
			const AssistantCall &Call = M.Calls[c];
			if (Call.State == CALL_NEW || Call.State == CALL_PROPOSED)
				continue;
			Json Result = Json::MakeObject();
			Result.Set("role", "tool");
			Result.Set("tool_call_id", Call.Id);
			Result.Set("name", Call.Name);
			Result.Set("content", Call.Result.empty() ? std::string("(no reply)") : Call.Result);
			Messages.Push(Result);
		}
	}
	Body.Set("messages", Messages);
	if (Setup.Functions.IsArray() && Setup.Functions.Size() > 0 && !m_bToolsOff)
		Body.Set("tools", Setup.Functions);
	Body.Set("stream", true);
	Json Options = Json::MakeObject();
	Options.Set("include_usage", true);
	Body.Set("stream_options", Options);
	return WriteJson(Body);
}

bool AssistantChat::Fits(const AssistantSetup &Setup, std::string &Body)
{
	const double Most = AssistantRequestRoom(Setup.ContextTokens, m_dBytesPerToken);	// bytes the request may take
	Body = Build(Setup);
	if (double(Body.size()) <= Most)
		return true;
	// Older questions' songs, then older tool replies, then whole older questions, then the question's own replies but
	// the last, then what's left cut to fit: each a step until it fits
	for (int Step = 0; Step < 5; ++Step) {
		bool Changed = true;
		while (Changed && double(Body.size()) > Most) {
			Changed = false;
			double Saved = 0;
			const double Excess = double(Body.size()) - Most;
			for (size_t m = 0; m < m_Messages.size() && Saved < Excess; ++m) {
				AssistantMessage &M = m_Messages[m];
				const bool Older = M.Question < m_iQuestion;
				if (M.LeftOut)
					continue;
				if (Step == 0 && Older && M.Role == ASSISTANT_USER && !M.ContextLeftOut && !M.Context.empty()) {
					M.ContextLeftOut = true;
					Saved += double(M.Context.size());
					Changed = true;
				}
				if ((Step == 1 && Older) || Step == 3)
					for (size_t c = 0; c < M.Calls.size() && Saved < Excess; ++c) {
						AssistantCall &Call = M.Calls[c];
						// the question's own last reply stays whole
						bool Last = false;
						if (Step == 3) {
							Last = true;
							for (size_t n = m; n < m_Messages.size() && Last; ++n)
								for (size_t d = (n == m ? c + 1 : 0); d < m_Messages[n].Calls.size(); ++d)
									if (!m_Messages[n].Calls[d].Result.empty())
										Last = false;
						}
						if (Last || Call.Shortened || Call.Result.size() <= ASSISTANT_SHORT_RESULT + 200)
							continue;
						const size_t Before = Call.Result.size();
						Call.Result = Shortened(Call.Result);
						Call.Shortened = true;
						Saved += double(Before - Call.Result.size());
						Changed = true;
					}
				if (Step == 4)
					for (size_t c = 0; c < M.Calls.size() && Saved < Excess; ++c) {
						AssistantCall &Call = M.Calls[c];
						if (Call.Result.size() <= ASSISTANT_SHORT_RESULT + 200)
							continue;
						const double Want = double(Call.Result.size()) - (Excess - Saved) - 256;
						const size_t Before = Call.Result.size();
						Call.Result = Cut(Call.Result, static_cast<size_t>(std::max<double>(ASSISTANT_SHORT_RESULT, Want))) +
							"\n(Cut here to fit in your context; ask for less, like fewer bars.)";
						Call.Shortened = true;
						if (Call.Result.size() < Before) {
							Saved += double(Before - Call.Result.size());
							Changed = true;
						}
					}
				if (Step == 2 && Older && M.Role == ASSISTANT_USER) {
					// the whole question, its answers and notes
					const int Question = M.Question;
					for (size_t n = m; n < m_Messages.size() && m_Messages[n].Question == Question; ++n)
						if (!m_Messages[n].LeftOut) {
							m_Messages[n].LeftOut = true;
							Saved += double(m_Messages[n].Text.size() + m_Messages[n].Context.size());
							for (size_t d = 0; d < m_Messages[n].Calls.size(); ++d)
								Saved += double(m_Messages[n].Calls[d].Result.size() + m_Messages[n].Calls[d].Arguments.size());
						}
					Changed = true;
				}
			}
			if (Changed)
				Body = Build(Setup);
		}
		if (double(Body.size()) <= Most)
			return true;
	}
	return false;
}

bool AssistantChat::Request(const AssistantSetup &Setup, std::string &Body, std::string &Problem)
{
	Problem.clear();
	if (m_State != MODEL)
		return false;
	for (size_t m = 0; m < m_Messages.size(); ++m)
		for (size_t c = 0; c < m_Messages[m].Calls.size(); ++c)
			if (m_Messages[m].Calls[c].State == CALL_NEW)
				return false;		// RunReady first
	if (!Fits(Setup, Body)) {
		AssistantSetup Bare = Setup;
		const int Context = std::max(ASSISTANT_LEAST_CONTEXT, Setup.ContextTokens);
		std::vector<AssistantMessage> Kept;
		Kept.swap(m_Messages);
		const double Alone = double(Build(Bare).size());
		Kept.swap(m_Messages);
		const long long Tokens = static_cast<long long>(Alone / m_dBytesPerToken);
		if (Alone > AssistantRequestRoom(Context, m_dBytesPerToken))
			Problem = "The model's context (" + Number(static_cast<long long>(Context)) + " tokens) is too small for "
				"FamiTracker's tools: they and their instructions take about " + Number(Tokens) + " tokens. Pick the small set of "
				"tools in the Ask panel's settings, or give the model a bigger context (llama-server's -c, like -c 65536).";
		else
			Problem = "This question has grown past what the model can read at once (" + Number(static_cast<long long>(Context)) +
				" tokens), even with the older parts shortened. Press New question to start over, or give the model a bigger "
				"context (llama-server's -c).";
		Note(Problem);
		m_State = IDLE;
		return false;
	}
	m_iSent = Body.size();
	return true;
}

void AssistantChat::Replied(const AssistantReply &Reply, bool Stopped, const std::string &Problem)
{
	Stopped = Stopped || !Problem.empty();
	++m_iRounds;
	if (Reply.PromptTokens > 0 && m_iSent > 0)
		m_dBytesPerToken = std::max(1.5, std::min(8.0, double(m_iSent) / Reply.PromptTokens));
	AssistantMessage M;
	M.Role = ASSISTANT_MODEL;
	M.Text = Reply.Content;
	M.Question = m_iQuestion;
	M.Mode = m_iMode;
	M.Stopped = Stopped;
	if (!Stopped && Reply.Error.empty())
		for (size_t i = 0; i < Reply.Calls.size(); ++i) {
			AssistantCall Call = Reply.Calls[i];
			Call.Name = Trim(Call.Name);
			if (Call.Id.empty())
				Call.Id = OwnId(++m_iCalls);
			Call.Title = Call.Name;
			bool Offered = false;
			for (size_t t = 0; t < m_Titles.size(); ++t)
				if (m_Titles[t].first == Call.Name) {
					Call.Title = m_Titles[t].second;
					Offered = true;
				}
			Json Args;
			std::string Error;
			if (Trim(Call.Arguments).empty())
				Call.Arguments = "{}";
			if (!ParseJson(Call.Arguments, Args, &Error) || !Args.IsObject()) {
				Call.State = CALL_FAILED;
				Call.Words = Call.Title;
				Call.Result = "The arguments weren't a JSON object (" + (Error.empty() ? std::string("not an object") : Error) +
					"), so nothing was done. Call it again with an object, like {\"channel\": \"Pulse 1\"}.";
				Call.Arguments = "{}";		// templates read the arguments as JSON
			}
			else if (!Offered || m_bToolsOff) {
				Call.State = CALL_FAILED;
				Call.Words = Call.Title;
				Call.Result = "There's no tool called \"" + Call.Name + "\" here, so nothing was done. Use one of the tools you were given.";
			}
			else {
				Call.Arguments = WriteJson(Args);
				Call.Changes = AssistantCallChanges(Call.Name, Args);
				Call.Words = AssistantCallWords(Call.Title, Args);
			}
			M.Calls.push_back(Call);
		}
	const bool Empty = M.Text.empty() && M.Calls.empty();
	if (!Empty || Stopped)
		m_Messages.push_back(M);
	if (Stopped) {
		Note(Problem.empty() ? std::string("Stopped.") : Problem);
		m_State = IDLE;
		return;
	}
	if (!Reply.Error.empty()) {
		m_State = IDLE;
		return;		// the panel tells the user, with what to check
	}
	if (M.Calls.empty()) {
		if (Reply.Finish == "length")
			Note("The answer was cut short: the model ran out of room. Ask it to go on, or press New question.");
		else if (Empty)
			Note("The model gave no answer. Ask again, perhaps in other words.");
		m_State = IDLE;
		return;
	}
	m_State = MODEL;		// RunReady runs the calls
}

void AssistantChat::Run(AssistantMessage &Message, AssistantCall &Call, AssistantTools &Tools)
{
	(void)Message;
	std::string Reply;
	bool Done = false;
	try {
		Done = Tools.RunTool(Call.Name, Call.Arguments, Call.Changes, Reply);
	}
	catch (...) {
		Done = false;
		Reply = "FamiTracker couldn't do that.";
	}
	Call.State = Done ? CALL_DONE : CALL_FAILED;
	if (Reply.size() > ASSISTANT_RESULT_MOST)
		Reply = Cut(Reply, ASSISTANT_RESULT_MOST) + "\n(Cut here: the rest is too long to read at once. Ask for less, like fewer "
			"bars.)";
	Call.Result = Reply.empty() ? std::string(Done ? "Done." : "It failed.") : Reply;
}

int AssistantChat::Waiting() const
{
	int Count = 0;
	for (size_t m = 0; m < m_Messages.size(); ++m)
		for (size_t c = 0; c < m_Messages[m].Calls.size(); ++c)
			if (m_Messages[m].Calls[c].State == CALL_PROPOSED)
				++Count;
	return Count;
}

void AssistantChat::AfterCalls()
{
	if (Waiting() > 0) {
		m_State = USER;
		return;
	}
	if (m_iRounds >= m_iMostRounds) {
		Note("That was " + Number(m_iRounds) + (m_iRounds == 1 ? " round" : " rounds") + " of tools, as many as a question may "
			"take in " + (m_iMode == ASSISTANT_LET_IT_WORK ? "Let it work" : "Ask first") + " (the Ask panel's settings change "
			"it). Ask again to go on.");
		m_State = IDLE;
		return;
	}
	m_State = MODEL;
}

void AssistantChat::RunReady(AssistantTools &Tools)
{
	if (m_Messages.empty())
		return;
	AssistantMessage &M = m_Messages.back();
	if (M.Role != ASSISTANT_MODEL || M.Question != m_iQuestion)
		return;
	bool Any = false;
	for (size_t c = 0; c < M.Calls.size(); ++c) {
		AssistantCall &Call = M.Calls[c];
		if (Call.State != CALL_NEW)
			continue;
		Any = true;
		if (!Call.Changes || m_iMode == ASSISTANT_LET_IT_WORK)
			Run(M, Call, Tools);
		else
			Call.State = CALL_PROPOSED;
		if (m_State == IDLE)
			break;		// stopped while it ran
	}
	if (m_State == IDLE) {
		for (size_t c = 0; c < M.Calls.size(); ++c)
			if (M.Calls[c].State == CALL_NEW || M.Calls[c].State == CALL_PROPOSED) {
				M.Calls[c].State = CALL_STOPPED;
				M.Calls[c].Result = "Not done: the user stopped.";
			}
		return;
	}
	if (Any || !M.Calls.empty())
		AfterCalls();
}

bool AssistantChat::Decide(size_t Message, size_t Call, bool Do, AssistantTools &Tools)
{
	if (Message >= m_Messages.size() || Call >= m_Messages[Message].Calls.size())
		return false;
	AssistantMessage &M = m_Messages[Message];
	AssistantCall &C = M.Calls[Call];
	if (C.State != CALL_PROPOSED)
		return false;
	if (Do)
		Run(M, C, Tools);
	else {
		C.State = CALL_SKIPPED;
		C.Result = "The user chose Skip, so this wasn't done. Don't call it again unless they ask for it; ask what they'd like "
			"instead.";
	}
	if (Waiting() > 0)
		return true;
	bool Done = false;
	for (size_t c = 0; c < M.Calls.size(); ++c)
		if (M.Calls[c].Changes && (M.Calls[c].State == CALL_DONE || M.Calls[c].State == CALL_FAILED))
			Done = true;
	if (Done)
		AfterCalls();
	else
		m_State = IDLE;		// all skipped: it's the user's turn to say what they'd like
	return true;
}

void AssistantChat::Stop()
{
	bool Waited = false;
	for (size_t m = 0; m < m_Messages.size(); ++m)
		for (size_t c = 0; c < m_Messages[m].Calls.size(); ++c) {
			AssistantCall &Call = m_Messages[m].Calls[c];
			if (Call.State == CALL_PROPOSED || Call.State == CALL_NEW) {
				Waited = Waited || Call.State == CALL_PROPOSED;
				Call.State = CALL_STOPPED;
				Call.Result = "Not done: the user stopped.";
			}
		}
	if (Waited)
		Note("Stopped: the proposals weren't done.");
	m_State = IDLE;
}

void AssistantChat::Offline(const std::string &Question, const std::string &Answer)
{
	++m_iQuestion;
	AssistantMessage Asked;
	Asked.Role = ASSISTANT_USER;
	Asked.Text = Trim(Question);
	Asked.Question = m_iQuestion;
	Asked.LeftOut = true;
	m_Messages.push_back(Asked);
	AssistantMessage Answered;
	Answered.Role = ASSISTANT_MODEL;
	Answered.Text = Answer;
	Answered.Question = m_iQuestion;
	Answered.LeftOut = true;
	m_Messages.push_back(Answered);
	m_State = IDLE;
}

void AssistantChat::Note(const std::string &Text)
{
	AssistantMessage M;
	M.Role = ASSISTANT_NOTE;
	M.Text = Text;
	M.Question = m_iQuestion;
	m_Messages.push_back(M);
}

const AssistantMessage *AssistantChat::Answer() const
{
	for (size_t m = m_Messages.size(); m-- > 0; ) {
		if (m_Messages[m].Question != m_iQuestion || m_Messages[m].Role == ASSISTANT_USER)
			return 0;
		if (m_Messages[m].Role == ASSISTANT_MODEL)
			return &m_Messages[m];
	}
	return 0;
}

// ---- Help without a model

std::vector<AssistantHelpItem> AssistantSearchHelp(const std::string &Question, const std::vector<AssistantTutorial> &Tutorials,
	size_t Most)
{
	std::vector<AssistantHelpItem> Items;
	const std::vector<std::string> Words = QuestionWords(Question);
	if (Words.empty())
		return Items;
	// The glossary: a word or two words together that name an entry count most, then the entries each word finds
	std::vector<std::pair<const GlossaryEntry *, int> > Found;
	struct Adder {
		std::vector<std::pair<const GlossaryEntry *, int> > &Found;
		void operator()(const GlossaryEntry *pEntry, int Score) const
		{
			for (size_t i = 0; i < Found.size(); ++i)
				if (Found[i].first == pEntry) {
					Found[i].second += Score;
					return;
				}
			Found.push_back(std::make_pair(pEntry, Score));
		}
	} Add = { Found };
	for (size_t i = 0; i < Words.size(); ++i) {
		const GlossaryEntry *pNamed = FindGlossaryEntry(Words[i]);
		if (pNamed != 0)
			Add(pNamed, 6);
		if (i + 1 < Words.size()) {
			const GlossaryEntry *pPair = FindGlossaryEntry(Words[i] + " " + Words[i + 1]);
			if (pPair != 0)
				Add(pPair, 9);
		}
		const std::vector<const GlossaryEntry *> Matches = SearchGlossary(Words[i]);
		for (size_t k = 0; k < Matches.size() && k < 3; ++k)
			Add(Matches[k], 3 - static_cast<int>(k));
	}
	for (size_t i = 0; i < Found.size(); ++i) {
		AssistantHelpItem Item;
		Item.Kind = "glossary";
		Item.Id = Found[i].first->Id;
		Item.Title = Found[i].first->Term;
		Item.Line = GlossaryFirstSentence(*Found[i].first);
		Item.Score = Found[i].second + CountIn(Found[i].first->Term + " " + Found[i].first->Text, Words);
		Items.push_back(Item);
	}
	// The exercises and the tutorials, by the words in their titles (twice) and their texts
	const std::vector<ExerciseInfo> &List = Exercises();
	for (size_t i = 0; i < List.size(); ++i) {
		const int Score = 3 * CountIn(List[i].Title, Words) + 2 * CountIn(List[i].About, Words);
		if (Score <= 0)
			continue;
		AssistantHelpItem Item;
		Item.Kind = "exercise";
		Item.Id = List[i].Id;
		Item.Title = List[i].Title;
		Item.Line = List[i].About;
		Item.Score = Score + 2;
		Items.push_back(Item);
	}
	for (size_t i = 0; i < Tutorials.size(); ++i) {
		const int Score = 3 * CountIn(Tutorials[i].Title, Words) + CountIn(Tutorials[i].Text, Words);
		if (Score < 2)
			continue;
		AssistantHelpItem Item;
		Item.Kind = "tutorial";
		Item.Id = Tutorials[i].Title;
		Item.Title = Tutorials[i].Title;
		Item.Line = AssistantResultLine(Tutorials[i].Text, 140);
		Item.Score = Score;
		Items.push_back(Item);
	}
	std::stable_sort(Items.begin(), Items.end(), [](const AssistantHelpItem &a, const AssistantHelpItem &b) {
		return a.Score > b.Score;
	});
	// At most four of the glossary's, two exercises and two tutorials
	std::vector<AssistantHelpItem> Out;
	int Glossary = 0, Exercise = 0, Tutorial = 0;
	for (size_t i = 0; i < Items.size() && Out.size() < Most; ++i) {
		int &Count = Items[i].Kind == "glossary" ? Glossary : Items[i].Kind == "exercise" ? Exercise : Tutorial;
		if (Count >= (Items[i].Kind == "glossary" ? 4 : 2))
			continue;
		++Count;
		Out.push_back(Items[i]);
	}
	return Out;
}

std::string AssistantHelpAnswer(const std::vector<AssistantHelpItem> &Items, bool Configured)
{
	std::string Text = Configured ? "Your model couldn't be reached, so here's what FamiTracker's own help has on it:" :
		"No model is set up yet, so here's what FamiTracker's own help has on it:";
	if (Items.empty())
		Text = Configured ? "Your model couldn't be reached, and nothing in the glossary, the exercises or the tutorials matches "
			"those words. Try other words, or look in Help > Glossary." : "Nothing in the glossary, the exercises or the "
			"tutorials matches those words. Try other words, or look in Help > Glossary.";
	for (size_t i = 0; i < Items.size(); ++i) {
		const AssistantHelpItem &Item = Items[i];
		Text += "\n- ";
		if (Item.Kind == "exercise")
			Text += "Exercise: [[" + Item.Title + "|exercise:" + Item.Id + "]]: " + Item.Line;
		else if (Item.Kind == "tutorial")
			Text += "Tutorial: [[" + Item.Title + "|tutorial:" + Item.Id + "]]: " + Item.Line;
		else
			Text += "[[" + Item.Title + "|" + Item.Id + "]]: " + Item.Line;
	}
	if (!Configured)
		Text += "\n\nTo ask your own model instead, open the [[settings|settings]] and give the address of its server: "
			"llama.cpp's llama-server, llama-swap, Ollama or LM Studio, on this PC or your home network.";
	return Text;
}

}	// namespace ftapi
