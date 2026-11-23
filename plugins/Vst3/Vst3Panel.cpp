/*
 * Vst3Panel.cpp - shared browser, parameter list, and native editor for VST3 plugins
 *
 * Copyright (c) 2026 LMMS developers
 *
 * This file is part of LMMS - https://lmms.io
 */

#include "Vst3Panel.h"

#include <algorithm>
#include <cstdint>

#include <QComboBox>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QSlider>
#include <QVBoxLayout>

#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "SubWindow.h"
#include "Vst3Instance.h"
#include "lmmsconfig.h"

namespace lmms::gui
{
namespace
{

class Vst3NativeEditor : public QWidget
{
public:
	Vst3NativeEditor(Vst3Instance* instance, QWidget* parent)
		: QWidget(parent)
		, m_instance(instance)
	{
		setAttribute(Qt::WA_NativeWindow);
		if (m_instance == nullptr) { return; }
		m_instance->setEditorResizeHandler([this](int width, int height) {
			setFixedSize(width, height);
			if (parentWidget() != nullptr) { parentWidget()->resize(width, height); }
		});
	}

	~Vst3NativeEditor() override
	{
		if (m_instance != nullptr) { m_instance->detachEditor(); }
	}

protected:
	void showEvent(QShowEvent* event) override
	{
		QWidget::showEvent(event);
		if (m_attached || m_instance == nullptr) { return; }
#ifdef LMMS_BUILD_WIN32
		m_attached = m_instance->attachEditor(reinterpret_cast<void*>(static_cast<uintptr_t>(winId())));
#else
		(void)event;
#endif
	}

private:
	Vst3Instance* m_instance;
	bool m_attached = false;
};

} // namespace

Vst3Panel::Vst3Panel(Vst3Instance* instance, Model* automationParent, QWidget* parent)
	: QWidget(parent)
	, m_instance(instance)
	, m_automationParent(automationParent)
{
	auto* layout = new QVBoxLayout(this);
	m_plugins = new QComboBox(this);
	m_extra = new QLineEdit(this);
	m_extra->setPlaceholderText(tr("Extra VST3 folder"));
	m_extra->setText(ConfigManager::inst()->value(QStringLiteral("vst3"), QStringLiteral("extradir")));
	m_status = new QLabel(this);
	m_status->setWordWrap(true);
	m_editorButton = new QPushButton(tr("Open editor"), this);
	layout->addWidget(m_plugins);
	layout->addWidget(m_extra);
	layout->addWidget(m_status);
	layout->addWidget(m_editorButton);

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
		closeEditor();
		QString error;
		const auto rate = Engine::audioEngine()->outputSampleRate();
		m_instance->load(path, static_cast<int>(rate), MAXIMUM_BUFFER_SIZE, &error);
		refresh();
	});
	connect(m_extra, &QLineEdit::editingFinished, this, [this] {
		ConfigManager::inst()->setValue(QStringLiteral("vst3"), QStringLiteral("extradir"), m_extra->text());
		refresh();
	});
	connect(m_editorButton, &QPushButton::clicked, this, [this] { openEditor(); });
	if (m_instance != nullptr)
	{
		m_instance->setParameterEditedHandler([this](unsigned int id, double normalized) {
			m_updating = true;
			for (const auto& row : m_rows)
			{
				if (row.id != id) { continue; }
				if (row.model != nullptr) { row.model->setValue(normalized); }
				if (row.slider != nullptr) { row.slider->setValue(static_cast<int>(normalized * 1000.0)); }
			}
			m_updating = false;
		});
	}
	refresh();
}

void Vst3Panel::openEditor()
{
	if (m_instance == nullptr || !m_instance->editorSupported()) { return; }
	if (m_editorWindow != nullptr)
	{
		m_editorWindow->show();
		m_editorWindow->raise();
		return;
	}
	if (getGUI() == nullptr || getGUI()->mainWindow() == nullptr) { return; }
	auto* editor = new Vst3NativeEditor(m_instance, nullptr);
	m_editorWindow = getGUI()->mainWindow()->addWindowedWidget(editor);
	if (m_editorWindow == nullptr) { return; }
	m_editorWindow->setWindowTitle(m_instance->name());
	m_editorWindow->show();
}

void Vst3Panel::closeEditor()
{
	if (m_instance != nullptr) { m_instance->detachEditor(); }
	if (m_editorWindow != nullptr) { m_editorWindow->close(); }
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
	m_editorButton->setEnabled(m_instance->editorSupported());

	for (const auto& row : m_rows)
	{
		delete row.model;
	}
	m_rows.clear();
	while (auto* item = m_parameters->takeAt(0))
	{
		delete item->widget();
		delete item;
	}
	const auto parameters = m_instance->parameters();
	for (const auto& parameter : parameters)
	{
		auto* rowWidget = new QWidget(this);
		auto* rowLayout = new QVBoxLayout(rowWidget);
		rowLayout->setContentsMargins(0, 0, 0, 0);
		auto* label = new QLabel(parameter.title, rowWidget);
		auto* slider = new QSlider(Qt::Horizontal, rowWidget);
		slider->setRange(0, 1000);
		slider->setValue(static_cast<int>(parameter.normalized * 1000.0));
		Row row;
		row.id = parameter.id;
		row.slider = slider;
		if (m_automationParent != nullptr)
		{
			row.model = new FloatModel(parameter.normalized, 0.0, 1.0, 0.001, m_automationParent, parameter.title);
			connect(row.model, &FloatModel::dataChanged, this, [this, id = parameter.id, model = row.model] {
				if (m_updating || m_instance == nullptr) { return; }
				m_instance->setNormalized(id, model->value());
			});
		}
		connect(slider, &QSlider::valueChanged, this, [this, id = parameter.id, model = row.model](int value) {
			if (m_updating) { return; }
			const auto normalized = value / 1000.0;
			if (model != nullptr) { model->setValue(normalized); }
			else if (m_instance != nullptr) { m_instance->setNormalized(id, normalized); }
		});
		rowLayout->addWidget(label);
		rowLayout->addWidget(slider);
		m_parameters->addWidget(rowWidget);
		m_rows.push_back(row);
	}
	m_parameters->addStretch();
	m_updating = false;
}

} // namespace lmms::gui
