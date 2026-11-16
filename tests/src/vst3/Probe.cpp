/*
 * Probe.cpp - tiny in-process VST3 used by Vst3HostTest
 *
 * Copyright (c) 2026 LMMS developers
 *
 * This file is part of LMMS - https://lmms.io
 */

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"

namespace
{

constexpr Steinberg::Vst::ParamID kLevel = 1;

class ProbeProcessor : public Steinberg::Vst::SingleComponentEffect
{
public:
	static Steinberg::FUnknown* createInstance(void*)
	{
		return static_cast<Steinberg::Vst::IAudioProcessor*>(new ProbeProcessor());
	}

	Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE
	{
		const auto result = SingleComponentEffect::initialize(context);
		if (result != Steinberg::kResultOk) { return result; }
		addAudioInput(STR16("Input"), Steinberg::Vst::SpeakerArr::kStereo);
		addAudioOutput(STR16("Output"), Steinberg::Vst::SpeakerArr::kStereo);
		addEventInput(STR16("Event Input"), 16);
		parameters.addParameter(STR16("Level"), STR16(""), 0, 1.0, Steinberg::Vst::ParameterInfo::kCanAutomate, kLevel);
		return Steinberg::kResultOk;
	}

	Steinberg::uint32 PLUGIN_API getLatencySamples() SMTG_OVERRIDE { return 17; }

	Steinberg::tresult PLUGIN_API setProcessing(Steinberg::TBool) SMTG_OVERRIDE { return Steinberg::kResultOk; }

	Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* stream) SMTG_OVERRIDE
	{
		const auto level = static_cast<float>(getParamNormalized(kLevel));
		Steinberg::int32 written = 0;
		return stream->write(const_cast<float*>(&level), static_cast<Steinberg::int32>(sizeof(level)), &written);
	}

	Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* stream) SMTG_OVERRIDE
	{
		float level = 1.f;
		Steinberg::int32 read = 0;
		if (stream->read(&level, static_cast<Steinberg::int32>(sizeof(level)), &read) != Steinberg::kResultOk) { return Steinberg::kResultFalse; }
		setParamNormalized(kLevel, level);
		return Steinberg::kResultOk;
	}

	Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE
	{
		if (data.inputEvents != nullptr)
		{
			for (int index = 0; index < data.inputEvents->getEventCount(); ++index)
			{
				Steinberg::Vst::Event event{};
				if (data.inputEvents->getEvent(index, event) != Steinberg::kResultOk) { continue; }
				if (event.type == Steinberg::Vst::Event::kNoteOnEvent && event.noteOn.velocity > 0.f) { m_note = true; }
				if (event.type == Steinberg::Vst::Event::kNoteOffEvent) { m_note = false; }
			}
		}
		if (data.inputParameterChanges != nullptr)
		{
			for (int index = 0; index < data.inputParameterChanges->getParameterCount(); ++index)
			{
				auto* queue = data.inputParameterChanges->getParameterData(index);
				if (queue == nullptr || queue->getParameterId() != kLevel || queue->getPointCount() < 1) { continue; }
				Steinberg::int32 offset = 0;
				Steinberg::Vst::ParamValue value = 1.0;
				queue->getPoint(queue->getPointCount() - 1, offset, value);
				setParamNormalized(kLevel, value);
			}
		}
		if (data.numOutputs < 1 || data.outputs[0].channelBuffers32 == nullptr) { return Steinberg::kResultOk; }
		const auto level = static_cast<float>(getParamNormalized(kLevel));
		const auto tone = m_note ? 0.2f * level : 0.f;
		for (int channel = 0; channel < data.outputs[0].numChannels; ++channel)
		{
			auto* output = data.outputs[0].channelBuffers32[channel];
			const float* input = nullptr;
			if (data.numInputs > 0 && data.inputs[0].channelBuffers32 != nullptr && channel < data.inputs[0].numChannels)
			{
				input = data.inputs[0].channelBuffers32[channel];
			}
			for (int frame = 0; frame < data.numSamples; ++frame)
			{
				const auto dry = input != nullptr ? input[frame] : 0.f;
				output[frame] = dry * level + tone;
			}
		}
		return Steinberg::kResultOk;
	}

private:
	bool m_note = false;
};

bool InitModule() { return true; }
bool DeinitModule() { return true; }

} // namespace

BEGIN_FACTORY_DEF("LMMS", "https://lmms.io", "mailto:dev@lmms.io")
DEF_CLASS2(INLINE_UID(0xA1B2C3D4, 0xE5F60718, 0x192A3B4C, 0x5D6E7F80),
	Steinberg::PClassInfo::kManyInstances,
	kVstAudioEffectClass,
	"Lmms VST3 Probe",
	Steinberg::Vst::kDistributable,
	"Instrument|Synth",
	"1.0.0",
	kVstVersionString,
	ProbeProcessor::createInstance)
END_FACTORY
