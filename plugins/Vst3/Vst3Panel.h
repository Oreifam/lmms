/*
 * Vst3Panel.h - shared browser and parameter list for VST3 plugins
 *
 * Copyright (c) 2026 LMMS developers
 *
 * This file is part of LMMS - https://lmms.io
 */

#ifndef LMMS_VST3_PANEL_H
#define LMMS_VST3_PANEL_H

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QWidget>

namespace lmms
{

class Vst3Instance;

namespace gui
{

class Vst3Panel : public QWidget
{
public:
	explicit Vst3Panel(Vst3Instance* instance, QWidget* parent = nullptr);

	void refresh();

private:
	Vst3Instance* m_instance;
	QComboBox* m_plugins = nullptr;
	QLineEdit* m_extra = nullptr;
	QLabel* m_status = nullptr;
	QVBoxLayout* m_parameters = nullptr;
	bool m_updating = false;
};

} // namespace gui
} // namespace lmms

#endif
