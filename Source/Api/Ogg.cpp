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

#include "Ogg.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#define OV_EXCLUDE_STATIC_CALLBACKS	// vorbisfile.h's file callbacks aren't used here
#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>
#include <vorbis/vorbisfile.h>

namespace ftapi {

namespace {

void WritePage(std::string &Out, const ogg_page &Page)
{
	Out.append(reinterpret_cast<const char*>(Page.header), static_cast<size_t>(Page.header_len));
	Out.append(reinterpret_cast<const char*>(Page.body), static_cast<size_t>(Page.body_len));
}

// The stream's serial number, the same for the same audio
int Serial(const std::vector<const std::vector<float>*> &Channels, int SampleRate)
{
	unsigned long Hash = 2166136261ul ^ static_cast<unsigned long>(SampleRate);
	for (size_t c = 0; c < Channels.size(); ++c) {
		const std::vector<float> &Samples = *Channels[c];
		const size_t Step = std::max<size_t>(1, Samples.size() / 4096);
		for (size_t i = 0; i < Samples.size(); i += Step)
			Hash = (Hash ^ static_cast<unsigned long>(static_cast<long>(Samples[i] * 32767.0f) & 0xFFFF)) * 16777619ul;
		Hash ^= static_cast<unsigned long>(Samples.size());
	}
	return static_cast<int>(Hash & 0x7FFFFFFF);
}

// vorbisfile reading from memory
struct MemoryFile {
	const std::string *pData;
	size_t Position;
};

size_t ReadMemory(void *pBuffer, size_t Size, size_t Count, void *pSource)
{
	MemoryFile *pFile = static_cast<MemoryFile*>(pSource);
	if (Size == 0)
		return 0;
	const size_t Taken = std::min(Size * Count, pFile->pData->size() - pFile->Position) / Size * Size;
	std::memcpy(pBuffer, pFile->pData->data() + pFile->Position, Taken);
	pFile->Position += Taken;
	return Taken / Size;
}

int SeekMemory(void *pSource, ogg_int64_t Offset, int Whence)
{
	MemoryFile *pFile = static_cast<MemoryFile*>(pSource);
	ogg_int64_t To = Offset;
	if (Whence == SEEK_CUR)
		To += static_cast<ogg_int64_t>(pFile->Position);
	else if (Whence == SEEK_END)
		To += static_cast<ogg_int64_t>(pFile->pData->size());
	if (To < 0 || To > static_cast<ogg_int64_t>(pFile->pData->size()))
		return -1;
	pFile->Position = static_cast<size_t>(To);
	return 0;
}

long TellMemory(void *pSource)
{
	return static_cast<long>(static_cast<MemoryFile*>(pSource)->Position);
}

// EncodeOgg's work, for one channel or two (as long as each other)
bool Encode(const std::vector<const std::vector<float>*> &Channels, int SampleRate, float Quality, const OggComments &Comments,
	std::string &Out, std::string &Error)
{
	Out.clear();
	size_t Length = Channels[0]->size();
	for (size_t c = 1; c < Channels.size(); ++c)
		Length = std::min(Length, Channels[c]->size());
	vorbis_info Info;
	vorbis_info_init(&Info);
	if (SampleRate < 8000 || SampleRate > 192000 ||
		vorbis_encode_init_vbr(&Info, static_cast<long>(Channels.size()), SampleRate, std::max(-0.1f, std::min(1.0f, Quality))) != 0) {
		vorbis_info_clear(&Info);
		char Text[96];
		std::snprintf(Text, sizeof(Text), "The OGG encoder can't use a sample rate of %d Hz.", SampleRate);
		Error = Text;
		return false;
	}
	vorbis_comment Comment;
	vorbis_comment_init(&Comment);
	for (size_t i = 0; i < Comments.size(); ++i)
		vorbis_comment_add_tag(&Comment, Comments[i].first.c_str(), Comments[i].second.c_str());
	vorbis_dsp_state State;
	vorbis_block Block;
	vorbis_analysis_init(&State, &Info);
	vorbis_block_init(&State, &Block);
	ogg_stream_state Stream;
	ogg_stream_init(&Stream, Serial(Channels, SampleRate));

	// The three headers, then the audio from a page of its own
	ogg_packet Header, HeaderComment, HeaderCode;
	vorbis_analysis_headerout(&State, &Comment, &Header, &HeaderComment, &HeaderCode);
	ogg_stream_packetin(&Stream, &Header);
	ogg_stream_packetin(&Stream, &HeaderComment);
	ogg_stream_packetin(&Stream, &HeaderCode);
	ogg_page Page;
	while (ogg_stream_flush(&Stream, &Page) != 0)
		WritePage(Out, Page);

	const size_t CHUNK = 4096;
	size_t Done = 0;
	bool Ended = false, Wrote = false;
	while (!Ended) {
		const size_t Count = std::min(CHUNK, Length - Done);
		if (Count > 0) {
			float **ppBuffer = vorbis_analysis_buffer(&State, static_cast<int>(Count));
			for (size_t c = 0; c < Channels.size(); ++c) {
				const std::vector<float> &Samples = *Channels[c];
				for (size_t i = 0; i < Count; ++i)
					ppBuffer[c][i] = std::max(-1.0f, std::min(1.0f, Samples[Done + i]));
			}
			vorbis_analysis_wrote(&State, static_cast<int>(Count));
			Done += Count;
		}
		else {
			if (Wrote)
				break;	// the last page should have come out already
			vorbis_analysis_wrote(&State, 0);	// the end
			Wrote = true;
		}
		while (vorbis_analysis_blockout(&State, &Block) == 1) {
			vorbis_analysis(&Block, NULL);
			vorbis_bitrate_addblock(&Block);
			ogg_packet Packet;
			while (vorbis_bitrate_flushpacket(&State, &Packet) == 1) {
				ogg_stream_packetin(&Stream, &Packet);
				while (!Ended && ogg_stream_pageout(&Stream, &Page) != 0) {
					WritePage(Out, Page);
					Ended = ogg_page_eos(&Page) != 0;
				}
			}
		}
	}
	while (ogg_stream_flush(&Stream, &Page) != 0)
		WritePage(Out, Page);

	ogg_stream_clear(&Stream);
	vorbis_block_clear(&Block);
	vorbis_dsp_clear(&State);
	vorbis_comment_clear(&Comment);
	vorbis_info_clear(&Info);
	return true;
}

}	// namespace

bool EncodeOgg(const std::vector<float> &Samples, int SampleRate, float Quality, const OggComments &Comments, std::string &Out,
	std::string &Error)
{
	return Encode(std::vector<const std::vector<float>*>(1, &Samples), SampleRate, Quality, Comments, Out, Error);
}

bool EncodeOgg(const std::vector<float> &Left, const std::vector<float> &Right, int SampleRate, float Quality,
	const OggComments &Comments, std::string &Out, std::string &Error)
{
	std::vector<const std::vector<float>*> Channels;
	Channels.push_back(&Left);
	Channels.push_back(&Right);
	return Encode(Channels, SampleRate, Quality, Comments, Out, Error);
}

bool DecodeOgg(const std::string &File, std::vector<float> &Samples, int &SampleRate, int &Channels, OggComments &Comments,
	std::string &Error, std::vector<float> *pSecond)
{
	Samples.clear();
	if (pSecond != 0)
		pSecond->clear();
	Comments.clear();
	MemoryFile Source = { &File, 0 };
	ov_callbacks Callbacks;
	Callbacks.read_func = &ReadMemory;
	Callbacks.seek_func = &SeekMemory;
	Callbacks.close_func = NULL;
	Callbacks.tell_func = &TellMemory;
	OggVorbis_File Vorbis;
	if (ov_open_callbacks(&Source, &Vorbis, NULL, 0, Callbacks) < 0) {
		Error = "It isn't an OGG Vorbis file.";
		return false;
	}
	const vorbis_info *pInfo = ov_info(&Vorbis, -1);
	SampleRate = pInfo != NULL ? static_cast<int>(pInfo->rate) : 0;
	Channels = pInfo != NULL ? pInfo->channels : 0;
	const vorbis_comment *pComment = ov_comment(&Vorbis, -1);
	for (int i = 0; pComment != NULL && i < pComment->comments; ++i) {
		const std::string Text(pComment->user_comments[i], static_cast<size_t>(pComment->comment_lengths[i]));
		const size_t Equals = Text.find('=');
		Comments.push_back(Equals == std::string::npos ? std::make_pair(Text, std::string()) :
			std::make_pair(Text.substr(0, Equals), Text.substr(Equals + 1)));
	}
	bool Good = true;
	for (;;) {
		float **ppPcm = NULL;
		int Section = 0;
		const long Count = ov_read_float(&Vorbis, &ppPcm, 4096, &Section);
		if (Count == 0)
			break;
		if (Count < 0) {
			Error = "The OGG file is damaged.";
			Good = false;
			break;
		}
		Samples.insert(Samples.end(), ppPcm[0], ppPcm[0] + Count);
		if (pSecond != 0)
			pSecond->insert(pSecond->end(), ppPcm[Channels > 1 ? 1 : 0], ppPcm[Channels > 1 ? 1 : 0] + Count);
	}
	ov_clear(&Vorbis);
	return Good;
}

bool ReadOggComments(const std::string &File, OggComments &Comments, std::string &Error)
{
	Comments.clear();
	MemoryFile Source = { &File, 0 };
	ov_callbacks Callbacks;
	Callbacks.read_func = &ReadMemory;
	Callbacks.seek_func = &SeekMemory;
	Callbacks.close_func = NULL;
	Callbacks.tell_func = &TellMemory;
	OggVorbis_File Vorbis;
	if (ov_open_callbacks(&Source, &Vorbis, NULL, 0, Callbacks) < 0) {
		Error = "It isn't an OGG Vorbis file.";
		return false;
	}
	const vorbis_comment *pComment = ov_comment(&Vorbis, -1);
	for (int i = 0; pComment != NULL && i < pComment->comments; ++i) {
		const std::string Text(pComment->user_comments[i], static_cast<size_t>(pComment->comment_lengths[i]));
		const size_t Equals = Text.find('=');
		Comments.push_back(Equals == std::string::npos ? std::make_pair(Text, std::string()) :
			std::make_pair(Text.substr(0, Equals), Text.substr(Equals + 1)));
	}
	ov_clear(&Vorbis);
	return true;
}

}	// namespace ftapi
