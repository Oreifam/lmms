/*
 * Vst3Panel.cpp - shared browser and parameter list for VST3 plugins
 *
 * Copyright (c) 2026 LMMS developers
 *
 * This file is part of LMMS - https://lmms.io
 */

#include "Vst3Panel.h"

#include <algorithm>

#include <QComboBox>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>

#include "AudioEngine.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "Vst3Instance.h"

namespace lmms::gui
{

Vst3Panel::Vst3Panel(Vst3Instance* instance, QWidget* parent)
	: QWidget(parent)
	, m_instance(instance)
{
	auto* layout = new QVBoxLayout(this);
	m_plugins = new QComboBox(this);
	m_extra = new QLineEdit(this);
	m_extra->setPlaceholderText(tr("Extra VST3 folder"));
	m_extra->setText(ConfigManager::inst()->value(QStringLiteral("vst3"), QStringLiteral("extradir")));
	m_status = new QLabel(this);
	m_status->setWordWrap(true);
	layout->addWidget(m_plugins);
	layout->addWidget(m_extra);
	layout->addWidget(m_status);

	auto* scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	auto* parameterHost = new QWidget(scroll);
	m_parameters = new QVBoxLayout(parameterHost);
	scroll->setWidget(parameterHost);
	layout->addWidget(scroll, 1);

	connect(m_plugins, qOverload<int>(&QComboBox::activated), this, [this](int index) {
		if (m_updating || index < 0) { return; }
		const auto path = m_plugins->itemData(index).toString();
		if (path.isEmpty() || m_instance == nullptr) { return; }
		QString error;
		const auto rate = Engine::audioEngine()->outputSampleRate();
		m_instance->load(path, static_cast<int>(rate), MAXIMUM_BUFFER_SIZE, &error);
		refresh();
	});
	connect(m_extra, &QLineEdit::editingFinished, this, [this] {
		ConfigManager::inst()->setValue(QStringLiteral("vst3"), QStringLiteral("extradir"), m_extra->text());
		refresh();
	});
	refresh();
}

void Vst3Panel::refresh()
{
	if (m_instance == nullptr) { return; }
	m_updating = true;
	const auto selected = m_instance->path();
	m_plugins->clear();
	m_plugins->addItem(tr("Select a VST3 plugin"), QString());
	auto paths = Vst3Instance::defaultSearchPaths();
	QStringList plugins;
	for (const auto& root : paths) { plugins << Vst3Instance::scanDirectory(root); }
	plugins.removeDuplicates();
	plugins.sort();
	auto chosen = 0;
	for (const auto& path : plugins)
	{
		m_plugins->addItem(QFileInfo(path).completeBaseName(), path);
		if (path == selected) { chosen = m_plugins->count() - 1; }
	}
	if (!selected.isEmpty() && chosen == 0)
	{
		m_plugins->addItem(QFileInfo(selected).completeBaseName(), selected);
		chosen = m_plugins->count() - 1;
	}
	m_plugins->setCurrentIndex(chosen);

	if (!m_instance->error().isEmpty())
	{
		m_status->setText(m_instance->error());
	}
	else if (m_instance->isLoaded())
	{
		m_status->setText(tr("%1 — latency %2 samples (not compensated)")
			.arg(m_instance->name())
			.arg(m_instance->latencySamples()));
	}
	else
	{
		m_status->setText(tr("Scans the system VST3 folder and the extra folder above."));
	}

	while (auto* item = m_parameters->takeAt(0))
	{
		delete item->widget();
		delete item;
	}
	const auto parameters = m_instance->parameters();
	const auto shown = std::min<int>(static_cast<int>(parameters.size()), 48);
	for (int index = 0; index < shown; ++index)
	{
		const auto& parameter = parameters[static_cast<size_t>(index)];
		auto* row = new QWidget(this);
		auto* rowLayout = new QVBoxLayout(row);
		rowLayout->setContentsMargins(0, 0, 0, 0);
		auto* label = new QLabel(parameter.title, row);
		auto* slider = new QSlider(Qt::Horizontal, row);
		slider->setRange(0, 1000);
		slider->setValue(static_cast<int>(parameter.normalized * 1000.0));
		const auto id = parameter.id;
		connect(slider, &QSlider::valueChanged, this, [this, id](int value) {
			if (m_updating || m_instance == nullptr) { return; }
			m_instance->setNormalized(id, value / 1000.0);
		});
		rowLayout->addWidget(label);
		rowLayout->addWidget(slider);
		m_parameters->addWidget(row);
	}
	if (static_cast<int>(parameters.size()) > shown)
	{
		m_parameters->addWidget(new QLabel(tr("Showing 48 of %1 parameters.").arg(parameters.size()), this));
	}
	m_parameters->addStretch();
	m_updating = false;
}

} // namespace lmms::gui
