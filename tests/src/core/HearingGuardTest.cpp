/*
 * HearingGuardTest.cpp - limiter and burst-mute checks
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

#include <cmath>
#include <limits>
#include <vector>

#include <QObject>
#include <QtTest>

#include "HearingGuard.h"

class HearingGuardTest : public QObject
{
	Q_OBJECT
private slots:
	void CeilingNanAndBurst()
	{
		using namespace lmms;

		constexpr int kRate = 48000;
		constexpr int kChannels = 2;
		constexpr int kFrames = 256;
		constexpr float kCeiling = 0.89f;

		auto run = [](HearingGuard& guard, std::vector<float>& interleaved) {
			guard.process(kChannels, kFrames, kRate, [&](int channel, int frame) -> float& {
				return interleaved[frame * kChannels + channel];
			});
		};

		HearingGuard ceiling;
		std::vector<float> hot(kFrames * kChannels, 0.2f);
		hot[0] = 100.f;
		hot[1] = -100.f;
		hot[2] = std::numeric_limits<float>::quiet_NaN();
		hot[3] = std::numeric_limits<float>::infinity();
		hot[4] = 1.5f;
		run(ceiling, hot);
		QVERIFY(ceiling.takeBurstNotice());
		for (const float sample : hot)
		{
			QVERIFY(std::isfinite(sample));
			QVERIFY(std::fabs(sample) <= kCeiling + 1e-5f);
		}
		QCOMPARE(hot[2], 0.f);
		QCOMPARE(hot[3], 0.f);

		HearingGuard burst;
		std::vector<float> quiet(kFrames * kChannels, 0.01f);
		run(burst, quiet);
		for (const float sample : quiet) { QCOMPARE(sample, 0.01f); }

		std::vector<float> fullScale(kFrames * kChannels, 1.f);
		run(burst, fullScale);
		QVERIFY(burst.takeBurstNotice());
		for (const float sample : fullScale)
		{
			QVERIFY(std::fabs(sample) <= kCeiling + 1e-5f);
		}
		for (int index = (kFrames - 8) * kChannels; index < kFrames * kChannels; ++index)
		{
			QCOMPARE(fullScale[index], 0.f);
		}

		std::vector<float> duringMute(kFrames * kChannels, 1.f);
		run(burst, duringMute);
		for (const float sample : duringMute) { QCOMPARE(sample, 0.f); }

		HearingGuard music;
		std::vector<float> tone(kFrames * kChannels);
		for (int index = 0; index < kFrames * kChannels; ++index)
		{
			tone[index] = (index % 2 == 0) ? 0.7f : -0.7f;
		}
		for (int pass = 0; pass < 4; ++pass)
		{
			auto played = tone;
			run(music, played);
			for (int index = 0; index < kFrames * kChannels; ++index) { QCOMPARE(played[index], tone[index]); }
			QVERIFY(!music.takeBurstNotice());
		}

		HearingGuard open;
		open.setEnabled(false);
		std::vector<float> clipped(kFrames * kChannels, 0.7f);
		clipped[0] = 2.f;
		clipped[1] = -3.f;
		clipped[2] = std::numeric_limits<float>::quiet_NaN();
		run(open, clipped);
		QCOMPARE(clipped[0], 1.f);
		QCOMPARE(clipped[1], -1.f);
		QCOMPARE(clipped[2], 0.f);
		QCOMPARE(clipped[3], 0.7f);
		QVERIFY(!open.takeBurstNotice());
	}
};

QTEST_GUILESS_MAIN(HearingGuardTest)
#include "HearingGuardTest.moc"
