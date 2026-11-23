/*
 * Vst3Panel.h - shared browser and parameter list for VST3 plugins
 *
 * Copyright (c) 2026 LMMS developers
 *
 * This file is part of LMMS - https://lmms.io
 */

#ifndef LMMS_VST3_PANEL_H
#define LMMS_VST3_PANEL_H

#include <atomic>
#include <functional>
#include <memory>
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

QWidget* createVst3NativeEditor(Vst3Instance* instance);

class Vst3Panel : public QWidget
{
public:
	Vst3Panel(Vst3Instance* instance, Model* automationParent, QWidget* parent = nullptr);
	~Vst3Panel() override;

	void refresh();
	void setEditorHooks(std::function<void()> open, std::function<void()> close);

private:
	void openEditor();
	void closeEditor();

	struct Row
	{
		unsigned int id = 0;
		QPointer<FloatModel> model;
		QSlider* slider = nullptr;
	};

	Vst3Instance* m_instance;
	std::weak_ptr<std::atomic<bool>> m_life;
	std::shared_ptr<std::atomic<bool>> m_panelAlive = std::make_shared<std::atomic<bool>>(true);
	Model* m_automationParent = nullptr;
	std::function<void()> m_openEditor;
	std::function<void()> m_closeEditor;
	QComboBox* m_plugins = nullptr;
	QLineEdit* m_extra = nullptr;
	QLabel* m_status = nullptr;
	QPushButton* m_editorButton = nullptr;
	QVBoxLayout* m_parameters = nullptr;
	std::vector<Row> m_rows;
	bool m_updating = false;
};

} // namespace gui
} // namespace lmms

#endif
