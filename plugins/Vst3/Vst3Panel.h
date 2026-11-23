/*
 * Vst3Panel.h - shared browser and parameter list for VST3 plugins
 *
 * Copyright (c) 2026 LMMS developers
 *
 * This file is part of LMMS - https://lmms.io
 */

#ifndef LMMS_VST3_PANEL_H
#define LMMS_VST3_PANEL_H

#include <vector>

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QVBoxLayout>
#include <QWidget>

class QPushButton;
class QSlider;

namespace lmms
{

class FloatModel;
class Model;
class Vst3Instance;

namespace gui
{

class Vst3Panel : public QWidget
{
public:
	Vst3Panel(Vst3Instance* instance, Model* automationParent, QWidget* parent = nullptr);

	void refresh();

private:
	void openEditor();
	void closeEditor();

	struct Row
	{
		unsigned int id = 0;
		FloatModel* model = nullptr;
		QSlider* slider = nullptr;
	};

	Vst3Instance* m_instance;
	Model* m_automationParent = nullptr;
	QComboBox* m_plugins = nullptr;
	QLineEdit* m_extra = nullptr;
	QLabel* m_status = nullptr;
	QPushButton* m_editorButton = nullptr;
	QVBoxLayout* m_parameters = nullptr;
	QPointer<QWidget> m_editorWindow;
	std::vector<Row> m_rows;
	bool m_updating = false;
};

} // namespace gui
} // namespace lmms

#endif
