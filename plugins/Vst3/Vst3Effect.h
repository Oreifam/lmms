/*
 * Vst3Effect.h - VST3 effect
 *
 * Copyright (c) 2026 LMMS developers
 *
 * This file is part of LMMS - https://lmms.io
 */

#ifndef LMMS_VST3_EFFECT_H
#define LMMS_VST3_EFFECT_H

#include <vector>

#include "Effect.h"
#include "EffectControlDialog.h"
#include "EffectControls.h"
#include "Vst3Instance.h"

namespace lmms
{

class Vst3Effect;

class Vst3EffectControls : public EffectControls
{
	Q_OBJECT
public:
	explicit Vst3EffectControls(Vst3Effect* effect);
	void saveSettings(QDomDocument& doc, QDomElement& element) override;
	void loadSettings(const QDomElement& element) override;
	QString nodeName() const override { return QStringLiteral("vst3effectcontrols"); }
	gui::EffectControlDialog* createView() override;
	int controlCount() override { return 1; }

private:
	Vst3Effect* m_effect;
};

class Vst3Effect : public Effect
{
	Q_OBJECT
public:
	Vst3Effect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);
	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override;
	EffectControls* controls() override { return &m_controls; }
	auto instance() -> Vst3Instance& { return m_instance; }

private:
	Vst3EffectControls m_controls;
	Vst3Instance m_instance;
	std::vector<SampleFrame> m_dry;

	friend class Vst3EffectControls;
};

namespace gui
{

class Vst3EffectDialog : public EffectControlDialog
{
public:
	explicit Vst3EffectDialog(Vst3EffectControls* controls);
};

} // namespace gui
} // namespace lmms

#endif
