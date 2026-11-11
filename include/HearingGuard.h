/*
 * HearingGuard.h - real-time output limiter and burst mute
 *
 * Copyright (c) 2026 LMMS developers
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#ifndef LMMS_HEARING_GUARD_H
#define LMMS_HEARING_GUARD_H

#include <atomic>
#include <cmath>

namespace lmms
{

//! Real-time hearing protection for a device buffer. Per-sample work is abs, compare, and multiply.
class HearingGuard
{
public:
	enum class Reason
	{
		None,
		LevelJump,
		FullScaleFraction,
		DcOffset
	};

	void setEnabled(bool enabled)
	{
		m_enabled = enabled;
		if (!enabled)
		{
			m_stage = Stage::Open;
			m_gain = 1.f;
			m_dcFrames = 0;
		}
	}

	bool enabled() const { return m_enabled; }

	float lastPeak() const { return m_lastPeak; }

	bool takeBurstNotice() { return m_burstNotice.exchange(false, std::memory_order_acq_rel); }

	//! @a sampleAt(channel, frame) returns a reference to that sample. Non-finite values become 0.
	//! When enabled, nothing louder than about -1 dBFS is passed, and a burst fades to silence.
	//! When disabled, values are hard-clipped at 1.
	template<typename SampleAt>
	Reason process(int channels, int frames, int sampleRate, SampleAt sampleAt)
	{
		m_lastReason = Reason::None;
		m_lastPeak = 0.f;
		if (channels <= 0 || frames <= 0) { return Reason::None; }

		const int rate = sampleRate > 0 ? sampleRate : 48000;
		const int total = channels * frames;
		float peak = 0.f;
		float sum = 0.f;
		int overs = 0;

		for (int frame = 0; frame < frames; ++frame)
		{
			for (int channel = 0; channel < channels; ++channel)
			{
				float sample = sampleAt(channel, frame);
				if (!std::isfinite(sample)) { sample = 0.f; }
				const float magnitude = std::fabs(sample);
				if (magnitude > peak) { peak = magnitude; }
				if (magnitude > 1.f) { ++overs; }
				sum += sample;
			}
		}
		m_lastPeak = peak;

		const int fadeFrames = rate / 200; // 5 ms
		const float step = fadeFrames > 0 ? 1.f / static_cast<float>(fadeFrames) : 1.f;
		Reason started = Reason::None;
		if (m_enabled && (m_stage == Stage::Open || m_stage == Stage::FadingIn))
		{
			const bool levelJump = peak > 0.9f && peak > m_runningPeak * 7.943282f;
			const bool tooManyOvers = overs * 20 > total;
			if (std::fabs(sum) > 0.25f * static_cast<float>(total)) { m_dcFrames += frames; }
			else { m_dcFrames = 0; }
			const bool dc = m_dcFrames > rate / 10;

			if (levelJump) { started = Reason::LevelJump; }
			else if (tooManyOvers) { started = Reason::FullScaleFraction; }
			else if (dc) { started = Reason::DcOffset; }

			if (started != Reason::None)
			{
				m_stage = Stage::FadingOut;
				m_fadeStep = step;
				m_lastReason = started;
				m_burstNotice.store(true, std::memory_order_release);
			}
		}

		for (int frame = 0; frame < frames; ++frame)
		{
			const float gain = m_enabled ? m_gain : 1.f;
			for (int channel = 0; channel < channels; ++channel)
			{
				float sample = sampleAt(channel, frame);
				sample = m_enabled ? limit(sample) : hardClip(sample);
				sampleAt(channel, frame) = sample * gain;
			}
			if (m_enabled) { advance(rate); }
		}

		const float window = static_cast<float>(rate) * 0.08f;
		float decay = 1.f - static_cast<float>(frames) / window;
		if (decay < 0.f) { decay = 0.f; }
		if (started == Reason::None) { m_runningPeak = peak > m_runningPeak * decay ? peak : m_runningPeak * decay; }
		else { m_runningPeak *= decay; }

		return started;
	}

private:
	enum class Stage
	{
		Open,
		FadingOut,
		Muted,
		FadingIn
	};

	static float hardClip(float sample)
	{
		if (!std::isfinite(sample)) { return 0.f; }
		if (sample > 1.f) { return 1.f; }
		if (sample < -1.f) { return -1.f; }
		return sample;
	}

	static float limit(float sample)
	{
		if (!std::isfinite(sample)) { return 0.f; }
		const float magnitude = std::fabs(sample);
		if (magnitude <= 0.7f) { return sample; }
		float shaped = 0.7f + kSlope * (magnitude - 0.7f);
		if (shaped > kCeiling) { shaped = kCeiling; }
		return sample < 0.f ? -shaped : shaped;
	}

	void advance(int rate)
	{
		switch (m_stage)
		{
		case Stage::FadingOut:
			m_gain -= m_fadeStep;
			if (m_gain <= 0.f)
			{
				m_gain = 0.f;
				m_stage = Stage::Muted;
				m_muteLeft = rate / 2;
				if (m_muteLeft < 1) { m_muteLeft = 1; }
			}
			break;
		case Stage::Muted:
			m_gain = 0.f;
			if (--m_muteLeft <= 0) { m_stage = Stage::FadingIn; }
			break;
		case Stage::FadingIn:
			m_gain += m_fadeStep;
			if (m_gain >= 1.f)
			{
				m_gain = 1.f;
				m_stage = Stage::Open;
			}
			break;
		case Stage::Open:
			m_gain = 1.f;
			break;
		}
	}

	static constexpr float kCeiling = 0.89f;
	static constexpr float kSlope = (kCeiling - 0.7f) / (2.f - 0.7f);

	bool m_enabled = true;
	Stage m_stage = Stage::Open;
	float m_gain = 1.f;
	float m_fadeStep = 0.02f;
	int m_muteLeft = 0;
	int m_dcFrames = 0;
	float m_runningPeak = 0.f;
	float m_lastPeak = 0.f;
	Reason m_lastReason = Reason::None;
	std::atomic<bool> m_burstNotice{false};
};

} // namespace lmms

#endif // LMMS_HEARING_GUARD_H
