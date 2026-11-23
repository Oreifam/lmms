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
#include "GuiApplication.h"
#include "MainWindow.h"
#include "SubWindow.h"
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
	for (const auto& parameter : m_effect->m_instance.parameters())
	{
		element.setAttribute(QStringLiteral("vst3p%1").arg(parameter.id), QString::number(parameter.normalized, 'g', 12));
	}
}

void Vst3EffectControls::loadSettings(const QDomElement& element)
{
	const auto path = element.attribute(QStringLiteral("path"));
	if (path.isEmpty()) { return; }
	const auto rate = Engine::audioEngine()->outputSampleRate();
	if (!m_effect->m_instance.loadFromProject(path, element.attribute(QStringLiteral("uid")), static_cast<int>(rate), MAXIMUM_BUFFER_SIZE))
	{
		return;
	}
	const auto state = element.firstChildElement(QStringLiteral("state")).attribute(QStringLiteral("data"));
	if (!state.isEmpty())
	{
		m_effect->m_instance.restoreState(QByteArray::fromBase64(state.toLatin1()));
	}
	for (const auto& parameter : m_effect->m_instance.parameters())
	{
		const auto key = QStringLiteral("vst3p%1").arg(parameter.id);
		double value = 0.0;
		if (element.hasAttribute(key) && Vst3Instance::readStoredNormalized(element.attribute(key), &value))
		{
			m_effect->m_instance.setNormalized(parameter.id, value);
		}
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

Vst3Effect::~Vst3Effect()
{
	closeNativeEditor();
}

void Vst3Effect::openNativeEditor()
{
	if (!m_instance.editorSupported()) { return; }
	if (m_editorWindow != nullptr)
	{
		m_editorWindow->show();
		m_editorWindow->raise();
		return;
	}
	if (gui::getGUI() == nullptr || gui::getGUI()->mainWindow() == nullptr) { return; }
	auto* editor = gui::createVst3NativeEditor(&m_instance);
	m_editorWindow = gui::getGUI()->mainWindow()->addWindowedWidget(editor);
	if (m_editorWindow == nullptr) { return; }
	m_editorWindow->setWindowTitle(m_instance.name());
	m_editorWindow->show();
}

void Vst3Effect::closeNativeEditor()
{
	auto* window = m_editorWindow.data();
	m_editorWindow = nullptr;
	if (window == nullptr) { return; }
	window->hide();
	delete window;
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
		auto* panel = new Vst3Panel(&effect->instance(), effect, this);
		panel->setEditorHooks([effect] { effect->openNativeEditor(); }, [effect] { effect->closeNativeEditor(); });
		layout->addWidget(panel);
	}
}

} // namespace gui
} // namespace lmms
