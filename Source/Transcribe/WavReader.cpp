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

// Minimal RIFF/WAVE reader and writer for the transcription engine.

#include "Transcribe.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

namespace transcribe {

namespace {

struct FileCloser {
	void operator()(FILE *f) const { if (f) std::fclose(f); }
};
typedef std::unique_ptr<FILE, FileCloser> FilePtr;

uint32_t ReadU32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }
uint16_t ReadU16(const unsigned char *p) { return uint16_t(p[0] | (p[1] << 8)); }

bool Fail(std::string *error, const std::string &message)
{
	if (error)
		*error = message;
	return false;
}

const uint16_t FORMAT_PCM = 1;
const uint16_t FORMAT_FLOAT = 3;
const uint16_t FORMAT_EXTENSIBLE = 0xFFFE;

}	// namespace

bool ReadWav(const std::string &path, Audio &out, std::string *error)
{
	FilePtr file(std::fopen(path.c_str(), "rb"));
	if (!file)
		return Fail(error, "cannot open " + path);

	unsigned char header[12];
	if (std::fread(header, 1, 12, file.get()) != 12 || std::memcmp(header, "RIFF", 4) != 0 || std::memcmp(header + 8, "WAVE", 4) != 0)
		return Fail(error, path + " is not a WAV file");

	uint16_t format = 0, channels = 0, bits = 0, blockAlign = 0;
	uint32_t sampleRate = 0;
	bool haveFormat = false;
	std::vector<unsigned char> data;

	unsigned char chunk[8];
	while (std::fread(chunk, 1, 8, file.get()) == 8) {
		const uint32_t size = ReadU32(chunk + 4);
		if (std::memcmp(chunk, "fmt ", 4) == 0) {
			if (size < 16)
				return Fail(error, "malformed fmt chunk");
			std::vector<unsigned char> fmt(size);
			if (std::fread(fmt.data(), 1, size, file.get()) != size)
				return Fail(error, "truncated fmt chunk");
			format = ReadU16(&fmt[0]);
			channels = ReadU16(&fmt[2]);
			sampleRate = ReadU32(&fmt[4]);
			blockAlign = ReadU16(&fmt[12]);
			bits = ReadU16(&fmt[14]);
			if (format == FORMAT_EXTENSIBLE && size >= 26)
				format = ReadU16(&fmt[24]);	// first two bytes of the SubFormat GUID
			haveFormat = true;
		}
		else if (std::memcmp(chunk, "data", 4) == 0) {
			data.resize(size);
			const size_t got = std::fread(data.data(), 1, size, file.get());
			data.resize(got);	// tolerate files whose data size field is too large
			break;
		}
		else if (std::fseek(file.get(), long(size), SEEK_CUR) != 0) {
			return Fail(error, "truncated chunk");
		}
		if (size & 1)
			std::fseek(file.get(), 1, SEEK_CUR);	// chunks are word aligned
	}

	if (!haveFormat)
		return Fail(error, "no fmt chunk");
	if (channels == 0 || sampleRate == 0 || blockAlign == 0)
		return Fail(error, "invalid WAV format");

	const bool isFloat = (format == FORMAT_FLOAT);
	if (!(format == FORMAT_PCM && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) &&
		!(isFloat && (bits == 32 || bits == 64)))
		return Fail(error, "unsupported WAV encoding (use PCM 8/16/24/32-bit or float)");

	const size_t bytesPerSample = bits / 8;
	if (blockAlign < channels * bytesPerSample)
		return Fail(error, "invalid block alignment");
	const size_t frames = data.size() / blockAlign;

	out.sampleRate = int(sampleRate);
	out.samples.assign(frames, 0.0f);

	for (size_t i = 0; i < frames; ++i) {
		double sum = 0.0;
		for (int c = 0; c < channels; ++c) {
			const unsigned char *p = &data[i * blockAlign + c * bytesPerSample];
			double v = 0.0;
			if (isFloat) {
				if (bits == 32) {
					float f;
					std::memcpy(&f, p, 4);
					v = f;
				}
				else {
					double d;
					std::memcpy(&d, p, 8);
					v = d;
				}
			}
			else {
				switch (bits) {
				case 8:  v = (int(p[0]) - 128) / 128.0; break;
				case 16: v = int16_t(ReadU16(p)) / 32768.0; break;
				case 24: v = (int32_t((uint32_t(p[0]) << 8) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 24)) >> 8) / 8388608.0; break;
				case 32: v = int32_t(ReadU32(p)) / 2147483648.0; break;
				}
			}
			sum += v;
		}
		out.samples[i] = float(sum / channels);
	}
	return true;
}

bool WriteWav(const std::string &path, const Audio &audio, std::string *error)
{
	FilePtr file(std::fopen(path.c_str(), "wb"));
	if (!file)
		return Fail(error, "cannot create " + path);

	const uint32_t dataBytes = uint32_t(audio.samples.size() * 2);
	unsigned char h[44];
	auto put32 = [&h](int at, uint32_t v) {
		for (int b = 0; b < 4; ++b)
			h[at + b] = (unsigned char)((v >> (8 * b)) & 0xFF);
	};
	auto put16 = [&h](int at, uint16_t v) {
		h[at] = (unsigned char)(v & 0xFF);
		h[at + 1] = (unsigned char)(v >> 8);
	};
	std::memcpy(h, "RIFF", 4);
	put32(4, 36 + dataBytes);
	std::memcpy(h + 8, "WAVEfmt ", 8);
	put32(16, 16);
	put16(20, FORMAT_PCM);
	put16(22, 1);
	put32(24, uint32_t(audio.sampleRate));
	put32(28, uint32_t(audio.sampleRate) * 2);
	put16(32, 2);
	put16(34, 16);
	std::memcpy(h + 36, "data", 4);
	put32(40, dataBytes);
	if (std::fwrite(h, 1, 44, file.get()) != 44)
		return Fail(error, "write failed");

	std::vector<unsigned char> pcm(dataBytes);
	for (size_t i = 0; i < audio.samples.size(); ++i) {
		// Same scale as ReadWav (1/32768 per step), rounded to nearest
		long q = std::lround(double(audio.samples[i]) * 32768.0);
		if (q > 32767) q = 32767;
		if (q < -32768) q = -32768;
		const int16_t v = int16_t(q);
		pcm[i * 2] = uint8_t(v & 0xFF);
		pcm[i * 2 + 1] = uint8_t((uint16_t(v) >> 8) & 0xFF);
	}
	if (!pcm.empty() && std::fwrite(pcm.data(), 1, pcm.size(), file.get()) != pcm.size())
		return Fail(error, "write failed");
	return true;
}

}	// namespace transcribe
