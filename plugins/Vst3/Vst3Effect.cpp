/*
 * Vst3Effect.cpp - VST3 effect
 *
 * Copyright (c) 2026 LMMS developers
 *
 * This file is part of LMMS - https://lmms.io
 */

#include "Vst3Effect.h"

#include <algorithm>

#include <QDomElement>
#include <QVBoxLayout>

#include "AudioEngine.h"
#include "Engine.h"
#include "Vst3Panel.h"
#include "embed.h"
#include "plugin_export.h"

namespace lmms
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT vst3effect_plugin_descriptor = {
	LMMS_STRINGIFY(PLUGIN_NAME),
	"VST3 Effect",
	QT_TRANSLATE_NOOP("PluginBrowser", "Host a VST3 effect in-process"),
	"LMMS developers",
	0x0100,
	Plugin::Type::Effect,
	new PixmapLoader("lmms-plugin-logo"),
	nullptr,
	nullptr,
};

}

Vst3EffectControls::Vst3EffectControls(Vst3Effect* effect)
	: EffectControls(effect)
	, m_effect(effect)
{
}

void Vst3EffectControls::saveSettings(QDomDocument& doc, QDomElement& element)
{
	element.setAttribute(QStringLiteral("path"), m_effect->m_instance.path());
	element.setAttribute(QStringLiteral("uid"), m_effect->m_instance.classUid());
	auto state = doc.createElement(QStringLiteral("state"));
	state.setAttribute(QStringLiteral("data"), QString::fromLatin1(m_effect->m_instance.saveState().toBase64()));
	element.appendChild(state);
}

void Vst3EffectControls::loadSettings(const QDomElement& element)
{
	const auto path = element.attribute(QStringLiteral("path"));
	if (path.isEmpty()) { return; }
	QString error;
	const auto rate = Engine::audioEngine()->outputSampleRate();
	m_effect->m_instance.load(path, static_cast<int>(rate), MAXIMUM_BUFFER_SIZE, &error);
	const auto state = element.firstChildElement(QStringLiteral("state")).attribute(QStringLiteral("data"));
	if (!state.isEmpty())
	{
		m_effect->m_instance.restoreState(QByteArray::fromBase64(state.toLatin1()));
	}
}

gui::EffectControlDialog* Vst3EffectControls::createView()
{
	return new gui::Vst3EffectDialog(this);
}

Vst3Effect::Vst3Effect(Model* parent, const Descriptor::SubPluginFeatures::Key* key)
	: Effect(&vst3effect_plugin_descriptor, parent, key)
	, m_controls(this)
{
}

Effect::ProcessStatus Vst3Effect::processImpl(SampleFrame* buf, const f_cnt_t frames)
{
	if (!m_instance.isLoaded()) { return ProcessStatus::Continue; }
	if (m_dry.size() < static_cast<size_t>(frames)) { m_dry.resize(static_cast<size_t>(frames)); }
	std::copy_n(buf, frames, m_dry.data());
	if (!m_instance.process(buf, frames)) { return ProcessStatus::Continue; }
	const auto wet = wetLevel();
	const auto dry = dryLevel();
	for (f_cnt_t frame = 0; frame < frames; ++frame)
	{
		buf[frame][0] = m_dry[static_cast<size_t>(frame)][0] * dry + buf[frame][0] * wet;
		buf[frame][1] = m_dry[static_cast<size_t>(frame)][1] * dry + buf[frame][1] * wet;
	}
	return ProcessStatus::Continue;
}

namespace gui
{

Vst3EffectDialog::Vst3EffectDialog(Vst3EffectControls* controls)
	: EffectControlDialog(controls)
{
	auto* effect = dynamic_cast<Vst3Effect*>(controls->effect());
	auto* layout = new QVBoxLayout(this);
	if (effect != nullptr)
	{
		layout->addWidget(new Vst3Panel(&effect->instance(), this));
	}
}

} // namespace gui
} // namespace lmms
