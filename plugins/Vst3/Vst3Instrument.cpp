/*
 * Vst3Instrument.cpp - VST3 instrument
 *
 * Copyright (c) 2026 LMMS developers
 *
 * This file is part of LMMS - https://lmms.io
 */

#include "Vst3Instrument.h"

#include <QDomElement>
#include <QVBoxLayout>

#include "AudioEngine.h"
#include "Engine.h"
#include "InstrumentPlayHandle.h"
#include "InstrumentTrack.h"
#include "MidiEvent.h"
#include "Vst3Panel.h"
#include "embed.h"
#include "plugin_export.h"

namespace lmms
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT vst3instrument_plugin_descriptor = {
	LMMS_STRINGIFY(PLUGIN_NAME),
	"VST3 Instrument",
	QT_TRANSLATE_NOOP("PluginBrowser", "Host a VST3 instrument in-process"),
	"LMMS developers",
	0x0100,
	Plugin::Type::Instrument,
	new PixmapLoader("lmms-plugin-logo"),
	nullptr,
	nullptr,
};

}

Vst3Instrument::Vst3Instrument(InstrumentTrack* track)
	: Instrument(track, &vst3instrument_plugin_descriptor, nullptr, Flag::IsSingleStreamed | Flag::IsMidiBased)
{
	Engine::audioEngine()->addPlayHandle(new InstrumentPlayHandle(this, track));
}

void Vst3Instrument::play(SampleFrame* workingBuffer)
{
	const auto frames = Engine::audioEngine()->framesPerPeriod();
	if (!m_instance.isLoaded()) { return; }
	m_instance.process(workingBuffer, frames);
}

void Vst3Instrument::saveSettings(QDomDocument& doc, QDomElement& element)
{
	element.setAttribute(QStringLiteral("path"), m_instance.path());
	element.setAttribute(QStringLiteral("uid"), m_instance.classUid());
	auto state = doc.createElement(QStringLiteral("state"));
	state.setAttribute(QStringLiteral("data"), QString::fromLatin1(m_instance.saveState().toBase64()));
	element.appendChild(state);
}

void Vst3Instrument::loadSettings(const QDomElement& element)
{
	const auto path = element.attribute(QStringLiteral("path"));
	if (path.isEmpty()) { return; }
	const auto rate = Engine::audioEngine()->outputSampleRate();
	if (!m_instance.loadFromProject(path, element.attribute(QStringLiteral("uid")), static_cast<int>(rate), MAXIMUM_BUFFER_SIZE))
	{
		return;
	}
	const auto state = element.firstChildElement(QStringLiteral("state")).attribute(QStringLiteral("data"));
	if (!state.isEmpty())
	{
		m_instance.restoreState(QByteArray::fromBase64(state.toLatin1()));
	}
}

gui::PluginView* Vst3Instrument::instantiateView(QWidget* parent)
{
	return new gui::Vst3InstrumentView(this, parent);
}

bool Vst3Instrument::handleMidiEvent(const MidiEvent& event, const TimePos&, f_cnt_t offset)
{
	const auto route = Vst3Instance::routeMidi(event.type(), event.param(0), event.param(1));
	if (route.kind == Vst3Instance::MidiRoute::Kind::None) { return true; }
	Vst3Instance::MidiMessage message;
	message.channel = event.channel();
	message.offset = offset;
	message.value = static_cast<float>(route.normalized);
	switch (route.kind)
	{
	case Vst3Instance::MidiRoute::Kind::NoteOn:
		message.kind = Vst3Instance::MidiKind::NoteOn;
		message.data = route.key;
		break;
	case Vst3Instance::MidiRoute::Kind::NoteOff:
		message.kind = Vst3Instance::MidiKind::NoteOff;
		message.data = route.key;
		break;
	case Vst3Instance::MidiRoute::Kind::PitchBend:
		message.kind = Vst3Instance::MidiKind::PitchBend;
		break;
	case Vst3Instance::MidiRoute::Kind::ControlChange:
		message.kind = Vst3Instance::MidiKind::ControlChange;
		message.data = route.controller;
		break;
	case Vst3Instance::MidiRoute::Kind::ChannelPressure:
		message.kind = Vst3Instance::MidiKind::ChannelPressure;
		break;
	case Vst3Instance::MidiRoute::Kind::KeyPressure:
		message.kind = Vst3Instance::MidiKind::KeyPressure;
		message.data = route.key;
		break;
	case Vst3Instance::MidiRoute::Kind::None:
		return true;
	}
	m_instance.addMidi(message);
	return true;
}

namespace gui
{

Vst3InstrumentView::Vst3InstrumentView(Instrument* instrument, QWidget* parent)
	: InstrumentView(instrument, parent)
{
	auto* trackInstrument = castModel<Vst3Instrument>();
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(4, 4, 4, 4);
	layout->addWidget(new Vst3Panel(&trackInstrument->instance(), trackInstrument, this));
}

} // namespace gui
} // namespace lmms
