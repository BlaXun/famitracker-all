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

// The Ask panel (see AskPanel.h)

// Standard headers come before stdafx.h, whose DEBUG_NEW macro would
// otherwise rewrite the placement new inside the standard library.
#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "stdafx.h"
#pragma push_macro("new")
#undef new
#include <afxpriv.h>		// WM_SIZEPARENT, for laying out the docked bar
#pragma pop_macro("new")
#include <wincrypt.h>		// the key kept with DPAPI
#include "Api/Assistant.h"
#include "Api/AssistantSocket.h"
#include "Api/Exercises.h"
#include "Api/Http.h"
#include "Api/Music.h"
#include "Api/Network.h"
#include "Api/Tools.h"
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "FamiTrackerView.h"
#include "MainFrm.h"
#include "PatternEditor.h"
#include "Action.h"
#include "InsertRecording.h"
#include "McpConnection.h"
#include "McpSocket.h"
#include "GlossaryDlg.h"
#include "Tutorial.h"
#include "WhatNextPanel.h"
#include "TutorialScript.h"
#include "AskPanel.h"

#ifdef _MSC_VER
#pragma comment(lib, "crypt32.lib")
#endif

namespace {

LPCTSTR SECTION = _T("Ask panel");			// registry section: open, where, its size
LPCTSTR SETTINGS = _T("Ask");				// the model's settings and the mode

// Sizes at 96 DPI (scaled with SX and SY)
const int DEFAULT_HEIGHT = 220;				// along the bottom
const int DEFAULT_WIDTH = 380;				// along the right
const int MIN_HEIGHT = 110;
const int ROW_LEAST = 170;					// beside the others along the bottom: a few lines and the question box
const int MIN_WIDTH = 260;
const int MARGIN = 8;
const int TOP_ROW = 28;						// the mode, New question, Settings
const int QUESTION_HEIGHT = 48;				// the question box, two lines and a bit
const int SIDE_BUTTON = 66;					// Send and Stop
const int BUTTON_HEIGHT = 22;
const int LINE = 16;						// for scrolling

const UINT WM_ASK_JOB = WM_APP + 0x341;		// a job's event (ask_event_t)
const UINT_PTR TMR_TICK = 1;				// while a job runs: the seconds it's taken, and a reply waiting for a dialog
const UINT TICK_MS = 500;
const UINT_PTR TMR_LOOK = 2;				// another song open?
const UINT LOOK_MS = 1000;
const UINT_PTR TMR_PAINT = 3;				// the text streamed in, shown
const UINT PAINT_MS = 40;

// Colours, as the What next panel's
const COLORREF PANEL_BACK = RGB(232, 234, 238);
const COLORREF BODY_BACK = RGB(255, 255, 255);
const COLORREF TEXT_DARK = RGB(30, 32, 40);
const COLORREF TEXT_DIM = RGB(100, 104, 112);
const COLORREF LINK_COLOUR = RGB(28, 92, 196);
const COLORREF QUESTION_BACK = RGB(226, 236, 251);
const COLORREF PROPOSAL_BACK = RGB(255, 248, 226);
const COLORREF PROPOSAL_BORDER = RGB(226, 196, 120);
const COLORREF DONE_COLOUR = RGB(30, 110, 50);
const COLORREF FAILED_COLOUR = RGB(170, 40, 40);
const COLORREF NOTE_COLOUR = RGB(150, 90, 20);
const COLORREF BUTTON_BACK = RGB(250, 251, 253);
const COLORREF BUTTON_BORDER = RGB(160, 166, 178);
const COLORREF BUTTON_HOT = RGB(60, 120, 215);
const COLORREF PRIMARY_BACK = RGB(60, 120, 215);
const COLORREF PRIMARY_HOT = RGB(40, 96, 190);

// What the panel's changes are labelled with, after their words (Edit > Undo says it; Take back all looks for it)
const char ASK_LABEL[] = " (the Ask panel)";

// What a click in the conversation does
enum action_t { ACT_LINK, ACT_DO, ACT_SKIP, ACT_TAKE_BACK };

// The fonts of the conversation
enum font_t { F_TEXT, F_BOLD, F_LINK, F_BOLD_LINK, F_SMALL, F_SMALL_BOLD, F_SMALL_LINK, F_ITALIC, F_CODE, F_COUNT };

// The jobs on their own threads, and their events
enum job_kind_t { JOB_CHAT, JOB_PROPS, JOB_TEST, JOB_RESOLVE };
enum event_t { EV_STARTED, EV_TEXT, EV_DONE, EV_NEXT_ROUND };

// The phases of a question
enum phase_t { PHASE_IDLE, PHASE_PROPS, PHASE_CHAT };

// Text from the tools (UTF-8) as FamiTracker shows it (the Windows code page), and back
CString FromUtf8(const std::string &Text)
{
	if (Text.empty())
		return CString();
	const int Wide = ::MultiByteToWideChar(CP_UTF8, 0, Text.c_str(), static_cast<int>(Text.size()), NULL, 0);
	if (Wide <= 0)
		return CString(Text.c_str());
	std::vector<wchar_t> Buffer(static_cast<size_t>(Wide));
	::MultiByteToWideChar(CP_UTF8, 0, Text.c_str(), static_cast<int>(Text.size()), &Buffer[0], Wide);
	const int Bytes = ::WideCharToMultiByte(CP_ACP, 0, &Buffer[0], Wide, NULL, 0, NULL, NULL);
	if (Bytes <= 0)
		return CString(Text.c_str());
	std::vector<char> Out(static_cast<size_t>(Bytes) + 1, 0);
	::WideCharToMultiByte(CP_ACP, 0, &Buffer[0], Wide, &Out[0], Bytes, NULL, NULL);
	return CString(&Out[0]);
}

std::string ToUtf8(const CString &Text)
{
	if (Text.IsEmpty())
		return std::string();
	const int Wide = ::MultiByteToWideChar(CP_ACP, 0, Text, Text.GetLength(), NULL, 0);
	if (Wide <= 0)
		return std::string(static_cast<LPCSTR>(Text));
	std::vector<wchar_t> Buffer(static_cast<size_t>(Wide));
	::MultiByteToWideChar(CP_ACP, 0, Text, Text.GetLength(), &Buffer[0], Wide);
	const int Bytes = ::WideCharToMultiByte(CP_UTF8, 0, &Buffer[0], Wide, NULL, 0, NULL, NULL);
	if (Bytes <= 0)
		return std::string(static_cast<LPCSTR>(Text));
	std::string Out(static_cast<size_t>(Bytes), '\0');
	::WideCharToMultiByte(CP_UTF8, 0, &Buffer[0], Wide, &Out[0], Bytes, NULL, NULL);
	return Out;
}

// UTF-8 to the wide text the conversation is drawn in (the model writes what it likes: dashes, quotes, accents)
std::wstring Wide(const std::string &Text)
{
	if (Text.empty())
		return std::wstring();
	const int Count = ::MultiByteToWideChar(CP_UTF8, 0, Text.c_str(), static_cast<int>(Text.size()), NULL, 0);
	if (Count <= 0)
		return std::wstring();
	std::wstring Out(static_cast<size_t>(Count), L'\0');
	::MultiByteToWideChar(CP_UTF8, 0, Text.c_str(), static_cast<int>(Text.size()), &Out[0], Count);
	return Out;
}

std::string Number(long long Value)
{
	char Text[32];
	sprintf_s(Text, "%lld", Value);
	return Text;
}

// The key, kept with DPAPI for the Windows account (never as plain text)
std::string ProtectKey(const std::string &Key)
{
	if (Key.empty())
		return std::string();
	DATA_BLOB In, Out;
	In.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(Key.data()));
	In.cbData = static_cast<DWORD>(Key.size());
	memset(&Out, 0, sizeof(Out));
	if (!::CryptProtectData(&In, L"FamiTracker Ask panel", NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &Out))
		return std::string();
	const std::string Bytes(reinterpret_cast<const char*>(Out.pbData), Out.cbData);
	::LocalFree(Out.pbData);
	return ftapi::Base64Encode(Bytes);
}

std::string UnprotectKey(const std::string &Stored)
{
	std::string Bytes;
	if (Stored.empty() || !ftapi::Base64Decode(Stored, Bytes) || Bytes.empty())
		return std::string();
	DATA_BLOB In, Out;
	In.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(Bytes.data()));
	In.cbData = static_cast<DWORD>(Bytes.size());
	memset(&Out, 0, sizeof(Out));
	if (!::CryptUnprotectData(&In, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &Out))
		return std::string();
	const std::string Key(reinterpret_cast<const char*>(Out.pbData), Out.cbData);
	::SecureZeroMemory(Out.pbData, Out.cbData);
	::LocalFree(Out.pbData);
	return Key;
}

ftapi::AssistantSettings ReadSettings()
{
	ftapi::AssistantSettings S;
	S.Endpoint = ToUtf8(theApp.GetProfileString(SETTINGS, _T("Address"), _T("")));
	S.Model = ToUtf8(theApp.GetProfileString(SETTINGS, _T("Model"), _T("")));
	S.Key = UnprotectKey(ToUtf8(theApp.GetProfileString(SETTINGS, _T("Key"), _T(""))));
	S.SmallSet = theApp.GetProfileInt(SETTINGS, _T("Small set"), 0) != 0;
	S.AskRounds = static_cast<int>(theApp.GetProfileInt(SETTINGS, _T("Ask first rounds"), ftapi::ASSISTANT_ASK_ROUNDS));
	S.WorkRounds = static_cast<int>(theApp.GetProfileInt(SETTINGS, _T("Let it work rounds"), ftapi::ASSISTANT_WORK_ROUNDS));
	S.ContextTokens = static_cast<int>(theApp.GetProfileInt(SETTINGS, _T("Context"), 0));
	return ftapi::SaneAssistantSettings(S);
}

// False when there's a key Windows couldn't keep encrypted: it isn't saved (never as plain text)
bool WriteSettings(const ftapi::AssistantSettings &S)
{
	const std::string Kept = ProtectKey(S.Key);
	theApp.WriteProfileString(SETTINGS, _T("Address"), FromUtf8(S.Endpoint));
	theApp.WriteProfileString(SETTINGS, _T("Model"), FromUtf8(S.Model));
	theApp.WriteProfileString(SETTINGS, _T("Key"), CString(Kept.c_str()));
	theApp.WriteProfileInt(SETTINGS, _T("Small set"), S.SmallSet ? 1 : 0);
	theApp.WriteProfileInt(SETTINGS, _T("Ask first rounds"), S.AskRounds);
	theApp.WriteProfileInt(SETTINGS, _T("Let it work rounds"), S.WorkRounds);
	theApp.WriteProfileInt(SETTINGS, _T("Context"), S.ContextTokens);
	return S.Key.empty() || !Kept.empty();
}

// ---- Jobs: the model's server is asked on threads of their own, which tell the window by messages

std::atomic<int> g_JobSerial(0);

// A job's event as its message carries it: the event and the job's serial (a job's address may be another's later)
WPARAM EventParam(int Event, int Serial)
{
	return static_cast<WPARAM>(Event) | (static_cast<WPARAM>(Serial) << 4);
}

struct ask_job_t {
	std::atomic<bool> Stop;
	int Serial;
	std::mutex Lock;
	HWND hWnd;						// told of its events; NULL once the window is gone
	int Kind;						// job_kind_t
	ftapi::AssistantEndpoint Where;
	std::string Key, Model, Body;
	ftapi::AssistantTimeouts Times;
	size_t ToolsBytes[2];			// Test connection: the tools' request with all of them, and with the small set
	// What came, as it comes (under Lock)
	std::string Text, Thinking;
	bool Told;						// an EV_TEXT is on its way
	// What came back, read once EV_DONE comes
	ftapi::assistant_net_t Net;
	ftapi::AssistantReply Reply;
	std::string Error;
	int Context;
	std::vector<std::string> Models;
	std::string Report;
	std::string Address;
	bool Started;
	int Status;

	ask_job_t() : Stop(false), Serial(0), hWnd(NULL), Kind(JOB_CHAT), Told(false), Net(ftapi::NET_DONE), Context(0), Started(false), Status(0)
	{
		ToolsBytes[0] = ToolsBytes[1] = 0;
	}
	void Tell(int Event)
	{
		std::lock_guard<std::mutex> Hold(Lock);
		if (hWnd != NULL)
			::PostMessage(hWnd, WM_ASK_JOB, EventParam(Event, Serial), reinterpret_cast<LPARAM>(this));
	}
	void Detach()
	{
		std::lock_guard<std::mutex> Hold(Lock);
		hWnd = NULL;
		Stop = true;
	}
};

typedef std::shared_ptr<ask_job_t> job_ptr;

// The streamed text, passed on to the window a piece at a time (one message on its way at most)
class JobListener : public ftapi::AssistantStreamListener {
public:
	explicit JobListener(ask_job_t &Job) : m_Job(Job) {}
	void Started(int Status)
	{
		{
			std::lock_guard<std::mutex> Hold(m_Job.Lock);
			m_Job.Started = true;
			m_Job.Status = Status;
		}
		m_Job.Tell(EV_STARTED);
	}
	void Streamed(const std::string &NewText, const std::string &NewThinking)
	{
		bool Tell = false;
		{
			std::lock_guard<std::mutex> Hold(m_Job.Lock);
			m_Job.Text += NewText;
			m_Job.Thinking += NewThinking;
			Tell = !m_Job.Told;
			m_Job.Told = true;
		}
		if (Tell)
			m_Job.Tell(EV_TEXT);
	}

private:
	ask_job_t &m_Job;
};

// The context the server reads at once: llama.cpp's /props, through llama-swap's /upstream/<model>/props when it isn't
// llama.cpp itself; 0 when it doesn't say
int FetchContext(ask_job_t &Job)
{
	ftapi::AssistantTimeouts Times;
	Times.FirstMs = 30000;
	Times.QuietMs = 30000;
	ftapi::AssistantAnswer Answer;
	std::string Error;
	if (ftapi::AssistantFetch(Job.Where, Job.Where.Root + "/props", Job.Key, std::string(), Times, &Job.Stop, Answer, Error) ==
		ftapi::NET_DONE && Answer.Status == 200) {
		const int Context = ftapi::AssistantContextOfProps(Answer.Body);
		if (Context > 0)
			return Context;
	}
	if (!Job.Model.empty() && !Job.Stop &&
		ftapi::AssistantFetch(Job.Where, Job.Where.Root + "/upstream/" + Job.Model + "/props", Job.Key, std::string(), Times, &Job.Stop,
		Answer, Error) == ftapi::NET_DONE && Answer.Status == 200)
		return ftapi::AssistantContextOfProps(Answer.Body);
	return 0;
}

std::vector<std::string> FetchModels(ask_job_t &Job, ftapi::assistant_net_t &Net, int &Status, std::string &Error)
{
	ftapi::AssistantTimeouts Times;
	Times.FirstMs = 30000;
	ftapi::AssistantAnswer Answer;
	Net = ftapi::AssistantFetch(Job.Where, Job.Where.ModelsPath(), Job.Key, std::string(), Times, &Job.Stop, Answer, Error);
	Status = Answer.Status;
	if (Net == ftapi::NET_DONE && Answer.Status >= 400)
		Error = ftapi::AssistantErrorMessage(Answer.Body);
	return Net == ftapi::NET_DONE && Answer.Status == 200 ? ftapi::AssistantModelsOf(Answer.Body) : std::vector<std::string>();
}

// Test connection: reaching the server, its models, its context and whether it takes tools, in plain words
std::string TestConnection(ask_job_t &Job)
{
	std::string Report, Error, Address;
	if (!ftapi::AssistantResolve(Job.Where.Host, Address))
		return ftapi::AssistantNetProblem(ftapi::NET_NOT_FOUND, Job.Where, Job.Times, std::string());
	ftapi::assistant_net_t Net = ftapi::NET_DONE;
	int Status = 0;
	const std::vector<std::string> Models = FetchModels(Job, Net, Status, Error);
	if (Net == ftapi::NET_STOPPED)
		return std::string();
	if (Net != ftapi::NET_DONE)
		return ftapi::AssistantNetProblem(Net, Job.Where, Job.Times, Error);
	{
		std::lock_guard<std::mutex> Hold(Job.Lock);
		Job.Models = Models;
	}
	Report = "Connected to " + Job.Where.Host + (Address != Job.Where.Host ? " (" + Address + ")" : std::string()) + ", port " +
		Number(Job.Where.Port) + ".";
	if (Status == 401 || Status == 403)
		return Report + "\r\n" + ftapi::AssistantHttpProblem(Job.Where, Job.Model, Status, Error);
	std::string Names;
	for (size_t i = 0; i < Models.size() && i < 8; ++i)
		Names += (i == 0 ? "" : ", ") + Models[i];
	if (!Models.empty())
		Report += "\r\nModels: " + Names + (Models.size() > 8 ? ", ..." : "") + ".";
	if (!Job.Model.empty() && !Models.empty() && std::find(Models.begin(), Models.end(), Job.Model) == Models.end())
		Report += "\r\nIt lists no model called \"" + Job.Model + "\": check the name.";
	if (Job.Model.empty() && Models.size() > 1)
		Report += "\r\nNo model is named here, so FamiTracker asks " + Models[0] + ", the first it lists.";
	ask_job_t &J = Job;
	if (J.Model.empty() && !Models.empty())
		J.Model = Models[0];
	const int Context = FetchContext(J);
	if (Context > 0) {
		Report += "\r\nIt reads " + Number(Context) + " tokens at once (its context).";
		const long long All = static_cast<long long>(double(J.ToolsBytes[0]) / ftapi::ASSISTANT_BYTES_PER_TOKEN),
			Small = static_cast<long long>(double(J.ToolsBytes[1]) / ftapi::ASSISTANT_BYTES_PER_TOKEN);
		if (All > Context * 3 / 4)
			Report += " All of FamiTracker's tools take about " + Number(All) + " of them: pick the small set (about " + Number(Small) +
				"), or start the server with a bigger context (-c 65536 or more).";
	}
	else
		Report += "\r\nIt doesn't say how much it reads at once; FamiTracker counts on " + Number(ftapi::ASSISTANT_CONTEXT) +
			" tokens (set it here if yours differs; with Ollama, set num_ctx to that or more).";
	// A word, with one tool: does it take tools?
	ftapi::AssistantTimeouts Times = Job.Times;
	Times.FirstMs = 180000;		// llama-swap loads the model first
	ftapi::AssistantAnswer Answer;
	Net = ftapi::AssistantFetch(J.Where, J.Where.ChatPath(), J.Key, ftapi::AssistantProbeBody(J.Model), Times, &J.Stop, Answer, Error);
	if (Net == ftapi::NET_STOPPED)
		return std::string();
	if (Net != ftapi::NET_DONE)
		return Report + "\r\n" + ftapi::AssistantNetProblem(Net, J.Where, Times, Error);
	const std::string Said = Answer.Status >= 400 ? ftapi::AssistantErrorMessage(Answer.Body) : std::string();
	if (Answer.Status < 400)
		Report += "\r\nTools: on. Your model can read the song and, when you let it, change it.";
	else if (ftapi::AssistantToolsRefused(Answer.Status, Said))
		Report += "\r\nTools: off (it said: " + Said + "). For llama.cpp, start llama-server with --jinja; with Ollama or LM Studio, "
			"pick a model that can use tools. Without them it answers from the song's summary.";
	else
		Report += "\r\n" + ftapi::AssistantHttpProblem(J.Where, J.Model, Answer.Status, Said);
	return Report;
}

void RunJob(job_ptr Job)
{
	switch (Job->Kind) {
	case JOB_CHAT: {
		JobListener Listener(*Job);
		ftapi::AssistantReply Reply;
		std::string Error;
		const ftapi::assistant_net_t Net = ftapi::AssistantChatExchange(Job->Where, Job->Key, Job->Body, Job->Times, &Job->Stop,
			&Listener, Reply, Error);
		std::lock_guard<std::mutex> Hold(Job->Lock);
		Job->Net = Net;
		Job->Reply = Reply;
		Job->Error = Error;
		break;
	}
	case JOB_PROPS: {
		ftapi::assistant_net_t Net = ftapi::NET_DONE;
		int Status = 0;
		std::string Error;
		const std::vector<std::string> Models = FetchModels(*Job, Net, Status, Error);
		if (Job->Model.empty() && !Models.empty())
			Job->Model = Models[0];		// llama-swap's /upstream/<model>/props
		const int Context = Net == ftapi::NET_DONE ? FetchContext(*Job) : 0;
		std::lock_guard<std::mutex> Hold(Job->Lock);
		Job->Models = Models;
		Job->Context = Context;
		Job->Net = Net;
		Job->Error = Error;
		break;
	}
	case JOB_TEST: {
		const std::string Report = TestConnection(*Job);
		std::lock_guard<std::mutex> Hold(Job->Lock);
		Job->Report = Report;
		break;
	}
	case JOB_RESOLVE: {
		std::string Address;
		ftapi::AssistantResolve(Job->Where.Host, Address);
		std::lock_guard<std::mutex> Hold(Job->Lock);
		Job->Address = Address;
		break;
	}
	}
	Job->Tell(EV_DONE);
}

// Starts a job on a thread of its own; the window gets its events while it lives
job_ptr StartJob(HWND hWnd, int Kind, const ftapi::AssistantEndpoint &Where, const std::string &Key, const std::string &Model,
	const std::string &Body)
{
	job_ptr Job = std::make_shared<ask_job_t>();
	Job->Serial = ++g_JobSerial;
	Job->hWnd = hWnd;
	Job->Kind = Kind;
	Job->Where = Where;
	Job->Key = Key;
	Job->Model = Model;
	Job->Body = Body;
	std::thread([Job]() { RunJob(Job); }).detach();
	return Job;
}

// The built-in tutorials' titles and texts, for the help without a model
std::vector<ftapi::AssistantTutorial> BuiltInTutorials()
{
	static const UINT TUTORIALS[] = { IDR_TUTORIAL_FIRST, IDR_TUTORIAL_SOUNDS, IDR_TUTORIAL_CLIPS, IDR_TUTORIAL_GAME };
	std::vector<ftapi::AssistantTutorial> List;
	for (size_t i = 0; i < sizeof(TUTORIALS) / sizeof(TUTORIALS[0]); ++i) {
		std::string Text, Error;
		tutorial_t Tutorial;
		if (!ReadBuiltInText(MAKEINTRESOURCE(TUTORIALS[i]), Text) || !ParseTutorial(Text, Tutorial, Error))
			continue;
		ftapi::AssistantTutorial Entry;
		Entry.Title = Tutorial.Title;
		Entry.Text = Tutorial.About;
		for (size_t s = 0; s < Tutorial.Steps.size(); ++s)
			Entry.Text += "\n" + ftapi::UnmarkLinks(Tutorial.Steps[s].Text);
		List.push_back(Entry);
	}
	return List;
}

}	// namespace

// ---- The settings dialog

class CAskSettingsDlg : public CDialog
{
public:
	CAskSettingsDlg(CWnd *pParent, const ftapi::AssistantSettings &Settings, size_t AllBytes, size_t SmallBytes) :
		CDialog(IDD_ASK_SETTINGS, pParent), m_Settings(Settings), m_iAllBytes(AllBytes), m_iSmallBytes(SmallBytes) {}
	const ftapi::AssistantSettings &Settings() const { return m_Settings; }

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	virtual void OnCancel();
	afx_msg void OnTest();
	afx_msg void OnDestroy();
	afx_msg LRESULT OnJobMessage(WPARAM wParam, LPARAM lParam);
	DECLARE_MESSAGE_MAP()

private:
	bool Read(ftapi::AssistantSettings &Out, bool Quiet);
	void StopTest();

	ftapi::AssistantSettings m_Settings;
	size_t m_iAllBytes, m_iSmallBytes;
	job_ptr m_Test;
};

BEGIN_MESSAGE_MAP(CAskSettingsDlg, CDialog)
	ON_BN_CLICKED(IDC_ASK_TEST, OnTest)
	ON_WM_DESTROY()
	ON_MESSAGE(WM_ASK_JOB, OnJobMessage)
END_MESSAGE_MAP()

BOOL CAskSettingsDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	SetDlgItemText(IDC_ASK_ENDPOINT, FromUtf8(m_Settings.Endpoint));
	SetDlgItemText(IDC_ASK_MODEL, FromUtf8(m_Settings.Model));
	SetDlgItemText(IDC_ASK_KEY, FromUtf8(m_Settings.Key));
	CheckRadioButton(IDC_ASK_ALLTOOLS, IDC_ASK_SMALLSET, m_Settings.SmallSet ? IDC_ASK_SMALLSET : IDC_ASK_ALLTOOLS);
	SetDlgItemInt(IDC_ASK_ASKROUNDS, static_cast<UINT>(m_Settings.AskRounds), FALSE);
	SetDlgItemInt(IDC_ASK_WORKROUNDS, static_cast<UINT>(m_Settings.WorkRounds), FALSE);
	SetDlgItemInt(IDC_ASK_CONTEXT, static_cast<UINT>(m_Settings.ContextTokens), FALSE);
	SetDlgItemText(IDC_ASK_RESULT, _T(""));
	// Set here: a dialog's text in the resource stops at 256 characters
	SetDlgItemText(IDC_ASK_HELP, _T("Context: all the tools and their instructions take about 45,000 tokens (170 KB), the small set ")
		_T("about 25,000 (94 KB): give the model 64K or more for all, 32K or more for the small set. llama.cpp: start llama-server ")
		_T("with --jinja (tools) and --host 0.0.0.0, like: llama-server -m model.gguf --jinja -c 65536 --host 0.0.0.0 --port 8080. ")
		_T("Ollama (http://<computer>:11434): its default context is small and it cuts a longer question without a word, so set ")
		_T("OLLAMA_CONTEXT_LENGTH=65536 for ollama serve, or use a model with num_ctx; pick one that can use tools. The instructions ")
		_T("and tools go first, the same every time, the song after them, so the server keeps what it read (llama-server: ")
		_T("--cache-reuse 256; Ollama while the model stays loaded). Models that use tools well: Qwen3 and later, gpt-oss, GLM 4.5 ")
		_T("and later, Mistral Small 3.2."));
	return TRUE;
}

bool CAskSettingsDlg::Read(ftapi::AssistantSettings &Out, bool Quiet)
{
	CString Text;
	ftapi::AssistantSettings S = m_Settings;
	GetDlgItemText(IDC_ASK_ENDPOINT, Text);
	S.Endpoint = ToUtf8(Text);
	GetDlgItemText(IDC_ASK_MODEL, Text);
	S.Model = ToUtf8(Text);
	GetDlgItemText(IDC_ASK_KEY, Text);
	S.Key = ToUtf8(Text);
	S.SmallSet = IsDlgButtonChecked(IDC_ASK_SMALLSET) == BST_CHECKED;
	S.AskRounds = static_cast<int>(GetDlgItemInt(IDC_ASK_ASKROUNDS, NULL, FALSE));
	S.WorkRounds = static_cast<int>(GetDlgItemInt(IDC_ASK_WORKROUNDS, NULL, FALSE));
	S.ContextTokens = static_cast<int>(GetDlgItemInt(IDC_ASK_CONTEXT, NULL, FALSE));
	S = ftapi::SaneAssistantSettings(S);
	if (!S.Endpoint.empty()) {
		ftapi::AssistantEndpoint Where;
		std::string Error;
		if (!ftapi::ParseAssistantEndpoint(S.Endpoint, Where, Error)) {
			if (!Quiet) {
				AfxMessageBox(FromUtf8(Error), MB_ICONEXCLAMATION);
				GetDlgItem(IDC_ASK_ENDPOINT)->SetFocus();
			}
			return false;
		}
	}
	Out = S;
	return true;
}

void CAskSettingsDlg::OnTest()
{
	ftapi::AssistantSettings S;
	if (!Read(S, false))
		return;
	if (S.Endpoint.empty()) {
		SetDlgItemText(IDC_ASK_RESULT, _T("Give the address of your model's server first, like http://192.168.1.50:8080."));
		return;
	}
	StopTest();
	ftapi::AssistantEndpoint Where;
	std::string Error;
	ftapi::ParseAssistantEndpoint(S.Endpoint, Where, Error);
	m_Test = StartJob(GetSafeHwnd(), JOB_TEST, Where, S.Key, S.Model, std::string());
	m_Test->ToolsBytes[0] = m_iAllBytes;
	m_Test->ToolsBytes[1] = m_iSmallBytes;
	SetDlgItemText(IDC_ASK_RESULT, _T("Testing... (a server that loads the model first, like llama-swap, can take a minute)"));
	GetDlgItem(IDC_ASK_TEST)->EnableWindow(FALSE);
}

LRESULT CAskSettingsDlg::OnJobMessage(WPARAM wParam, LPARAM lParam)
{
	if (!m_Test || reinterpret_cast<ask_job_t*>(lParam) != m_Test.get() || static_cast<int>(wParam >> 4) != m_Test->Serial ||
		static_cast<int>(wParam & 15) != EV_DONE)
		return 0;
	std::string Report;
	std::vector<std::string> Models;
	{
		std::lock_guard<std::mutex> Hold(m_Test->Lock);
		Report = m_Test->Report;
		Models = m_Test->Models;
	}
	m_Test.reset();
	SetDlgItemText(IDC_ASK_RESULT, FromUtf8(Report));
	// The models it has, to pick from
	CComboBox *pModels = static_cast<CComboBox*>(GetDlgItem(IDC_ASK_MODEL));
	if (pModels != NULL && !Models.empty()) {
		CString Typed;
		pModels->GetWindowText(Typed);
		pModels->ResetContent();
		for (size_t i = 0; i < Models.size(); ++i)
			pModels->AddString(FromUtf8(Models[i]));
		pModels->SetWindowText(Typed);
	}
	GetDlgItem(IDC_ASK_TEST)->EnableWindow(TRUE);
	return 0;
}

void CAskSettingsDlg::StopTest()
{
	if (m_Test)
		m_Test->Detach();
	m_Test.reset();
}

void CAskSettingsDlg::OnOK()
{
	ftapi::AssistantSettings S;
	if (!Read(S, false))
		return;
	m_Settings = S;
	StopTest();
	CDialog::OnOK();
}

void CAskSettingsDlg::OnCancel()
{
	StopTest();
	CDialog::OnCancel();
}

void CAskSettingsDlg::OnDestroy()
{
	StopTest();
	CDialog::OnDestroy();
}

// ---- The conversation as the panel shows it

class CAskTalkWnd : public CWnd
{
public:
	CAskTalkWnd() : m_pImpl(NULL) {}
	bool Create(CWnd *pParent, CAskPanelWnd::Impl *pImpl);

protected:
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC *pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar *pScrollBar);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnMouseLeave();
	afx_msg BOOL OnSetCursor(CWnd *pWnd, UINT nHitTest, UINT message);
	afx_msg void OnContextMenu(CWnd *pWnd, CPoint point);
	afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
	afx_msg UINT OnGetDlgCode();
	DECLARE_MESSAGE_MAP()

private:
	CAskPanelWnd::Impl *m_pImpl;
};

struct CAskPanelWnd::Impl {
	// A song's conversation, kept while FamiTracker runs
	struct talk_t {
		ftapi::AssistantChat Chat;
		int RunQuestion;					// the Let it work question Take back all undoes (0 for none)...
		std::vector<CAction*> RunActions;	// ...and its changes, oldest first
		talk_t() : RunQuestion(0) {}
	};

	// What the conversation shows, laid out (as if nothing were scrolled)
	struct text_t {
		CRect Rect;
		std::wstring Text;
		int Font;
		COLORREF Colour;
	};
	struct box_t {
		CRect Rect;
		COLORREF Fill, Border;
	};
	struct hit_t {
		CRect Rect;
		int Action;							// action_t
		std::string Link;
		size_t Message, Call;
		std::wstring Label;					// a button's
		bool Primary;
	};

	CAskPanelWnd *pWnd;
	CMcpConnection *pTools;
	ftapi::AssistantSettings Settings;
	ftapi::AssistantEndpoint Where;
	bool HasEndpoint;
	std::string PrivacyNote;
	bool ServerAsked;						// its context and models asked, for these settings
	int ServerContext;						// as it said, 0 when it didn't
	std::vector<std::string> ServerModels;
	std::vector<ftapi::ToolInfo> ToolInfos;
	ftapi::Json Functions[2];				// all the tools, the small set
	bool HasFunctions[2];
	std::string Prompts[2];
	int Mode;								// assistant_mode_t

	std::map<std::string, std::unique_ptr<talk_t> > Talks;	// by song: its path, or "untitled" and its serial
	std::string SongKey;
	unsigned int SongSerial;
	talk_t *pTalk;

	// The question being worked on
	int Phase;								// phase_t
	job_ptr Job, Resolving;
	DWORD JobStart;
	bool StopAsked;
	bool Started, Thinking;					// the server answered; the model is thinking
	std::string Streaming;					// the reply as it comes
	ftapi::AssistantSetup Setup;
	bool Waiting;							// a reply waits for a dialog to close before its tools run
	ftapi::assistant_net_t WaitingNet;
	ftapi::AssistantReply WaitingReply;
	std::string WaitingError;

	// The controls and the conversation's view
	CComboBox ModeBox;
	CButton NewButton, SettingsButton, HelpButton, SendButton, StopButton;
	CEdit QuestionBox;
	CAskTalkWnd Talk;
	CFont Fonts[F_COUNT];
	CFont ControlFont;
	std::vector<text_t> Texts;
	std::vector<box_t> Boxes;
	std::vector<hit_t> Hits;
	int LaidWidth, Content, Scroll;
	bool Dirty, Follow;
	int Hover;
	bool Tracking;
	bool Open;

	Impl(CAskPanelWnd *p) : pWnd(p), pTools(NULL), HasEndpoint(false), ServerAsked(false), ServerContext(0),
		Mode(ftapi::ASSISTANT_ASK_FIRST), SongSerial(0), pTalk(NULL), Phase(PHASE_IDLE), JobStart(0), StopAsked(false), Started(false),
		Thinking(false), Waiting(false), WaitingNet(ftapi::NET_DONE), LaidWidth(-1), Content(0), Scroll(0), Dirty(true),
		Follow(true), Hover(-1), Tracking(false), Open(false)
	{
		HasFunctions[0] = HasFunctions[1] = false;
	}

	CMainFrame *Main() const { return static_cast<CMainFrame*>(AfxGetMainWnd()); }
	CFamiTrackerDoc *Doc() const
	{
		CMainFrame *pMain = Main();
		CFamiTrackerDoc *pDoc = pMain != NULL ? static_cast<CFamiTrackerDoc*>(pMain->GetActiveDocument()) : NULL;
		return pDoc != NULL && pDoc->IsFileLoaded() ? pDoc : NULL;
	}

	void MakeFonts()
	{
		LOGFONT Log;
		memset(&Log, 0, sizeof(Log));
		::GetObject(::GetStockObject(DEFAULT_GUI_FONT), sizeof(Log), &Log);
		_tcscpy_s(Log.lfFaceName, LF_FACESIZE, _T("Segoe UI"));
		struct { int Font, Height, Weight; bool Underline, Italic; } const KINDS[] = {
			{ F_TEXT, 13, FW_NORMAL, false, false }, { F_BOLD, 13, FW_BOLD, false, false }, { F_LINK, 13, FW_NORMAL, true, false },
			{ F_BOLD_LINK, 13, FW_BOLD, true, false }, { F_SMALL, 11, FW_NORMAL, false, false },
			{ F_SMALL_BOLD, 11, FW_BOLD, false, false }, { F_SMALL_LINK, 11, FW_NORMAL, true, false },
			{ F_ITALIC, 11, FW_NORMAL, false, true }, { F_CODE, 12, FW_NORMAL, false, false },
		};
		for (size_t i = 0; i < sizeof(KINDS) / sizeof(KINDS[0]); ++i) {
			Log.lfHeight = -SY(KINDS[i].Height);
			Log.lfWeight = KINDS[i].Weight;
			Log.lfUnderline = KINDS[i].Underline ? TRUE : FALSE;
			Log.lfItalic = KINDS[i].Italic ? TRUE : FALSE;
			if (KINDS[i].Font == F_CODE)
				_tcscpy_s(Log.lfFaceName, LF_FACESIZE, _T("Consolas"));
			Fonts[KINDS[i].Font].CreateFontIndirect(&Log);
			_tcscpy_s(Log.lfFaceName, LF_FACESIZE, _T("Segoe UI"));
		}
		Log.lfHeight = -SY(12);
		Log.lfWeight = FW_NORMAL;
		Log.lfUnderline = FALSE;
		Log.lfItalic = FALSE;
		ControlFont.CreateFontIndirect(&Log);
	}

	// ---- Settings and the server

	void ApplySettings(const ftapi::AssistantSettings &S)
	{
		Settings = S;
		std::string Error;
		HasEndpoint = !S.Endpoint.empty() && ftapi::ParseAssistantEndpoint(S.Endpoint, Where, Error);
		ServerAsked = false;
		ServerContext = 0;
		ServerModels.clear();
		PrivacyNote.clear();
		if (Resolving)
			Resolving->Detach();
		Resolving.reset();
		if (HasEndpoint) {
			unsigned long Address = 0;
			if (ftapi::ParseIpv4(Where.Host, Address))
				PrivacyNote = ftapi::AssistantPrivacyNote(Where, Where.Host, OwnAddresses());
			else
				Resolving = StartJob(pWnd->GetSafeHwnd(), JOB_RESOLVE, Where, std::string(), std::string(), std::string());
		}
		Changed();
	}

	static std::vector<std::string> OwnAddresses()
	{
		std::vector<std::string> Own;
		const std::vector<mcp_address_t> List = ListNetworkAddresses();
		for (size_t i = 0; i < List.size(); ++i)
			Own.push_back(List[i].Address);
		return Own;
	}

	const ftapi::Json &FunctionsFor(bool Small)
	{
		const int i = Small ? 1 : 0;
		if (!HasFunctions[i]) {
			Functions[i] = ftapi::AssistantFunctions(ToolInfos, Small);
			HasFunctions[i] = true;
		}
		return Functions[i];
	}

	const std::string &PromptFor(bool Small)
	{
		const int i = Small ? 1 : 0;
		if (Prompts[i].empty())
			Prompts[i] = ftapi::AssistantSystemPrompt(Small, true);
		return Prompts[i];
	}

	std::string TitleOf(const std::string &Tool) const
	{
		for (size_t i = 0; i < ToolInfos.size(); ++i)
			if (ToolInfos[i].Name == Tool)
				return ToolInfos[i].Title;
		return Tool;
	}

	std::string ModelName() const
	{
		return !Settings.Model.empty() ? Settings.Model : ServerModels.empty() ? std::string() : ServerModels[0];
	}

	// ---- The song's conversation

	std::string KeyOfSong(unsigned int &Serial) const
	{
		CFamiTrackerDoc *pDoc = Doc();
		if (pDoc == NULL) {
			Serial = 0;
			return "none";
		}
		Serial = pDoc->GetSongSerial();
		const CString Path = pDoc->GetPathName();
		return Path.IsEmpty() ? "untitled " + Number(Serial) : ToUtf8(Path);
	}

	void FollowSong()
	{
		unsigned int Serial = 0;
		const std::string Key = KeyOfSong(Serial);
		if (pTalk != NULL && Key == SongKey)
			return;
		// An untitled song just saved keeps its conversation under its new name
		if (pTalk != NULL && Serial == SongSerial && Serial != 0 && SongKey.compare(0, 9, "untitled ") == 0 && Talks.count(SongKey) > 0) {
			Talks[Key] = std::move(Talks[SongKey]);
			Talks.erase(SongKey);
			SongKey = Key;
			pTalk = Talks[Key].get();
			return;
		}
		StopJob();
		if (pTalk != NULL && pTalk->Chat.State() == ftapi::AssistantChat::USER)
			pTalk->Chat.Stop();
		std::unique_ptr<talk_t> &Slot = Talks[Key];
		if (!Slot)
			Slot.reset(new talk_t());
		SongKey = Key;
		SongSerial = Serial;
		pTalk = Slot.get();
		Follow = true;
		Changed();
	}

	// ---- Asking

	// The song as the question is asked: get_song's overview, what_next's step (for the kind chosen in What next, which the
	// model is told to pass on), the exercise and the cursor
	std::string Context(bool WithTools)
	{
		ftapi::AssistantFacts Facts;
		Facts.Mode = Mode;
		Facts.WithTools = WithTools;
		Facts.Kind = WhatNextChosenKind();	// the kind chosen in What next for this track, which its step follows too
		std::string Reply;
		if (pTools != NULL) {
			if (pTools->RunTool("get_song", "{}", Reply, false, Settings.SmallSet))
				Facts.Song = Reply;
			if (pTools->RunTool("what_next", Facts.Kind.empty() ? std::string("{}") : "{\"kind\": \"" + Facts.Kind + "\"}", Reply, false,
				Settings.SmallSet))
				Facts.Next = Reply;
		}
		ExerciseInProgress(Facts.Exercise, Facts.ExerciseStep, Facts.ExerciseCheck);
		CFamiTrackerDoc *pDoc = Doc();
		CFamiTrackerView *pView = CFamiTrackerView::GetView();
		CMainFrame *pMain = Main();
		if (pDoc != NULL && pView != NULL && pMain != NULL && pView->GetPatternEditor() != NULL) {
			CPatternEditor *pEditor = pView->GetPatternEditor();
			const unsigned int Track = static_cast<unsigned int>(pMain->GetSelectedTrack());
			const int Length = static_cast<int>(pDoc->GetPatternLength(Track));
			const ftapi::Timing Time(Length, static_cast<int>(pDoc->GetFrameCount(Track)), pDoc->GetFirstHighlight(),
				pDoc->GetSecondHighlight());
			Facts.Bar = Time.BarOf(pEditor->GetFrame() * Length + pEditor->GetRow());
			Facts.Channel = ToUtf8(RecordingChannelName(pDoc, pEditor->GetChannel()));
		}
		return ftapi::AssistantContext(Facts);
	}

	void Send()
	{
		if (pTalk == NULL || Phase != PHASE_IDLE)
			return;
		CString Typed;
		QuestionBox.GetWindowText(Typed);
		Typed.Trim();
		if (Typed.IsEmpty())
			return;
		const std::string Question = ToUtf8(Typed);
		QuestionBox.SetWindowText(_T(""));
		Follow = true;
		if (!HasEndpoint) {
			// No model: FamiTracker's own help
			pTalk->Chat.Offline(Question, ftapi::AssistantHelpAnswer(ftapi::AssistantSearchHelp(Question, BuiltInTutorials()), false));
			Changed();
			return;
		}
		pWnd->BeginWaitCursor();
		const std::string Song = Context(!pTalk->Chat.HasToolsOff());
		pWnd->EndWaitCursor();
		pTalk->Chat.SetTools(ToolInfos, Settings.SmallSet);
		Setup = ftapi::AssistantSetup();
		Setup.SystemPrompt = pTalk->Chat.HasToolsOff() ? ftapi::AssistantSystemPrompt(Settings.SmallSet, false) : PromptFor(Settings.SmallSet);
		Setup.Functions = pTalk->Chat.HasToolsOff() ? ftapi::Json::MakeArray() : FunctionsFor(Settings.SmallSet);
		if (Mode == ftapi::ASSISTANT_LET_IT_WORK) {
			pTalk->RunQuestion = pTalk->Chat.Questions() + 1;
			pTalk->RunActions.clear();
		}
		pTalk->Chat.Ask(Question, Song, Mode, Mode == ftapi::ASSISTANT_LET_IT_WORK ? Settings.WorkRounds : Settings.AskRounds);
		StopAsked = false;
		Round();
	}

	// The model's turn: its server asked first what it can read at once, then the question sent
	void Round()
	{
		if (pTalk == NULL || pTalk->Chat.State() != ftapi::AssistantChat::MODEL || StopAsked) {
			Phase = PHASE_IDLE;
			Changed();
			return;
		}
		if (!ServerAsked) {
			Phase = PHASE_PROPS;
			Job = StartJob(pWnd->GetSafeHwnd(), JOB_PROPS, Where, Settings.Key, Settings.Model, std::string());
			Begin();
			return;
		}
		Setup.Model = ModelName();
		Setup.ContextTokens = Settings.ContextTokens > 0 ? Settings.ContextTokens :
			ServerContext > 0 ? ServerContext : ftapi::ASSISTANT_CONTEXT;
		std::string Body, Problem;
		if (!pTalk->Chat.Request(Setup, Body, Problem)) {
			Phase = PHASE_IDLE;
			Changed();
			return;
		}
		Phase = PHASE_CHAT;
		Streaming.clear();
		Started = Thinking = false;
		Job = StartJob(pWnd->GetSafeHwnd(), JOB_CHAT, Where, Settings.Key, Setup.Model, Body);
		Begin();
	}

	void Begin()
	{
		JobStart = ::GetTickCount();
		pWnd->SetTimer(TMR_TICK, TICK_MS, NULL);
		Changed();
	}

	void StopJob()
	{
		if (Job)
			Job->Detach();
		Job.reset();
		if (Phase == PHASE_CHAT && pTalk != NULL) {
			ftapi::AssistantReply Partial;
			Partial.Content = Streaming;
			pTalk->Chat.Replied(Partial, true);
		}
		Phase = PHASE_IDLE;
		Streaming.clear();
		Waiting = false;
	}

	void Stop()
	{
		StopAsked = true;
		if (Phase == PHASE_CHAT && Job) {
			Job->Stop = true;		// its EV_DONE comes in a moment, with what came
			Changed();
			return;
		}
		if (Phase == PHASE_PROPS) {
			StopJob();
			if (pTalk != NULL)
				pTalk->Chat.Stop();
		}
		else if (pTalk != NULL && (pTalk->Chat.State() != ftapi::AssistantChat::IDLE || Waiting)) {
			Waiting = false;
			pTalk->Chat.Stop();
		}
		Phase = PHASE_IDLE;
		Changed();
	}

	// A job's event
	void Event(int Kind, ask_job_t *pJob, int Serial)
	{
		if (Resolving && pJob == Resolving.get() && Serial == Resolving->Serial && Kind == EV_DONE) {
			std::string Address;
			{
				std::lock_guard<std::mutex> Hold(pJob->Lock);
				Address = pJob->Address;
			}
			Resolving.reset();
			PrivacyNote = ftapi::AssistantPrivacyNote(Where, Address, OwnAddresses());
			Changed();
			return;
		}
		if (!Job || pJob != Job.get() || Serial != Job->Serial)
			return;		// one stopped, or of a song no longer shown
		if (Kind == EV_STARTED) {
			Started = true;
			Changed();
			return;
		}
		if (Kind == EV_TEXT) {
			std::string Text, Thought;
			{
				std::lock_guard<std::mutex> Hold(pJob->Lock);
				Text.swap(pJob->Text);
				Thought.swap(pJob->Thinking);
				pJob->Told = false;
			}
			Streaming += Text;
			Thinking = Streaming.empty() && !Thought.empty();
			Soon();
			return;
		}
		if (Kind != EV_DONE)
			return;
		const job_ptr Done = Job;
		Job.reset();
		if (Phase == PHASE_PROPS) {
			{
				std::lock_guard<std::mutex> Hold(Done->Lock);
				ServerContext = Done->Context;
				ServerModels = Done->Models;
				WaitingNet = Done->Net;
				WaitingError = Done->Error;
			}
			ServerAsked = true;
			if (WaitingNet != ftapi::NET_DONE && WaitingNet != ftapi::NET_STOPPED && pTalk != NULL) {
				// The server can't be reached: said once, with what to check
				ServerAsked = false;
				pTalk->Chat.Replied(ftapi::AssistantReply(), true, ftapi::AssistantNetProblem(WaitingNet, Where, ftapi::AssistantTimeouts(),
					WaitingError));
				Phase = PHASE_IDLE;
				Changed();
				return;
			}
			Round();
			return;
		}
		{
			std::lock_guard<std::mutex> Hold(Done->Lock);
			WaitingNet = Done->Net;
			WaitingReply = Done->Reply;
			WaitingError = Done->Error;
			Streaming += Done->Text;		// what came after the last EV_TEXT
		}
		if (StopAsked && WaitingNet == ftapi::NET_DONE && !WaitingReply.Calls.empty())
			WaitingNet = ftapi::NET_STOPPED;	// stopped as it ended: its calls aren't made
		if (WaitingReply.Content.empty())
			WaitingReply.Content = Streaming;
		Streaming.clear();
		Phase = PHASE_IDLE;
		Waiting = true;
		TakeReply();
	}

	// The reply taken in, and its calls run on this thread (FamiTracker's song is changed here), unless a dialog is open:
	// then they wait for it to close
	void TakeReply()
	{
		CMainFrame *pMain = Main();
		if (!Waiting || pTalk == NULL)
			return;
		if (pMain != NULL && !pMain->IsWindowEnabled() && !WaitingReply.Calls.empty() && WaitingNet == ftapi::NET_DONE) {
			pWnd->SetTimer(TMR_TICK, TICK_MS, NULL);
			Changed();
			return;
		}
		Waiting = false;
		const bool Again = ftapi::AssistantTakeReply(pTalk->Chat, Setup, Settings.SmallSet, Where, Setup.Model, ftapi::AssistantTimeouts(),
			WaitingNet, WaitingReply, WaitingError);
		WaitingReply = ftapi::AssistantReply();
		if (Again) {
			Round();
			return;
		}
		RunCalls();
	}

	void RunCalls()
	{
		if (pTalk == NULL)
			return;
		class Tools : public ftapi::AssistantTools {
		public:
			Tools(Impl &I) : m_I(I) {}
			bool RunTool(const std::string &Name, const std::string &Arguments, bool Changes, std::string &Reply)
			{
				return m_I.RunTool(Name, Arguments, Changes, Reply);
			}
		private:
			Impl &m_I;
		} Here(*this);
		if (pTalk->Chat.State() == ftapi::AssistantChat::MODEL) {
			pWnd->BeginWaitCursor();
			pTalk->Chat.RunReady(Here);
			pWnd->EndWaitCursor();
		}
		Changed();
		if (pTalk->Chat.State() == ftapi::AssistantChat::MODEL && !StopAsked)
			pWnd->PostMessage(WM_ASK_JOB, EV_NEXT_ROUND, 0);	// after the panel shows what was done
	}

	// A tool for the model, on this thread as the Claude Code connection runs them: a change as one undoable step
	bool RunTool(const std::string &Name, const std::string &Arguments, bool Changes, std::string &Reply)
	{
		CMainFrame *pMain = Main();
		if (pTools == NULL || pMain == NULL) {
			Reply = "FamiTracker's tools aren't there.";
			return false;
		}
		if (!Changes)
			return pTools->RunTool(Name.c_str(), Arguments, Reply, false, Settings.SmallSet);
		const long long Before = pMain->GetActionSerial();
		pMain->BeginActionGroup();
		const bool Done = pTools->RunTool(Name.c_str(), Arguments, Reply, false, Settings.SmallSet);
		pMain->EndActionGroup();
		const bool Undoable = pMain->GetActionSerial() != Before;
		if (!Done) {
			if (Undoable) {		// what it changed before it failed, and nothing to redo of it
				pMain->SendMessage(WM_COMMAND, ID_EDIT_UNDO);
				pMain->ForgetRedo();
			}
			return false;
		}
		CAction *pAction = Undoable ? pMain->GetUndoAction() : NULL;
		if (pAction != NULL) {
			ftapi::Json Args;
			ftapi::ParseJson(Arguments, Args);
			pAction->SetLabel(ftapi::AssistantCallWords(TitleOf(Name), Args) + ASK_LABEL);
			if (pTalk != NULL && pTalk->Chat.Mode() == ftapi::ASSISTANT_LET_IT_WORK && pTalk->RunQuestion == pTalk->Chat.Questions())
				pTalk->RunActions.push_back(pAction);
		}
		return true;
	}

	void Decide(size_t Message, size_t Call, bool Do)
	{
		if (pTalk == NULL || Phase != PHASE_IDLE)
			return;
		class Tools : public ftapi::AssistantTools {
		public:
			Tools(Impl &I) : m_I(I) {}
			bool RunTool(const std::string &Name, const std::string &Arguments, bool Changes, std::string &Reply)
			{
				return m_I.RunTool(Name, Arguments, Changes, Reply);
			}
		private:
			Impl &m_I;
		} Here(*this);
		StopAsked = false;
		pWnd->BeginWaitCursor();
		pTalk->Chat.Decide(Message, Call, Do, Here);
		pWnd->EndWaitCursor();
		Changed();
		if (pTalk->Chat.State() == ftapi::AssistantChat::MODEL)
			Round();
	}

	// Take back all: the run's changes undone, as long as they're the last in the undo history
	void TakeBackAll()
	{
		CMainFrame *pMain = Main();
		if (pTalk == NULL || pMain == NULL || pTalk->RunActions.empty())
			return;
		if (Phase != PHASE_IDLE || pTalk->Chat.State() != ftapi::AssistantChat::IDLE)
			Stop();
		const size_t Made = pTalk->RunActions.size();
		size_t Undone = 0;
		while (Undone < Made) {
			CAction *pTop = pMain->GetUndoAction();
			if (pTop == NULL || std::find(pTalk->RunActions.begin(), pTalk->RunActions.end(), pTop) == pTalk->RunActions.end())
				break;
			const std::string &Label = pTop->GetLabel();
			const size_t Mark = sizeof(ASK_LABEL) - 1;
			if (Label.size() < Mark || Label.compare(Label.size() - Mark, Mark, ASK_LABEL) != 0)
				break;
			pMain->SendMessage(WM_COMMAND, ID_EDIT_UNDO);
			++Undone;
		}
		pTalk->RunActions.clear();
		if (Undone == Made)
			pTalk->Chat.Note("Took back all " + Number(static_cast<long long>(Made)) + (Made == 1 ? " change" : " changes") +
				" of the run (Edit > Redo brings them back one at a time).");
		else if (Undone == 0)
			pTalk->Chat.Note("Nothing was taken back: the run's changes aren't the last in Edit > Undo's history any more (the song "
				"was changed since, or the history was cleared). Use Edit > Undo to go back a step at a time.");
		else
			pTalk->Chat.Note("Took back " + Number(static_cast<long long>(Undone)) + " of the run's " + Number(static_cast<long long>(Made)) +
				" changes; the song was changed after the others. Use Edit > Undo for the rest.");
		Changed();
	}

	void NewQuestion()
	{
		StopJob();
		if (pTalk != NULL) {
			pTalk->Chat.Clear();
			pTalk->RunActions.clear();
			pTalk->RunQuestion = 0;
		}
		StopAsked = false;
		Follow = true;
		Changed();
		QuestionBox.SetFocus();
	}

	void Link(const std::string &Target)
	{
		CMainFrame *pMain = Main();
		if (pMain == NULL)
			return;
		if (Target == "settings") {
			ShowSettings();
			return;
		}
		if (Target.compare(0, 9, "exercise:") == 0) {
			const std::vector<ftapi::ExerciseInfo> &List = ftapi::Exercises();
			for (size_t i = 0; i < List.size(); ++i)
				if (Target.substr(9) == List[i].Id)
					pMain->PostMessage(WM_COMMAND, ID_HELP_EXERCISE_FIRST + static_cast<UINT>(i));
			return;
		}
		if (Target.compare(0, 9, "tutorial:") == 0) {
			ShowTutorials(pMain);
			return;
		}
		ShowGlossaryAt(pMain, Target);
	}

	// The ? button: how to connect a model, step by step, shown in the conversation and never sent (a note while a
	// question is being answered, so that answer keeps its place)
	void ShowSetupHelp()
	{
		if (pTalk == NULL)
			return;
		if (Phase == PHASE_IDLE)
			pTalk->Chat.Offline("How do I connect my own AI model?", ftapi::AssistantSetupGuide());
		else
			pTalk->Chat.Note(ftapi::AssistantSetupGuide());
		Follow = true;
		Changed();
	}

	void ShowSettings()
	{
		const size_t All = ftapi::WriteJson(FunctionsFor(false)).size() + PromptFor(false).size();
		const size_t Small = ftapi::WriteJson(FunctionsFor(true)).size() + PromptFor(true).size();
		CAskSettingsDlg Dialog(pWnd, Settings, All, Small);
		const INT_PTR Answer = Dialog.DoModal();
		// MFC gives the keys to the pattern editor as the window comes back from the dialog, where a question typed
		// next played notes, Space started editing and Enter played the song: they go back to the question
		if (QuestionBox.GetSafeHwnd() != NULL)
			QuestionBox.SetFocus();
		if (Answer != IDOK)
			return;
		const bool KeyKept = WriteSettings(Dialog.Settings());
		ApplySettings(Dialog.Settings());
		if (!KeyKept && pTalk != NULL) {
			pTalk->Chat.Note("Your key is used until FamiTracker closes: Windows couldn't keep it encrypted (DPAPI), and it's "
				"never saved as plain text.");
			Changed();
		}
	}

	// ---- Showing it

	void Changed()
	{
		Dirty = true;
		if (Talk.GetSafeHwnd() != NULL)
			Talk.Invalidate(FALSE);
		const bool Working = Phase != PHASE_IDLE || Waiting;
		if (SendButton.GetSafeHwnd() != NULL) {
			SendButton.EnableWindow(!Working);
			StopButton.EnableWindow(Working || (pTalk != NULL && pTalk->Chat.State() != ftapi::AssistantChat::IDLE));
		}
		if (!Working && pWnd->GetSafeHwnd() != NULL)
			pWnd->KillTimer(TMR_TICK);
	}

	void Soon()
	{
		Dirty = true;
		pWnd->SetTimer(TMR_PAINT, PAINT_MS, NULL);
	}

	int LineHeight(CDC &dc, int Font)
	{
		CFont *pOld = dc.SelectObject(&Fonts[Font]);
		TEXTMETRIC Metric;
		dc.GetTextMetrics(&Metric);
		dc.SelectObject(pOld);
		return Metric.tmHeight + Metric.tmExternalLeading + SY(1);
	}

	int Measure(CDC &dc, int Font, const std::wstring &Text)
	{
		if (Text.empty())
			return 0;
		CFont *pOld = dc.SelectObject(&Fonts[Font]);
		SIZE Size = { 0, 0 };
		::GetTextExtentPoint32W(dc.GetSafeHdc(), Text.c_str(), static_cast<int>(Text.size()), &Size);
		dc.SelectObject(pOld);
		return Size.cx;
	}

	void Put(const CRect &Rect, const std::wstring &Text, int Font, COLORREF Colour)
	{
		const text_t T = { Rect, Text, Font, Colour };
		Texts.push_back(T);
	}

	// Paragraphs flowed into Width from Left, words wrapped, links noted; y goes down
	void Flow(CDC &dc, const std::vector<ftapi::AssistantParagraph> &Paragraphs, int Left, int Width, int &y, COLORREF Colour,
		bool Small)
	{
		const int Line = LineHeight(dc, Small ? F_SMALL : F_TEXT);
		for (size_t p = 0; p < Paragraphs.size(); ++p) {
			const ftapi::AssistantParagraph &P = Paragraphs[p];
			if (P.Gap)
				y += Line / 2;
			int Indent = 0;
			if (!P.Bullet.empty()) {
				const std::wstring Mark = P.Bullet == "-" ? std::wstring(1, static_cast<wchar_t>(0x2022)) : Wide(P.Bullet);
				Put(CRect(Left + SX(4), y, Left + SX(24), y + Line), Mark, Small ? F_SMALL : F_TEXT, Colour);
				Indent = std::max(SX(16), Measure(dc, Small ? F_SMALL : F_TEXT, Mark) + SX(10));
			}
			const int Room = std::max(SX(40), Width - Indent);
			int x = 0;
			bool Empty = true;
			// The words of a line in one style go out as one piece
			std::wstring Piece;
			int PieceX = 0, PieceY = y, PieceFont = F_TEXT;
			COLORREF PieceInk = Colour;
			std::string PieceLink;
			const auto Flush = [&]() {
				if (Piece.empty())
					return;
				const CRect Rect(Left + Indent + PieceX, PieceY, Left + Indent + x, PieceY + Line);
				Put(Rect, Piece, PieceFont, PieceInk);
				if (!PieceLink.empty())
					AddHit(Rect, ACT_LINK, PieceLink);
				Piece.clear();
			};
			for (size_t r = 0; r < P.Runs.size(); ++r) {
				const ftapi::AssistantRun &R = P.Runs[r];
				const bool Linked = !R.Link.empty(), Bold = R.Bold || P.Heading;
				const int Font = P.Code ? F_CODE : Small ? (Linked ? F_SMALL_LINK : Bold ? F_SMALL_BOLD : F_SMALL) :
					Linked ? (Bold ? F_BOLD_LINK : F_LINK) : Bold ? F_BOLD : F_TEXT;
				const COLORREF Ink = Linked ? LINK_COLOUR : Colour;
				const std::wstring W = Wide(R.Text);
				const int Space = Measure(dc, Font, L" ");
				if (!Piece.empty() && (PieceFont != Font || PieceInk != Ink || PieceLink != R.Link))
					Flush();
				size_t i = 0;
				while (i < W.size()) {
					if (W[i] == L' ' || W[i] == L'\t') {
						if (!Empty) {
							x += Space;
							if (!Piece.empty())
								Piece += L' ';
						}
						++i;
						continue;
					}
					size_t j = i;
					while (j < W.size() && W[j] != L' ' && W[j] != L'\t')
						++j;
					std::wstring Word = W.substr(i, j - i);
					i = j;
					int w = Measure(dc, Font, Word);
					if (!Empty && x + w > Room) {
						Flush();
						y += Line;
						x = 0;
						Empty = true;
					}
					// A word wider than the line goes on in pieces
					if (w > Room && Word.size() > 1)
						Flush();
					while (w > Room && Word.size() > 1) {
						size_t Fit = Word.size() - 1;
						while (Fit > 1 && Measure(dc, Font, Word.substr(0, Fit)) > Room - x)
							--Fit;
						const std::wstring Part = Word.substr(0, Fit);
						const CRect Rect(Left + Indent + x, y, Left + Indent + x + Measure(dc, Font, Part), y + Line);
						Put(Rect, Part, Font, Ink);
						if (Linked)
							AddHit(Rect, ACT_LINK, R.Link);
						Word = Word.substr(Fit);
						w = Measure(dc, Font, Word);
						y += Line;
						x = 0;
					}
					if (Piece.empty()) {
						PieceX = x;
						PieceY = y;
						PieceFont = Font;
						PieceInk = Ink;
						PieceLink = R.Link;
					}
					Piece += Word;
					x += w;
					Empty = false;
				}
			}
			Flush();
			y += Line;
		}
	}

	void FlowText(CDC &dc, const std::string &Text, int Left, int Width, int &y, COLORREF Colour, bool Small)
	{
		Flow(dc, ftapi::AssistantRichText(Text), Left, Width, y, Colour, Small);
	}

	// Plain text as it was typed: its lines kept, no marks read
	void FlowPlain(CDC &dc, const std::string &Text, int Left, int Width, int &y, COLORREF Colour)
	{
		std::vector<ftapi::AssistantParagraph> Lines;
		size_t Start = 0;
		while (Start <= Text.size()) {
			size_t End = Text.find('\n', Start);
			if (End == std::string::npos)
				End = Text.size();
			ftapi::AssistantParagraph P;
			ftapi::AssistantRun R;
			R.Text = Text.substr(Start, End - Start);
			if (!R.Text.empty() && R.Text[R.Text.size() - 1] == '\r')
				R.Text.erase(R.Text.size() - 1);
			P.Runs.push_back(R);
			Lines.push_back(P);
			Start = End + 1;
		}
		Flow(dc, Lines, Left, Width, y, Colour, false);
	}

	void AddHit(const CRect &Rect, int Action, const std::string &Link, size_t Message = 0, size_t Call = 0)
	{
		hit_t Hit;
		Hit.Rect = Rect;
		Hit.Action = Action;
		Hit.Link = Link;
		Hit.Message = Message;
		Hit.Call = Call;
		Hit.Primary = false;
		Hits.push_back(Hit);
	}

	// A button at x, y: its width
	int AddButton(CDC &dc, int x, int y, const std::wstring &Label, int Action, bool Primary, size_t Message, size_t Call)
	{
		const int Width = Measure(dc, F_BOLD, Label) + SX(20);
		hit_t Hit;
		Hit.Rect = CRect(x, y, x + Width, y + SY(BUTTON_HEIGHT));
		Hit.Action = Action;
		Hit.Message = Message;
		Hit.Call = Call;
		Hit.Label = Label;
		Hit.Primary = Primary;
		Hits.push_back(Hit);
		return Width;
	}

	// A line of a call's outcome: its mark ("Done:") in its colour, then its words
	void CallLine(CDC &dc, const char *pMark, COLORREF MarkColour, const std::string &Words, int Left, int Width, int &y)
	{
		std::vector<ftapi::AssistantParagraph> P(1);
		ftapi::AssistantRun Mark;
		Mark.Text = std::string(pMark) + " ";
		Mark.Bold = true;
		P[0].Runs.push_back(Mark);
		ftapi::AssistantRun Rest;
		Rest.Text = Words;
		P[0].Runs.push_back(Rest);
		const size_t First = Texts.size();
		Flow(dc, P, Left, Width, y, TEXT_DARK, true);
		if (First < Texts.size())
			Texts[First].Colour = MarkColour;		// the mark: bold, a piece of its own
	}

	// Everything laid out for Width
	void Lay(CDC &dc, int Width)
	{
		Texts.clear();
		Boxes.clear();
		Hits.clear();
		const int Left = SX(MARGIN), Inner = std::max(SX(60), Width - 2 * SX(MARGIN));
		int y = SY(6);
		// Where the questions go
		std::string Head;
		if (!HasEndpoint)
			Head = "No model is set up: the panel searches FamiTracker's own help. Connect yours in the [[settings|settings]].";
		else
			Head = "Your model: " + (ModelName().empty() ? std::string("the server's own") : ModelName()) + " at " + Where.Host + ":" +
				Number(Where.Port) + (Settings.SmallSet ? ", the small set of tools" : ", all the tools") +
				(pTalk != NULL && pTalk->Chat.HasToolsOff() ? " (the server takes none)" : "") + ". [[Settings|settings]]";
		FlowText(dc, Head, Left, Inner, y, TEXT_DIM, true);
		if (!PrivacyNote.empty())
			FlowText(dc, PrivacyNote, Left, Inner, y, NOTE_COLOUR, true);
		y += SY(4);
		const std::vector<ftapi::AssistantMessage> Empty;
		const std::vector<ftapi::AssistantMessage> &Messages = pTalk != NULL ? pTalk->Chat.Messages() : Empty;
		if (Messages.empty() && Phase == PHASE_IDLE) {
			FlowText(dc, "Ask about your song or about making music (\"how do I make the tune sound calmer?\"), or tell your "
				"model what to do (\"add drums to bars 5-8\"). In **Ask first** it asks before it changes anything; in **Let it "
				"work** it goes ahead, step by step, and Take back all undoes the run. Words like [[chord]] open the glossary.",
				Left, Inner, y, TEXT_DIM, false);
		}
		const int Pad = SX(6);
		// The last message of the Let it work run that Take back all undoes
		size_t RunEnd = Messages.size();
		if (pTalk != NULL && pTalk->RunQuestion > 0 && !pTalk->RunActions.empty())
			for (size_t m = 0; m < Messages.size(); ++m)
				if (Messages[m].Question == pTalk->RunQuestion)
					RunEnd = m;
		for (size_t m = 0; m < Messages.size(); ++m) {
			const ftapi::AssistantMessage &M = Messages[m];
			if (M.Role == ftapi::ASSISTANT_USER) {
				y += SY(6);
				const int Top = y;
				y += SY(3);
				Put(CRect(Left + Pad, y, Left + Inner, y + LineHeight(dc, F_SMALL_BOLD)),
					M.Mode == ftapi::ASSISTANT_LET_IT_WORK ? L"You (Let it work)" : L"You", F_SMALL_BOLD, TEXT_DIM);
				y += LineHeight(dc, F_SMALL_BOLD);
				FlowPlain(dc, M.Text, Left + Pad, Inner - 2 * Pad, y, TEXT_DARK);
				y += SY(4);
				const box_t Box = { CRect(Left, Top, Left + Inner, y), QUESTION_BACK, QUESTION_BACK };
				Boxes.push_back(Box);
				y += SY(4);
			}
			else if (M.Role == ftapi::ASSISTANT_NOTE) {
				FlowText(dc, M.Text, Left, Inner, y, NOTE_COLOUR, true);
				y += SY(2);
			}
			else
				LayAnswer(dc, M, m, Left, Inner, Pad, y);
			if (m == RunEnd) {
				y += SY(3);
				const std::wstring Label = L"Take back all (" + Wide(Number(static_cast<long long>(pTalk->RunActions.size()))) +
					(pTalk->RunActions.size() == 1 ? L" change)" : L" changes)");
				AddButton(dc, Left, y, Label, ACT_TAKE_BACK, false, m, 0);
				y += SY(BUTTON_HEIGHT) + SY(6);
			}
		}
		// The reply as it comes, and what's going on
		if (Phase == PHASE_CHAT && !Streaming.empty())
			FlowText(dc, Streaming, Left, Inner, y, TEXT_DARK, false);
		const std::string Status = StatusText();
		if (!Status.empty())
			FlowText(dc, Status, Left, Inner, y, TEXT_DIM, true);
		y += SY(MARGIN);
		Content = y;
		LaidWidth = Width;
		Dirty = false;
	}

	// An answer: its text, the reads in a line, and each change with what became of it
	void LayAnswer(CDC &dc, const ftapi::AssistantMessage &M, size_t m, int Left, int Inner, int Pad, int &y)
	{
		if (!M.Text.empty())
			FlowText(dc, M.Text, Left, Inner, y, TEXT_DARK, false);
		std::string Reads, Unread;
		for (size_t c = 0; c < M.Calls.size(); ++c) {
			const ftapi::AssistantCall &C = M.Calls[c];
			if (C.Changes)
				continue;
			if (C.State == ftapi::CALL_DONE)
				Reads += (Reads.empty() ? "" : ", ") + C.Title;
			else if (C.State == ftapi::CALL_FAILED || C.State == ftapi::CALL_STOPPED || C.State == ftapi::CALL_SKIPPED)
				Unread += (Unread.empty() ? "" : "; ") + C.Words + ": " + ftapi::AssistantResultLine(C.Result);
		}
		if (!Reads.empty())
			CallLine(dc, "Looked at:", TEXT_DIM, Reads, Left, Inner, y);
		if (!Unread.empty())
			CallLine(dc, "Couldn't:", FAILED_COLOUR, Unread, Left, Inner, y);
		for (size_t c = 0; c < M.Calls.size(); ++c) {
			const ftapi::AssistantCall &C = M.Calls[c];
			if (!C.Changes)
				continue;
			switch (C.State) {
			case ftapi::CALL_PROPOSED: {
				y += SY(4);
				const int Top = y;
				y += SY(4);
				std::vector<ftapi::AssistantParagraph> P(1);
				ftapi::AssistantRun Mark;
				Mark.Text = "Proposed: ";
				Mark.Bold = true;
				P[0].Runs.push_back(Mark);
				ftapi::AssistantRun Words;
				Words.Text = C.Words;
				P[0].Runs.push_back(Words);
				Flow(dc, P, Left + Pad, Inner - 2 * Pad, y, TEXT_DARK, false);
				std::vector<ftapi::AssistantParagraph> Call(1);
				ftapi::AssistantRun Behind;
				Behind.Text = ftapi::AssistantCallText(C.Name, C.Arguments);
				Call[0].Runs.push_back(Behind);
				Flow(dc, Call, Left + Pad, Inner - 2 * Pad, y, TEXT_DIM, true);
				y += SY(3);
				int x = Left + Pad;
				x += AddButton(dc, x, y, L"Do it", ACT_DO, true, m, c) + SX(6);
				AddButton(dc, x, y, L"Skip", ACT_SKIP, false, m, c);
				y += SY(BUTTON_HEIGHT) + SY(6);
				const box_t Box = { CRect(Left, Top, Left + Inner, y), PROPOSAL_BACK, PROPOSAL_BORDER };
				Boxes.push_back(Box);
				y += SY(4);
				break;
			}
			case ftapi::CALL_DONE:
				CallLine(dc, "Done:", DONE_COLOUR, C.Words + " - " + ftapi::AssistantResultLine(C.Result), Left, Inner, y);
				break;
			case ftapi::CALL_FAILED:
				CallLine(dc, "Didn't work:", FAILED_COLOUR, C.Words + " - " + ftapi::AssistantResultLine(C.Result), Left, Inner, y);
				break;
			case ftapi::CALL_SKIPPED:
				CallLine(dc, "Skipped:", TEXT_DIM, C.Words, Left, Inner, y);
				break;
			case ftapi::CALL_STOPPED:
				CallLine(dc, "Not done:", TEXT_DIM, C.Words + " (stopped)", Left, Inner, y);
				break;
			default:
				break;
			}
		}
		if (M.Stopped && !M.Text.empty())
			FlowText(dc, "(cut short)", Left, Inner, y, TEXT_DIM, true);
		y += SY(4);
	}

	std::string StatusText() const
	{
		const long long Seconds = (::GetTickCount() - JobStart) / 1000;
		const std::string Taken = Seconds >= 2 ? " (" + Number(Seconds) + " s)" : std::string();
		const std::string Long = Seconds >= 30 ? " A model loading, or reading FamiTracker's tools for the first time, can take "
			"a minute or two; Stop stops it." : std::string();
		if (Waiting)
			return "Waiting for the dialog to close before the tools run.";
		if (StopAsked && Phase == PHASE_CHAT)
			return "Stopping...";
		if (Phase == PHASE_PROPS)
			return "Asking the server how much the model can read at once..." + Taken;
		if (Phase == PHASE_CHAT) {
			if (!Started)
				return "Sending the question to your model..." + Taken + Long;
			if (Streaming.empty())
				return (Thinking ? "Your model is thinking..." : "Your model is reading the question...") + Taken + Long;
			return std::string();
		}
		if (pTalk != NULL && pTalk->Chat.State() == ftapi::AssistantChat::USER)
			return "Do it or Skip the proposal above, or ask something else.";
		return std::string();
	}

	void ScrollTo(int Position, bool Repaint = true)
	{
		CRect Client;
		Talk.GetClientRect(&Client);
		const int Most = std::max(0, Content - Client.Height());
		Scroll = std::max(0, std::min(Most, Position));
		Follow = Scroll >= Most;
		SCROLLINFO Info;
		memset(&Info, 0, sizeof(Info));
		Info.cbSize = sizeof(Info);
		Info.fMask = SIF_ALL | SIF_DISABLENOSCROLL;	// always there, so the width the text is laid out in stays
		Info.nMin = 0;
		Info.nMax = std::max(0, Content - 1);
		Info.nPage = static_cast<UINT>(Client.Height());
		Info.nPos = Scroll;
		Talk.SetScrollInfo(SB_VERT, &Info, TRUE);
		if (Repaint)
			Talk.Invalidate(FALSE);
	}

	CRect Shown(const CRect &Rect) const
	{
		CRect Out = Rect;
		Out.OffsetRect(0, -Scroll);
		return Out;
	}

	int HitAt(const CPoint &Point) const
	{
		for (size_t i = 0; i < Hits.size(); ++i)
			if (Shown(Hits[i].Rect).PtInRect(Point))
				return static_cast<int>(i);
		return -1;
	}

	void Act(int Index)
	{
		if (Index < 0 || static_cast<size_t>(Index) >= Hits.size())
			return;
		const hit_t Hit = Hits[static_cast<size_t>(Index)];
		switch (Hit.Action) {
		case ACT_LINK:
			Link(Hit.Link);
			break;
		case ACT_DO:
			Decide(Hit.Message, Hit.Call, true);
			break;
		case ACT_SKIP:
			Decide(Hit.Message, Hit.Call, false);
			break;
		case ACT_TAKE_BACK:
			TakeBackAll();
			break;
		}
	}

	// What can be copied: the last answer, or the whole conversation
	std::string CopyText(bool All) const
	{
		std::string Out;
		if (pTalk == NULL)
			return Out;
		const std::vector<ftapi::AssistantMessage> &M = pTalk->Chat.Messages();
		for (size_t m = 0; m < M.size(); ++m) {
			if (!All && !(M[m].Role == ftapi::ASSISTANT_MODEL && !M[m].Text.empty()))
				continue;
			const std::string Text = M[m].Role == ftapi::ASSISTANT_USER ? "You: " + M[m].Text : ftapi::AssistantPlainText(M[m].Text);
			if (All)
				Out += (Out.empty() ? "" : "\n\n") + Text;
			else
				Out = Text;
		}
		return Out;
	}

	void Copy(bool All)
	{
		const std::wstring Text = Wide(CopyText(All));
		std::wstring Windows;
		for (size_t i = 0; i < Text.size(); ++i) {
			if (Text[i] == L'\n')
				Windows += L'\r';
			Windows += Text[i];
		}
		if (Windows.empty() || !pWnd->OpenClipboard())
			return;
		::EmptyClipboard();
		HGLOBAL hMemory = ::GlobalAlloc(GMEM_MOVEABLE, (Windows.size() + 1) * sizeof(wchar_t));
		if (hMemory != NULL) {
			wchar_t *pText = static_cast<wchar_t*>(::GlobalLock(hMemory));
			if (pText != NULL) {
				memcpy(pText, Windows.c_str(), (Windows.size() + 1) * sizeof(wchar_t));
				::GlobalUnlock(hMemory);
				if (::SetClipboardData(CF_UNICODETEXT, hMemory) == NULL)
					::GlobalFree(hMemory);
			}
			else
				::GlobalFree(hMemory);
		}
		::CloseClipboard();
	}

	void Menu(CPoint Screen)
	{
		CPanelBar *pBar = CPanelBar::Of(pWnd);
		CMenu Popup;
		Popup.CreatePopupMenu();
		Popup.AppendMenu(MF_STRING | (CopyText(false).empty() ? MF_GRAYED : 0), 1, _T("Copy the last answer"));
		Popup.AppendMenu(MF_STRING | (CopyText(true).empty() ? MF_GRAYED : 0), 2, _T("Copy the whole conversation"));
		Popup.AppendMenu(MF_STRING, 3, _T("New question"));
		Popup.AppendMenu(MF_SEPARATOR, 0, (LPCTSTR)NULL);
		Popup.AppendMenu(MF_STRING, 5, _T("Settings..."));
		Popup.AppendMenu(MF_SEPARATOR, 0, (LPCTSTR)NULL);
		if (pBar != NULL)
			pBar->AddPlaceItems(Popup, 10);	// a tab at the bottom, on the right, its own window
		Popup.AppendMenu(MF_STRING, 6, _T("Close"));
		const UINT Picked = Popup.TrackPopupMenu(TPM_LEFTALIGN | TPM_RETURNCMD | TPM_NONOTIFY, Screen.x, Screen.y, pWnd);
		switch (Picked) {
		case 1: Copy(false); break;
		case 2: Copy(true); break;
		case 3: NewQuestion(); break;
		case 5: ShowSettings(); break;
		case 6:
			if (Main() != NULL)
				Main()->PostMessage(WM_COMMAND, ID_VIEW_ASK);
			break;
		default:
			if (pBar != NULL)
				pBar->PickPlace(Picked, 10);
			break;
		}
	}

	void Arrange(int cx, int cy)
	{
		const int M = SX(MARGIN) / 2, Top = SY(TOP_ROW), Bottom = SY(QUESTION_HEIGHT) + 2 * M;
		const int Button = SY(BUTTON_HEIGHT), y = (Top - Button) / 2;
		int x = M;
		const int ModeWidth = std::min(SX(110), std::max(SX(70), cx / 3));
		ModeBox.MoveWindow(x, y, ModeWidth, SY(200));
		x += ModeWidth + M;
		NewButton.MoveWindow(x, y, SX(96), Button);
		x += SX(96) + M;
		SettingsButton.MoveWindow(x, y, SX(76), Button);
		x += SX(76) + M;
		HelpButton.MoveWindow(x, y, SX(24), Button);
		const int Side = SX(SIDE_BUTTON), Box = std::max(SX(40), cx - Side - 3 * M);
		Talk.MoveWindow(0, Top, cx, std::max(0, cy - Top - Bottom));
		QuestionBox.MoveWindow(M, cy - Bottom + M, Box, SY(QUESTION_HEIGHT));
		SendButton.MoveWindow(M + Box + M, cy - Bottom + M, Side, Button);
		StopButton.MoveWindow(M + Box + M, cy - Bottom + M + SY(QUESTION_HEIGHT) - Button, Side, Button);
		Dirty = true;
	}
};

// ---- The conversation's view

BEGIN_MESSAGE_MAP(CAskTalkWnd, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_VSCROLL()
	ON_WM_MOUSEWHEEL()
	ON_WM_LBUTTONDOWN()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSELEAVE()
	ON_WM_SETCURSOR()
	ON_WM_CONTEXTMENU()
	ON_WM_RBUTTONUP()
	ON_WM_GETDLGCODE()
END_MESSAGE_MAP()

bool CAskTalkWnd::Create(CWnd *pParent, CAskPanelWnd::Impl *pImpl)
{
	m_pImpl = pImpl;
	const CString Class = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(NULL, IDC_ARROW), NULL, NULL);
	return CWnd::Create(Class, _T("Answers"), WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP, CRect(0, 0, 0, 0), pParent,
		IDC_ASK_TALK) != FALSE;
}

BOOL CAskTalkWnd::OnEraseBkgnd(CDC *pDC)
{
	return TRUE;
}

void CAskTalkWnd::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	if (m_pImpl != NULL) {
		m_pImpl->Dirty = true;
		Invalidate(FALSE);
	}
}

void CAskTalkWnd::OnPaint()
{
	CPaintDC Paint(this);
	CRect Client;
	GetClientRect(&Client);
	if (m_pImpl == NULL || Client.Width() <= 0 || Client.Height() <= 0)
		return;
	CDC Memory;
	Memory.CreateCompatibleDC(&Paint);
	CBitmap Bitmap;
	Bitmap.CreateCompatibleBitmap(&Paint, Client.Width(), Client.Height());
	CBitmap *pOldBitmap = Memory.SelectObject(&Bitmap);
	CAskPanelWnd::Impl &I = *m_pImpl;
	if (I.Dirty || I.LaidWidth != Client.Width()) {
		I.Lay(Memory, Client.Width());
		const int Most = std::max(0, I.Content - Client.Height());
		I.ScrollTo(I.Follow ? Most : std::min(I.Scroll, Most), false);
	}
	Memory.FillSolidRect(&Client, BODY_BACK);
	for (size_t b = 0; b < I.Boxes.size(); ++b) {
		const CRect Box = I.Shown(I.Boxes[b].Rect);
		if (Box.bottom < 0 || Box.top > Client.bottom)
			continue;
		Memory.FillSolidRect(&Box, I.Boxes[b].Fill);
		if (I.Boxes[b].Border != I.Boxes[b].Fill) {
			CBrush Border(I.Boxes[b].Border);
			Memory.FrameRect(&Box, &Border);
		}
	}
	Memory.SetBkMode(TRANSPARENT);
	CFont *pOldFont = Memory.SelectObject(&I.Fonts[F_TEXT]);
	for (size_t t = 0; t < I.Texts.size(); ++t) {
		const CAskPanelWnd::Impl::text_t &T = I.Texts[t];
		const CRect Rect = I.Shown(T.Rect);
		if (Rect.bottom < 0 || Rect.top > Client.bottom)
			continue;
		Memory.SelectObject(&I.Fonts[T.Font]);
		Memory.SetTextColor(T.Colour);
		::TextOutW(Memory.GetSafeHdc(), Rect.left, Rect.top, T.Text.c_str(), static_cast<int>(T.Text.size()));
	}
	for (size_t h = 0; h < I.Hits.size(); ++h) {
		const CAskPanelWnd::Impl::hit_t &Hit = I.Hits[h];
		if (Hit.Label.empty())
			continue;		// a link: drawn with its text
		const CRect Rect = I.Shown(Hit.Rect);
		const bool Hot = I.Hover == static_cast<int>(h);
		Memory.FillSolidRect(&Rect, Hit.Primary ? (Hot ? PRIMARY_HOT : PRIMARY_BACK) : BUTTON_BACK);
		CBrush Border(Hit.Primary ? PRIMARY_HOT : Hot ? BUTTON_HOT : BUTTON_BORDER);
		Memory.FrameRect(&Rect, &Border);
		Memory.SelectObject(&I.Fonts[F_BOLD]);
		Memory.SetTextColor(Hit.Primary ? RGB(255, 255, 255) : TEXT_DARK);
		CRect TextRect = Rect;
		::DrawTextW(Memory.GetSafeHdc(), Hit.Label.c_str(), static_cast<int>(Hit.Label.size()), &TextRect,
			DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	}
	Memory.SelectObject(pOldFont);
	Paint.BitBlt(0, 0, Client.Width(), Client.Height(), &Memory, 0, 0, SRCCOPY);
	Memory.SelectObject(pOldBitmap);
}

void CAskTalkWnd::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar *pScrollBar)
{
	if (m_pImpl == NULL)
		return;
	CRect Client;
	GetClientRect(&Client);
	int Position = m_pImpl->Scroll;
	switch (nSBCode) {
	case SB_LINEUP: Position -= SY(LINE); break;
	case SB_LINEDOWN: Position += SY(LINE); break;
	case SB_PAGEUP: Position -= Client.Height(); break;
	case SB_PAGEDOWN: Position += Client.Height(); break;
	case SB_THUMBTRACK: case SB_THUMBPOSITION: {
		SCROLLINFO Info;
		memset(&Info, 0, sizeof(Info));
		Info.cbSize = sizeof(Info);
		Info.fMask = SIF_TRACKPOS;
		GetScrollInfo(SB_VERT, &Info);
		Position = Info.nTrackPos;
		break;
	}
	case SB_TOP: Position = 0; break;
	case SB_BOTTOM: Position = m_pImpl->Content; break;
	default: return;
	}
	m_pImpl->ScrollTo(Position);
}

BOOL CAskTalkWnd::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	if (m_pImpl != NULL)
		m_pImpl->ScrollTo(m_pImpl->Scroll - zDelta * SY(LINE) * 3 / WHEEL_DELTA);
	return TRUE;
}

void CAskTalkWnd::OnLButtonDown(UINT nFlags, CPoint point)
{
	SetFocus();
	if (m_pImpl != NULL)
		m_pImpl->Act(m_pImpl->HitAt(point));
}

void CAskTalkWnd::OnMouseMove(UINT nFlags, CPoint point)
{
	if (m_pImpl == NULL)
		return;
	const int Hover = m_pImpl->HitAt(point);
	if (Hover != m_pImpl->Hover) {
		m_pImpl->Hover = Hover;
		Invalidate(FALSE);
	}
	if (!m_pImpl->Tracking) {
		TRACKMOUSEEVENT Track;
		memset(&Track, 0, sizeof(Track));
		Track.cbSize = sizeof(Track);
		Track.dwFlags = TME_LEAVE;
		Track.hwndTrack = GetSafeHwnd();
		m_pImpl->Tracking = ::TrackMouseEvent(&Track) != FALSE;
	}
}

void CAskTalkWnd::OnMouseLeave()
{
	if (m_pImpl == NULL)
		return;
	m_pImpl->Tracking = false;
	if (m_pImpl->Hover >= 0) {
		m_pImpl->Hover = -1;
		Invalidate(FALSE);
	}
}

BOOL CAskTalkWnd::OnSetCursor(CWnd *pWnd, UINT nHitTest, UINT message)
{
	if (m_pImpl != NULL && pWnd == this && nHitTest == HTCLIENT && m_pImpl->Hover >= 0) {
		::SetCursor(::LoadCursor(NULL, IDC_HAND));
		return TRUE;
	}
	return CWnd::OnSetCursor(pWnd, nHitTest, message);
}

void CAskTalkWnd::OnContextMenu(CWnd *pWnd, CPoint point)
{
	if (m_pImpl == NULL)
		return;
	if (point.x == -1 && point.y == -1) {
		point = CPoint(SX(20), SY(20));
		ClientToScreen(&point);
	}
	m_pImpl->Menu(point);
}

void CAskTalkWnd::OnRButtonUp(UINT nFlags, CPoint point)
{
	// The menu here, not through WM_CONTEXTMENU (which Wine doesn't send a child window for a right-click)
	if (m_pImpl == NULL)
		return;
	ClientToScreen(&point);
	m_pImpl->Menu(point);
}

UINT CAskTalkWnd::OnGetDlgCode()
{
	return DLGC_WANTARROWS;
}

// ---- The panel

BEGIN_MESSAGE_MAP(CAskPanelWnd, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_TIMER()
	ON_WM_DESTROY()
	ON_WM_CONTEXTMENU()
	ON_WM_RBUTTONUP()
	ON_BN_CLICKED(IDC_ASK_SEND, OnSend)
	ON_BN_CLICKED(IDC_ASK_STOP, OnStop)
	ON_BN_CLICKED(IDC_ASK_NEW, OnNewQuestion)
	ON_BN_CLICKED(IDC_ASK_SETTINGS, OnSettings)
	ON_BN_CLICKED(IDC_ASK_SETUPHELP, OnSetupHelp)
	ON_CBN_SELCHANGE(IDC_ASK_MODE, OnModeChange)
	ON_MESSAGE(WM_ASK_JOB, OnJobMessage)
END_MESSAGE_MAP()

CAskPanelWnd::CAskPanelWnd() : m_pImpl(new Impl(this))
{
}

CAskPanelWnd::~CAskPanelWnd()
{
	delete m_pImpl;
}

bool CAskPanelWnd::Create(CWnd *pParent, CMcpConnection *pTools)
{
	Impl &I = *m_pImpl;
	I.pTools = pTools;
	const CString Class = AfxRegisterWndClass(0, ::LoadCursor(NULL, IDC_ARROW), NULL, NULL);
	if (!CWnd::Create(Class, _T("Ask"), WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, CRect(0, 0, 0, 0), pParent, AFX_IDW_PANE_FIRST + 7))
		return false;
	I.MakeFonts();
	const DWORD Button = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON;
	I.ModeBox.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, CRect(0, 0, 0, 0), this, IDC_ASK_MODE);
	I.ModeBox.AddString(_T("Ask first"));
	I.ModeBox.AddString(_T("Let it work"));
	I.Mode = theApp.GetProfileInt(SETTINGS, _T("Mode"), 0) != 0 ? ftapi::ASSISTANT_LET_IT_WORK : ftapi::ASSISTANT_ASK_FIRST;
	I.ModeBox.SetCurSel(I.Mode == ftapi::ASSISTANT_LET_IT_WORK ? 1 : 0);
	I.NewButton.Create(_T("New question"), Button, CRect(0, 0, 0, 0), this, IDC_ASK_NEW);
	I.SettingsButton.Create(_T("Settings..."), Button, CRect(0, 0, 0, 0), this, IDC_ASK_SETTINGS);
	I.HelpButton.Create(_T("?"), Button, CRect(0, 0, 0, 0), this, IDC_ASK_SETUPHELP);
	I.Talk.Create(this, m_pImpl);
	I.QuestionBox.CWnd::CreateEx(WS_EX_CLIENTEDGE, _T("EDIT"), _T(""), WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE |
		ES_AUTOVSCROLL | ES_WANTRETURN, CRect(0, 0, 0, 0), this, IDC_ASK_QUESTION);
	I.SendButton.Create(_T("Send"), Button | BS_DEFPUSHBUTTON, CRect(0, 0, 0, 0), this, IDC_ASK_SEND);
	I.StopButton.Create(_T("Stop"), Button, CRect(0, 0, 0, 0), this, IDC_ASK_STOP);
	CWnd *const CONTROLS[] = { &I.ModeBox, &I.NewButton, &I.SettingsButton, &I.HelpButton, &I.QuestionBox, &I.SendButton,
		&I.StopButton };
	for (size_t i = 0; i < sizeof(CONTROLS) / sizeof(CONTROLS[0]); ++i)
		CONTROLS[i]->SetFont(&I.ControlFont);
	I.QuestionBox.SetLimitText(8000);
	if (pTools != NULL)
		pTools->ToolList(I.ToolInfos);
	I.ApplySettings(ReadSettings());
	I.FollowSong();
	SetTimer(TMR_LOOK, LOOK_MS, NULL);
	I.Changed();
	return true;
}

void CAskPanelWnd::Opened(bool Open)
{
	m_pImpl->Open = Open;
	if (!Open) {
		// Closed: nothing goes on unseen
		if (m_pImpl->Phase != PHASE_IDLE || m_pImpl->Waiting)
			m_pImpl->Stop();
		return;
	}
	m_pImpl->FollowSong();
	m_pImpl->Changed();
}

void CAskPanelWnd::SongChanged()
{
	if (m_pImpl->Open)
		m_pImpl->FollowSong();
}

void CAskPanelWnd::FocusQuestion()
{
	if (m_pImpl->QuestionBox.GetSafeHwnd() != NULL)
		m_pImpl->QuestionBox.SetFocus();
}

BOOL CAskPanelWnd::PreTranslateMessage(MSG *pMsg)
{
	// The panel's keys go to its controls before FamiTracker's shortcuts see them (Space is one, as are many letters
	// with Ctrl): typing a question must type it
	Impl &I = *m_pImpl;
	const bool Key = pMsg->message == WM_KEYDOWN || pMsg->message == WM_SYSKEYDOWN || pMsg->message == WM_CHAR ||
		pMsg->message == WM_SYSCHAR || pMsg->message == WM_KEYUP;
	if (!Key || pMsg->hwnd == NULL || (pMsg->hwnd != GetSafeHwnd() && !::IsChild(GetSafeHwnd(), pMsg->hwnd)))
		return CWnd::PreTranslateMessage(pMsg);
	const bool Down = pMsg->message == WM_KEYDOWN;
	const WPARAM Code = pMsg->wParam;
	const bool Shift = ::GetKeyState(VK_SHIFT) < 0, Control = ::GetKeyState(VK_CONTROL) < 0;
	if (pMsg->message == WM_SYSKEYDOWN || pMsg->message == WM_SYSCHAR)
		return CWnd::PreTranslateMessage(pMsg);		// Alt: the menus
	if (Down && Code >= VK_F1 && Code <= VK_F12)
		return CWnd::PreTranslateMessage(pMsg);		// play, stop and the others
	if (Down && Code == VK_ESCAPE) {
		CFamiTrackerView *pView = CFamiTrackerView::GetView();
		if (pView != NULL)
			pView->SetFocus();
		return TRUE;
	}
	if (Down && Code == VK_TAB) {
		// Around the panel's controls
		CWnd *const ORDER[] = { &I.QuestionBox, &I.SendButton, &I.StopButton, &I.ModeBox, &I.NewButton, &I.SettingsButton, &I.Talk };
		const size_t Count = sizeof(ORDER) / sizeof(ORDER[0]);
		size_t At = 0;
		for (size_t i = 0; i < Count; ++i)
			if (ORDER[i]->GetSafeHwnd() == pMsg->hwnd || ::IsChild(ORDER[i]->GetSafeHwnd(), pMsg->hwnd))
				At = i;
		for (size_t Step = 1; Step < Count; ++Step) {
			CWnd *pNext = ORDER[(At + (Shift ? Count - Step : Step)) % Count];
			if (pNext->IsWindowEnabled()) {
				pNext->SetFocus();
				break;
			}
		}
		return TRUE;
	}
	if (pMsg->hwnd == I.QuestionBox.GetSafeHwnd()) {
		if (Down && Code == VK_RETURN && !Shift && !Control) {
			OnSend();
			return TRUE;
		}
		if (pMsg->message == WM_CHAR && (Code == '\r' || Code == '\n') && !Shift)
			return TRUE;		// the Enter that sent
		if (Down && Control && Code == 'A') {
			I.QuestionBox.SetSel(0, -1);
			return TRUE;
		}
	}
	if (pMsg->hwnd == I.Talk.GetSafeHwnd()) {
		// The conversation scrolls and copies; FamiTracker's shortcuts work there (Ctrl+Z after Do it)
		if (Down) {
			switch (Code) {
			case VK_UP: I.ScrollTo(I.Scroll - SY(LINE)); return TRUE;
			case VK_DOWN: I.ScrollTo(I.Scroll + SY(LINE)); return TRUE;
			case VK_PRIOR: I.ScrollTo(I.Scroll - SY(LINE) * 8); return TRUE;
			case VK_NEXT: I.ScrollTo(I.Scroll + SY(LINE) * 8); return TRUE;
			case VK_HOME: I.ScrollTo(0); return TRUE;
			case VK_END: I.ScrollTo(I.Content); return TRUE;
			default:
				if (Control && Code == 'C') {
					I.Copy(false);
					return TRUE;
				}
				break;
			}
		}
		return CWnd::PreTranslateMessage(pMsg);
	}
	const bool Typing = pMsg->hwnd == I.QuestionBox.GetSafeHwnd() || pMsg->hwnd == I.ModeBox.GetSafeHwnd() ||
		::IsChild(I.ModeBox.GetSafeHwnd(), pMsg->hwnd);
	const bool Pressing = Code == VK_SPACE || Code == VK_RETURN;	// a button's own keys
	if (!Typing && !Pressing)
		return CWnd::PreTranslateMessage(pMsg);		// a button has the keys: the shortcuts work
	if (!Typing || !Control || Code == VK_CONTROL || pMsg->hwnd == I.QuestionBox.GetSafeHwnd()) {
		// Straight to the control, past the shortcuts (Space is one, and the note keys' letters)
		::TranslateMessage(pMsg);
		::DispatchMessage(pMsg);
		return TRUE;
	}
	return CWnd::PreTranslateMessage(pMsg);		// Ctrl with the mode box: the shortcuts
}

void CAskPanelWnd::OnPaint()
{
	CPaintDC dc(this);
	CRect Client;
	GetClientRect(&Client);
	dc.FillSolidRect(0, 0, Client.Width(), SY(TOP_ROW), PANEL_BACK);
	const int Bottom = SY(QUESTION_HEIGHT) + SX(MARGIN);
	dc.FillSolidRect(0, Client.Height() - Bottom, Client.Width(), Bottom, PANEL_BACK);
}

BOOL CAskPanelWnd::OnEraseBkgnd(CDC *pDC)
{
	return TRUE;
}

void CAskPanelWnd::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	if (m_pImpl->Talk.GetSafeHwnd() != NULL)
		m_pImpl->Arrange(cx, cy);
	Invalidate(FALSE);
}

void CAskPanelWnd::OnTimer(UINT_PTR nIDEvent)
{
	Impl &I = *m_pImpl;
	if (nIDEvent == TMR_PAINT) {
		KillTimer(TMR_PAINT);
		I.Talk.Invalidate(FALSE);
		return;
	}
	if (nIDEvent == TMR_TICK) {
		if (I.Waiting)
			I.TakeReply();		// the dialog may have closed
		else if (I.Phase == PHASE_IDLE)
			KillTimer(TMR_TICK);
		I.Dirty = true;
		I.Talk.Invalidate(FALSE);
		return;
	}
	if (nIDEvent == TMR_LOOK && I.Open)
		I.FollowSong();
	CWnd::OnTimer(nIDEvent);
}

void CAskPanelWnd::OnDestroy()
{
	Impl &I = *m_pImpl;
	if (I.Job)
		I.Job->Detach();
	if (I.Resolving)
		I.Resolving->Detach();
	I.Job.reset();
	I.Resolving.reset();
	KillTimer(TMR_LOOK);
	KillTimer(TMR_TICK);
	KillTimer(TMR_PAINT);
	CWnd::OnDestroy();
}

void CAskPanelWnd::OnContextMenu(CWnd *pWnd, CPoint point)
{
	if (point.x == -1 && point.y == -1) {
		point = CPoint(SX(20), SY(20));
		ClientToScreen(&point);
	}
	m_pImpl->Menu(point);
}

void CAskPanelWnd::OnRButtonUp(UINT nFlags, CPoint point)
{
	ClientToScreen(&point);
	m_pImpl->Menu(point);
}

void CAskPanelWnd::OnSend()
{
	m_pImpl->Send();
}

void CAskPanelWnd::OnStop()
{
	m_pImpl->Stop();
}

void CAskPanelWnd::OnNewQuestion()
{
	m_pImpl->NewQuestion();
}

void CAskPanelWnd::OnSettings()
{
	m_pImpl->ShowSettings();
}

void CAskPanelWnd::OnSetupHelp()
{
	m_pImpl->ShowSetupHelp();
}

void CAskPanelWnd::OnModeChange()
{
	Impl &I = *m_pImpl;
	I.Mode = I.ModeBox.GetCurSel() == 1 ? ftapi::ASSISTANT_LET_IT_WORK : ftapi::ASSISTANT_ASK_FIRST;
	theApp.WriteProfileInt(SETTINGS, _T("Mode"), I.Mode == ftapi::ASSISTANT_LET_IT_WORK ? 1 : 0);
	I.QuestionBox.SetFocus();
}

LRESULT CAskPanelWnd::OnJobMessage(WPARAM wParam, LPARAM lParam)
{
	Impl &I = *m_pImpl;
	const int Event = static_cast<int>(wParam & 15), Serial = static_cast<int>(wParam >> 4);
	if (Event == EV_NEXT_ROUND) {
		if (I.Phase == PHASE_IDLE && !I.StopAsked && I.pTalk != NULL && I.pTalk->Chat.State() == ftapi::AssistantChat::MODEL)
			I.Round();
		return 0;
	}
	I.Event(Event, reinterpret_cast<ask_job_t*>(lParam), Serial);
	return 0;
}

// ---- The bar it is docked in, or its own window (PanelBar.h)

CAskBar::CAskBar() : CPanelBar(CMainFrame::PANEL_ASK, SECTION, _T("Ask"), DEFAULT_HEIGHT, DEFAULT_WIDTH, MIN_HEIGHT, MIN_WIDTH)
{
}

CAskBar::~CAskBar()
{
}

bool CAskBar::Create(CFrameWnd *pFrame, CMcpConnection *pTools)
{
	if (!CreateBar(pFrame, AFX_IDW_CONTROLBAR_FIRST + 54) || !m_wndPanel.Create(this, pTools))
		return false;
	SetPanel(&m_wndPanel);
	return true;
}

void CAskBar::PanelOpened(bool Open)
{
	m_wndPanel.Opened(Open);
}

void CAskBar::SongChanged()
{
	m_wndPanel.SongChanged();
}

void CAskBar::Focus()
{
	m_wndPanel.FocusQuestion();
}

bool CAskBar::WasOpen()
{
	return theApp.GetProfileInt(SECTION, _T("Open"), 0) != 0;
}

int CAskBar::PanelLeast(int Width) const
{
	// The question box, its buttons and a few lines of the conversation
	return SY(ROW_LEAST);
}
