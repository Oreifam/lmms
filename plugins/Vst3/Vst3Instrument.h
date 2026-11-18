/*
 * Vst3Instrument.h - VST3 instrument
 *
 * Copyright (c) 2026 LMMS developers
 *
 * This file is part of LMMS - https://lmms.io
 */

#ifndef LMMS_VST3_INSTRUMENT_H
#define LMMS_VST3_INSTRUMENT_H

#include "Instrument.h"
#include "InstrumentView.h"
#include "Vst3Instance.h"

namespace lmms
{

class Vst3Instrument : public Instrument
{
	Q_OBJECT
public:
	explicit Vst3Instrument(InstrumentTrack* track);
	~Vst3Instrument() override = default;

	void play(SampleFrame* workingBuffer) override;
	void saveSettings(QDomDocument& doc, QDomElement& element) override;
	void loadSettings(const QDomElement& element) override;
	QString nodeName() const override { return QStringLiteral("vst3instrument"); }
	gui::PluginView* instantiateView(QWidget* parent) override;
	bool handleMidiEvent(const MidiEvent& event, const TimePos& time, f_cnt_t offset) override;

	auto instance() -> Vst3Instance& { return m_instance; }

private:
	Vst3Instance m_instance;
};

namespace gui
{

class Vst3InstrumentView : public InstrumentView
{
public:
	Vst3InstrumentView(Instrument* instrument, QWidget* parent);
};

} // namespace gui
} // namespace lmms

#endif
