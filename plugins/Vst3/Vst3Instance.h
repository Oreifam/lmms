/*
 * Vst3Instance.h - in-process VST3 loader used by the instrument and effect
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

#ifndef LMMS_VST3_INSTANCE_H
#define LMMS_VST3_INSTANCE_H

#include <memory>
#include <vector>

#include <QByteArray>
#include <QString>
#include <QStringList>

#include "LmmsTypes.h"

namespace lmms
{

class SampleFrame;

//! One loaded VST3 audio module. Loading and processing failures are reported, not thrown out.
class Vst3Instance
{
public:
	struct MidiNote
	{
		bool on = false;
		int channel = 0;
		int pitch = 0;
		float velocity = 0.f;
		int offset = 0;
	};

	struct Parameter
	{
		unsigned int id = 0;
		QString title;
		double normalized = 0.0;
	};

	Vst3Instance();
	~Vst3Instance();

	Vst3Instance(const Vst3Instance&) = delete;
	Vst3Instance& operator=(const Vst3Instance&) = delete;

	//! Standard VST3 folders for this platform, plus the vst3/extradir config value.
	static auto defaultSearchPaths() -> QStringList;

	//! .vst3 bundles and single-file plugins under @a root. Bundle binaries nested inside a bundle are skipped.
	static auto scanDirectory(const QString& root) -> QStringList;

	auto load(const QString& path, int sampleRate, int maxBlock, QString* error) -> bool;
	void unload();

	auto isLoaded() const -> bool;
	auto path() const -> QString;
	auto classUid() const -> QString;
	auto name() const -> QString;
	auto error() const -> QString;
	auto latencySamples() const -> int;

	auto parameters() const -> std::vector<Parameter>;
	void setNormalized(unsigned int id, double normalized);

	//! Queues a note for the next process() call. Safe against a concurrent load.
	void addNote(const MidiNote& note);

	//! Writes plugin output into @a buf. On failure the buffer is cleared and the call returns false.
	auto process(SampleFrame* buf, f_cnt_t frames) -> bool;

	auto saveState() const -> QByteArray;
	auto restoreState(const QByteArray& state) -> bool;

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};

} // namespace lmms

#endif
