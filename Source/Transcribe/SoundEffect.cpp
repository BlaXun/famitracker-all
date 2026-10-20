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

// Sound effects as FamiTracker instruments: see SoundEffect.h

#include "SoundEffect.h"

#include <algorithm>
#include <cmath>

namespace transcribe {

namespace {

const double CPU_NTSC = 1789773.0;
const double CPU_PAL = 1662607.0;
const int NOTES = 96;	// C-0 to B-7

// The noise channel's periods in CPU cycles, by register value (0 the highest)
const int NOISE_NTSC[16] = { 4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068 };
const int NOISE_PAL[16] = { 4, 8, 14, 30, 60, 88, 118, 148, 188, 236, 354, 472, 708, 944, 1890, 3778 };

int Clamp(int Value, int Low, int High)
{
	return std::max(Low, std::min(High, Value));
}

int Round(double Value)
{
	return static_cast<int>(std::floor(Value + 0.5));
}

// The period FamiTracker's table holds for a note (CSoundGen::LoadMachineSettings:
// a 32.7032 Hz C-0, the CPU clock over 16, less a half, cut), and the
// unrounded one for pitches between notes
double ExactPeriod(double Note, bool Pal)
{
	const double Frequency = 32.7032 * std::pow(2.0, Note / 12.0);
	return (Pal ? CPU_PAL : CPU_NTSC) / 16.0 / Frequency - 0.5;
}

int TablePeriod(int Note, bool Pal)
{
	return static_cast<int>(ExactPeriod(Clamp(Note, 0, NOTES - 1), Pal));
}

}	// namespace

double SfxLayer::pitchAt(int tick) const
{
	if (pitch.empty())
		return 0.0;
	return pitch[static_cast<size_t>(std::max(0, std::min(tick, static_cast<int>(pitch.size()) - 1)))];
}

int SfxLayer::dutyAt(int tick) const
{
	if (duty.empty())
		return channel == SfxChannel::Noise ? 0 : 2;
	return duty[static_cast<size_t>(std::max(0, std::min(tick, static_cast<int>(duty.size()) - 1)))];
}

int SoundEffect::length() const
{
	int End = 0;
	for (size_t i = 0; i < layers.size(); ++i)
		End = std::max(End, layers[i].start + layers[i].length());
	return End;
}

SfxInstrument ToInstrument(const SfxLayer &layer, bool pal)
{
	SfxInstrument Out;
	const int Ticks = std::min(layer.length(), SFX_MAX_TICKS);

	// The volume, ending in silence
	for (int t = 0; t < Ticks; ++t)
		Out.volume.push_back(Clamp(layer.volume[static_cast<size_t>(t)], 0, 15));
	if (Out.volume.empty() || Out.volume.back() != 0) {
		if (static_cast<int>(Out.volume.size()) < SFX_MAX_TICKS)
			Out.volume.push_back(0);
		else
			Out.volume.back() = 0;
	}

	// The duty or noise mode: one item when it stays the same
	std::vector<int> Duty;
	bool DutyMoves = false;
	for (int t = 0; t < Ticks; ++t) {
		const int Value = layer.channel == SfxChannel::Noise ? Clamp(layer.dutyAt(t), 0, 1) : Clamp(layer.dutyAt(t), 0, 3);
		DutyMoves = DutyMoves || (!Duty.empty() && Value != Duty.front());
		Duty.push_back(Value);
	}
	if (Duty.empty())
		Duty.push_back(layer.dutyAt(0));
	if (!DutyMoves)
		Duty.resize(1);

	if (layer.channel == SfxChannel::Noise) {
		// The pitch is the noise's own (0-15), set each tick by a fixed arpeggio
		Out.note = Clamp(Round(layer.pitchAt(0)), 0, 15);
		std::vector<int> Arpeggio;
		bool Moves = false;
		for (int t = 0; t < Ticks; ++t) {
			const int Value = Clamp(Round(layer.pitchAt(t)), 0, 15);
			Moves = Moves || Value != Out.note;
			Arpeggio.push_back(Value);
		}
		if (Moves) {
			Out.arpeggio = Arpeggio;
			Out.fixedArpeggio = true;
		}
		if (DutyMoves || Duty.front() != 0)
			Out.duty = Duty;
		return Out;
	}

	// Pulse and triangle: each tick the absolute arpeggio picks the nearest
	// note, and the pitch sequence moves the period from it to the pitch
	// between notes (the arpeggio sets the period anew every tick, so the
	// offsets don't add up)
	Out.note = Clamp(Round(layer.pitchAt(0)), 0, NOTES - 1);
	std::vector<int> Arpeggio, Fine;
	bool Moves = false, Between = false;
	for (int t = 0; t < Ticks; ++t) {
		const double Pitch = std::max(0.0, std::min(static_cast<double>(NOTES - 1), layer.pitchAt(t)));
		const int Note = Round(Pitch);
		Arpeggio.push_back(Note - Out.note);
		Moves = Moves || Note != Out.note;
		int Offset = 0;
		if (std::fabs(Pitch - Note) > 1e-6)
			Offset = Clamp(Round(ExactPeriod(Pitch, pal) - TablePeriod(Note, pal)), -128, 127);
		Between = Between || Offset != 0;
		Fine.push_back(Offset);
	}
	if (Moves || Between)
		Out.arpeggio = Arpeggio;
	if (Between)
		Out.pitch = Fine;
	if (layer.channel == SfxChannel::Pulse)
		Out.duty = Duty;	// FamiTracker's own default is the thin 12.5%
	return Out;
}

Audio RenderEffect(const SoundEffect &effect, int sampleRate, bool pal)
{
	Audio Out;
	Out.sampleRate = sampleRate;
	const double Cpu = pal ? CPU_PAL : CPU_NTSC;
	const int FrameRate = pal ? 50 : 60;
	const int *const NoisePeriods = pal ? NOISE_PAL : NOISE_NTSC;

	// Each layer's channel, as FamiTracker's sequence handler drives it
	struct Voice {
		SfxChannel Channel;
		int Start;
		SfxInstrument Instrument;
		int Period, Volume, Duty, Noise;
		double Phase;
		unsigned int Shift;
		double Clock;
	};
	std::vector<Voice> Voices;
	int Ticks = 0;
	for (size_t i = 0; i < effect.layers.size(); ++i) {
		const SfxLayer &Layer = effect.layers[i];
		Voice V;
		V.Channel = Layer.channel;
		V.Start = Layer.start;
		V.Instrument = ToInstrument(Layer, pal);
		V.Period = 0;
		V.Volume = 0;
		V.Duty = 0;
		V.Noise = 0;
		V.Phase = 0.0;
		V.Shift = 1;
		V.Clock = 0.0;
		Ticks = std::max(Ticks, Layer.start + static_cast<int>(V.Instrument.volume.size()) + 1);
		Voices.push_back(V);
	}

	static const int DUTY[4][8] = { { 0, 1, 0, 0, 0, 0, 0, 0 }, { 0, 1, 1, 0, 0, 0, 0, 0 }, { 0, 1, 1, 1, 1, 0, 0, 0 },
		{ 1, 0, 0, 1, 1, 1, 1, 1 } };
	static const int TRIANGLE[32] = { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
	double Previous = 0.0, Filtered = 0.0;
	const double HighPass = std::exp(-2.0 * 3.14159265358979 * 20.0 / sampleRate);
	long long Written = 0;
	for (int Tick = 0; Tick < Ticks; ++Tick) {
		for (size_t v = 0; v < Voices.size(); ++v) {
			Voice &V = Voices[v];
			const int t = Tick - V.Start;
			if (t < 0)
				continue;
			const SfxInstrument &I = V.Instrument;
			if (t == 0) {	// the note
				V.Period = V.Channel == SfxChannel::Noise ? I.note : TablePeriod(I.note, pal);
				V.Volume = 15;
				V.Duty = 0;
			}
			const size_t Item = static_cast<size_t>(t);
			if (Item < I.volume.size())
				V.Volume = I.volume[Item];
			if (Item < I.arpeggio.size())
				V.Period = I.fixedArpeggio ? I.arpeggio[Item] : TablePeriod(I.note + I.arpeggio[Item], pal);
			else if (Item == I.arpeggio.size() && I.fixedArpeggio)
				V.Period = I.note;	// a fixed arpeggio's end goes back to the note
			if (Item < I.pitch.size())
				V.Period += I.pitch[Item];
			if (Item < I.duty.size())
				V.Duty = I.duty[Item];
		}
		const long long End = static_cast<long long>(Tick + 1) * sampleRate / FrameRate;
		for (; Written < End; ++Written) {
			double Pulses = 0.0, TriangleOut = 0.0, NoiseOut = 0.0;
			for (size_t v = 0; v < Voices.size(); ++v) {
				Voice &V = Voices[v];
				if (Tick < V.Start)
					continue;
				switch (V.Channel) {
				case SfxChannel::Pulse:
					if (V.Period >= 8 && V.Period <= 0x7FF && V.Volume > 0) {
						V.Phase += Cpu / (16.0 * (V.Period + 1)) / sampleRate;
						V.Phase -= std::floor(V.Phase);
						Pulses += DUTY[Clamp(V.Duty, 0, 3)][static_cast<int>(V.Phase * 8.0) & 7] * V.Volume;
					}
					break;
				case SfxChannel::Triangle:
					if (V.Period >= 2 && V.Volume > 0) {
						V.Phase += Cpu / (32.0 * (V.Period + 1)) / sampleRate;
						V.Phase -= std::floor(V.Phase);
					}
					TriangleOut += TRIANGLE[static_cast<int>(V.Phase * 32.0) & 31];
					break;
				case SfxChannel::Noise:
					if (V.Volume > 0) {
						// The noise register is 15 - pitch; each period's end shifts the register
						V.Clock += Cpu / NoisePeriods[15 - Clamp(V.Period & 0x0F, 0, 15)] / sampleRate;
						while (V.Clock >= 1.0) {
							const unsigned int Tap = (V.Duty & 1) ? 6 : 1;
							const unsigned int Feedback = (V.Shift ^ (V.Shift >> Tap)) & 1;
							V.Shift = (V.Shift >> 1) | (Feedback << 14);
							V.Clock -= 1.0;
						}
						NoiseOut += (V.Shift & 1) ? 0 : V.Volume;
					}
					break;
				}
			}
			// The NES's mixer, then a high-pass like the console's
			const double PulseMix = Pulses > 0.0 ? 95.88 / (8128.0 / Pulses + 100.0) : 0.0;
			const double Tnd = TriangleOut / 8227.0 + NoiseOut / 12241.0;
			const double TndMix = Tnd > 0.0 ? 159.79 / (1.0 / Tnd + 100.0) : 0.0;
			const double Mixed = PulseMix + TndMix;
			Filtered = HighPass * (Filtered + Mixed - Previous);
			Previous = Mixed;
			Out.samples.push_back(static_cast<float>(std::max(-1.0, std::min(1.0, Filtered))));
		}
	}
	return Out;
}

namespace {

// The noise index (0-15, 15 the brightest) whose sound has its zero
// crossings nearest to a recording's: the noise register shifts once a
// period, and its output changes, crossing zero, at about every other shift
int NoiseIndexFor(double CrossingsPerSecond, bool Pal)
{
	const double Cpu = Pal ? CPU_PAL : CPU_NTSC;
	const int *const Periods = Pal ? NOISE_PAL : NOISE_NTSC;
	int Best = 0;
	double BestDistance = 1e9;
	for (int n = 0; n < 16; ++n) {
		const double Crossings = Cpu / Periods[15 - n] / 2.0;
		const double Distance = std::fabs(std::log(std::max(1.0, CrossingsPerSecond)) - std::log(Crossings));
		if (Distance < BestDistance) {
			BestDistance = Distance;
			Best = n;
		}
	}
	return Best;
}

double Median(std::vector<double> Values)
{
	if (Values.empty())
		return 0.0;
	std::sort(Values.begin(), Values.end());
	return Values[Values.size() / 2];
}

// A layer from per-tick volumes and pitches, without the silent ticks before
// and after, nor quiet ticks on their own; false when fewer than two ticks
// sound
bool LayerFrom(SfxChannel Channel, std::vector<int> Volume, const std::vector<double> &Pitch, SfxLayer &Out, bool &Cut)
{
	for (size_t t = 0; t < Volume.size(); ++t) {
		bool Alone = Volume[t] <= 2;
		for (size_t u = t >= 2 ? t - 2 : 0; u < std::min(Volume.size(), t + 3) && Alone; ++u)
			Alone = u == t || Volume[u] == 0;
		if (Alone)
			Volume[t] = 0;
	}
	int First = -1, Last = -1, Sounding = 0;
	for (size_t t = 0; t < Volume.size(); ++t) {
		if (Volume[t] <= 0)
			continue;
		if (First < 0)
			First = static_cast<int>(t);
		Last = static_cast<int>(t);
		++Sounding;
	}
	if (Sounding < 2)
		return false;
	if (Last - First + 1 > SFX_MAX_TICKS) {
		Last = First + SFX_MAX_TICKS - 1;
		Cut = true;
	}
	Out = SfxLayer();
	Out.channel = Channel;
	Out.start = First;
	for (int t = First; t <= Last; ++t) {
		Out.volume.push_back(Volume[static_cast<size_t>(t)]);
		Out.pitch.push_back(Pitch[static_cast<size_t>(t)]);
	}
	Out.duty.push_back(Channel == SfxChannel::Noise ? 0 : 2);
	return true;
}

}	// namespace

bool VoiceToEffect(const Audio &audio, const VoiceEffectOptions &options, SoundEffect &effect, VoiceEffectInfo *info,
	std::string *problem)
{
	effect = SoundEffect();
	if (info != nullptr)
		*info = VoiceEffectInfo();
	auto Fail = [&](const char *Why) {
		if (problem != nullptr)
			*problem = Why;
		return false;
	};
	if (audio.sampleRate <= 0 || audio.samples.empty())
		return Fail("The recording is empty.");
	const int Rate = audio.sampleRate;
	const double Tick = 1.0 / (options.pal ? 50.0 : 60.0);
	const size_t PerTick = static_cast<size_t>(std::max(1, Round(Rate * Tick)));
	const size_t Ticks = (audio.samples.size() + PerTick - 1) / PerTick;

	// Loudness and zero crossings, a tick at a time
	std::vector<double> Level(Ticks, 0.0), Crossings(Ticks, 0.0);
	for (size_t t = 0; t < Ticks; ++t) {
		const size_t From = t * PerTick, To = std::min(audio.samples.size(), From + PerTick);
		double Mean = 0.0;
		for (size_t i = From; i < To; ++i)
			Mean += audio.samples[i];
		Mean /= static_cast<double>(To - From);
		double Energy = 0.0;
		int Changes = 0;
		for (size_t i = From; i < To; ++i) {
			const double x = audio.samples[i] - Mean;
			Energy += x * x;
			if (i > From && ((audio.samples[i - 1] - Mean < 0.0) != (x < 0.0)))
				++Changes;
		}
		Level[t] = std::sqrt(Energy / static_cast<double>(To - From));
		Crossings[t] = Changes * static_cast<double>(Rate) / static_cast<double>(std::max<size_t>(1, To - From - 1));
	}
	const double Peak = *std::max_element(Level.begin(), Level.end());
	if (Peak < 1e-4)
		return Fail("There's no sound in the recording.");
	const double Gate = std::max(Peak * 0.01, 0.001);	// 40 dB below the loudest tick, and above -60 dBFS

	// The voice's pitch, a tick at a time: the middle of the pitched frames,
	// and how much of the tick has a pitch
	PitchOptions Options;
	Options.minFrequency = 70.0;
	Options.maxFrequency = 3000.0;
	const std::vector<PitchFrame> Frames = TrackPitch(audio, Options);
	std::vector<double> Pitch(Ticks, 0.0), Voiced(Ticks, 0.0);
	{
		std::vector<std::vector<double> > Found(Ticks);
		std::vector<int> Count(Ticks, 0);
		for (size_t i = 0; i < Frames.size(); ++i) {
			const size_t t = static_cast<size_t>(std::max(0.0, Frames[i].time / Tick));
			if (t >= Ticks)
				continue;
			++Count[t];
			if (Frames[i].frequency > 0.0 && Frames[i].aperiodicity < 0.25)
				Found[t].push_back(12.0 * std::log2(Frames[i].frequency / 440.0) + 45.0);
		}
		for (size_t t = 0; t < Ticks; ++t) {
			Voiced[t] = Count[t] > 0 ? static_cast<double>(Found[t].size()) / Count[t] : 0.0;
			if (!Found[t].empty())
				Pitch[t] = Median(Found[t]);
		}
	}
	// A tick whose pitch is an octave off from its neighbours' (a slip of the
	// tracker) joins them; short unpitched gaps inside the voice are bridged
	std::vector<double> Fixed = Pitch;
	for (size_t t = 0; t < Ticks; ++t) {
		if (Pitch[t] <= 0.0)
			continue;
		std::vector<double> Around;
		for (size_t u = t >= 3 ? t - 3 : 0; u < std::min(Ticks, t + 4); ++u)
			if (Pitch[u] > 0.0)
				Around.push_back(Pitch[u]);
		const double Middle = Median(Around);
		if (std::fabs(Pitch[t] - Middle) > 9.0)
			Fixed[t] = Pitch[t] + 12.0 * Round((Middle - Pitch[t]) / 12.0);
	}
	Pitch = Fixed;
	for (size_t t = 1; t + 1 < Ticks; ++t) {
		if (Pitch[t] > 0.0 || Level[t] < Gate)
			continue;
		size_t Next = t;
		while (Next < Ticks && Pitch[Next] <= 0.0 && Next - t < 3)
			++Next;
		if (Next < Ticks && Pitch[Next] > 0.0 && Pitch[t - 1] > 0.0 && Next - t <= 2) {
			for (size_t u = t; u < Next; ++u) {
				Pitch[u] = Pitch[t - 1] + (Pitch[Next] - Pitch[t - 1]) * static_cast<double>(u - t + 1) / static_cast<double>(Next - t + 1);
				Voiced[u] = std::max(Voiced[u], 0.5);
			}
		}
	}

	// Whole octaves: as asked, or where the middle of the voice lands near the target
	std::vector<double> Pitched;
	for (size_t t = 0; t < Ticks; ++t)
		if (Pitch[t] > 0.0 && Level[t] >= Gate)
			Pitched.push_back(Pitch[t]);
	int Octaves = options.octaves;
	if (!Pitched.empty() && options.fitOctave)
		Octaves = Round((options.targetNote - Median(Pitched)) / 12.0);
	if (!Pitched.empty()) {
		const double Low = *std::min_element(Pitched.begin(), Pitched.end());
		const double High = *std::max_element(Pitched.begin(), Pitched.end());
		while (High + 12.0 * Octaves > 95.0 && Low + 12.0 * (Octaves - 1) >= 9.0)
			--Octaves;
		while (Low + 12.0 * Octaves < 9.0 && High + 12.0 * (Octaves + 1) <= 95.0)
			++Octaves;
	}

	// The layers: the volume follows the loudness, split between the pitched
	// part on a pulse and the rest on the noise
	std::vector<int> PulseVolume(Ticks, 0), NoiseVolume(Ticks, 0);
	std::vector<double> PulsePitch(Ticks, 0.0), NoisePitch(Ticks, 0.0);
	double Held = Pitched.empty() ? 0.0 : -1.0;
	for (size_t t = 0; t < Ticks; ++t) {
		if (Pitch[t] > 0.0) {
			Held = Pitch[t];
			break;
		}
	}
	for (size_t t = 0; t < Ticks; ++t) {
		if (Pitch[t] > 0.0)
			Held = Pitch[t];
		PulsePitch[t] = std::max(9.0, std::min(95.0, Held + 12.0 * Octaves));
		NoisePitch[t] = NoiseIndexFor(Crossings[t], options.pal);
		if (Level[t] < Gate)
			continue;
		const double Volume = 15.0 * std::pow(Level[t] / Peak, 0.7);
		const double Share = Pitch[t] > 0.0 ? std::min(1.0, Voiced[t]) : 0.0;
		PulseVolume[t] = Pitched.empty() ? 0 : Round(Volume * Share);
		NoiseVolume[t] = options.noise ? Round(Volume * (1.0 - Share)) : 0;
	}
	bool Cut = false;
	SfxLayer Layer;
	if (LayerFrom(SfxChannel::Pulse, PulseVolume, PulsePitch, Layer, Cut))
		effect.layers.push_back(Layer);
	if (LayerFrom(SfxChannel::Noise, NoiseVolume, NoisePitch, Layer, Cut))
		effect.layers.push_back(Layer);
	if (effect.layers.empty())
		return Fail(options.noise ? "There's no clear sound in the recording." :
			"There's no pitch in the recording to play on a pulse (breathy and hissy sounds need the noise channel).");

	// Both start where the sound starts
	int First = effect.layers[0].start;
	for (size_t l = 1; l < effect.layers.size(); ++l)
		First = std::min(First, effect.layers[l].start);
	for (size_t l = 0; l < effect.layers.size(); ++l)
		effect.layers[l].start -= First;
	if (info != nullptr) {
		info->octaves = Octaves;
		info->seconds = effect.length() * Tick;
		info->cut = Cut;
		for (size_t l = 0; l < effect.layers.size(); ++l) {
			info->pitched = info->pitched || effect.layers[l].channel == SfxChannel::Pulse;
			info->noisy = info->noisy || effect.layers[l].channel == SfxChannel::Noise;
		}
	}
	return true;
}

}	// namespace transcribe
