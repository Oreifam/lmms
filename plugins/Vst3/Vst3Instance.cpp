/*
 * Vst3Instance.cpp - in-process VST3 loader
 *
 * Copyright (c) 2026 LMMS developers
 *
 * This file is part of LMMS - https://lmms.io
 */

#include "Vst3Instance.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <vector>

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

#include "ConfigManager.h"
#include "SampleFrame.h"
#include "lmmsconfig.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

namespace lmms
{
namespace
{

constexpr int kMaxNotes = 256;
constexpr int kMaxParametersQueued = 64;

auto hostApplication() -> Steinberg::Vst::HostApplication&
{
	// Process lifetime: plugins may call back into the context while they unload.
	static auto* application = new Steinberg::Vst::HostApplication();
	return *application;
}

void clearBuffer(SampleFrame* buf, f_cnt_t frames)
{
	if (buf == nullptr) { return; }
	for (f_cnt_t frame = 0; frame < frames; ++frame)
	{
		buf[frame][0] = 0.f;
		buf[frame][1] = 0.f;
	}
}

auto readChunk(const QByteArray& state, int& cursor) -> QByteArray
{
	if (cursor < 0 || cursor + 4 > state.size()) { return {}; }
	uint32_t size = 0;
	std::memcpy(&size, state.constData() + cursor, 4);
	cursor += 4;
	if (size > static_cast<uint32_t>(state.size() - cursor)) { return {}; }
	const auto chunk = state.mid(cursor, static_cast<int>(size));
	cursor += static_cast<int>(size);
	return chunk;
}

} // namespace

struct Vst3Instance::Impl
{
	std::mutex mutex;
	VST3::Hosting::Module::Ptr module;
	std::unique_ptr<Steinberg::Vst::PlugProvider> provider;
	Steinberg::IPtr<Steinberg::Vst::IComponent> component;
	Steinberg::IPtr<Steinberg::Vst::IEditController> controller;
	Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> processor;
	Steinberg::Vst::HostProcessData processData;
	Steinberg::Vst::EventList events{kMaxNotes};
	Steinberg::Vst::ParameterChanges parameterChanges{kMaxParametersQueued};
	std::vector<float> inputLeft;
	std::vector<float> inputRight;
	std::vector<float> outputLeft;
	std::vector<float> outputRight;
	std::vector<Parameter> parameters;
	std::vector<unsigned int> dirtyParameters;
	std::vector<MidiNote> notes;
	QString path;
	QString classUid;
	QString pluginName;
	QString error;
	int sampleRate = 48000;
	int maxBlock = 256;
	int latency = 0;
	int inputChannels = 0;
	int outputChannels = 0;
	bool loaded = false;
	bool processing = false;
	bool broken = false;
	bool announced = false;

	void reset()
	{
		if (processing && processor)
		{
			processor->setProcessing(false);
		}
		processing = false;
		if (component)
		{
			component->setActive(false);
		}
		processData.unprepare();
		processor.reset();
		controller.reset();
		component.reset();
		provider.reset();
		module.reset();
		parameters.clear();
		dirtyParameters.clear();
		notes.clear();
		loaded = false;
		broken = false;
		latency = 0;
		inputChannels = 0;
		outputChannels = 0;
	}
};

Vst3Instance::Vst3Instance() : m_impl(std::make_unique<Impl>()) {}

Vst3Instance::~Vst3Instance()
{
	unload();
}

auto Vst3Instance::defaultSearchPaths() -> QStringList
{
	QStringList paths;
#ifdef LMMS_BUILD_WIN32
	paths << QStringLiteral("C:/Program Files/Common Files/VST3");
	paths << QDir::homePath() + QStringLiteral("/AppData/Local/Programs/Common/VST3");
#else
	paths << QDir::homePath() + QStringLiteral("/.vst3");
	paths << QStringLiteral("/usr/lib/vst3");
	paths << QStringLiteral("/usr/lib64/vst3");
	paths << QStringLiteral("/usr/local/lib/vst3");
#endif
	const auto extra = ConfigManager::inst()->value(QStringLiteral("vst3"), QStringLiteral("extradir"));
	if (!extra.isEmpty()) { paths << extra; }
	return paths;
}

auto Vst3Instance::scanDirectory(const QString& root) -> QStringList
{
	QStringList found;
	if (root.isEmpty() || !QFileInfo(root).isDir()) { return found; }
	QDirIterator it(root, QDir::AllEntries | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
	while (it.hasNext())
	{
		it.next();
		if (!it.fileName().endsWith(QStringLiteral(".vst3"), Qt::CaseInsensitive)) { continue; }
		const QFileInfo info = it.fileInfo();
		if (info.isFile())
		{
			auto parent = info.dir();
			auto nested = false;
			while (parent.exists() && parent.absolutePath() != parent.rootPath())
			{
				if (parent.dirName().endsWith(QStringLiteral(".vst3"), Qt::CaseInsensitive))
				{
					nested = true;
					break;
				}
				if (!parent.cdUp()) { break; }
			}
			if (nested) { continue; }
		}
		found << info.absoluteFilePath();
	}
	found.removeDuplicates();
	found.sort();
	return found;
}

void Vst3Instance::unload()
{
	std::lock_guard<std::mutex> lock(m_impl->mutex);
	m_impl->reset();
	m_impl->path.clear();
	m_impl->classUid.clear();
	m_impl->pluginName.clear();
}

auto Vst3Instance::load(const QString& path, int sampleRate, int maxBlock, QString* error) -> bool
{
	std::lock_guard<std::mutex> lock(m_impl->mutex);
	m_impl->reset();
	m_impl->error.clear();
	m_impl->announced = false;
	auto fail = [&](const QString& message) {
		m_impl->error = message;
		m_impl->reset();
		if (error != nullptr) { *error = message; }
		qWarning("VST3: %s", qUtf8Printable(message));
		return false;
	};

	if (path.isEmpty() || !QFileInfo::exists(path))
	{
		return fail(QStringLiteral("VST3 plugin was not found: %1").arg(path));
	}

	try
	{
		std::string description;
		auto module = VST3::Hosting::Module::create(path.toStdString(), description);
		if (!module)
		{
			const auto why = description.empty() ? std::string("unknown error") : description;
			return fail(QStringLiteral("Could not load %1 (%2)").arg(path, QString::fromStdString(why)));
		}

		VST3::Hosting::ClassInfo chosen;
		auto foundClass = false;
		for (const auto& info : module->getFactory().classInfos())
		{
			if (info.category() == kVstAudioEffectClass)
			{
				chosen = info;
				foundClass = true;
				break;
			}
		}
		if (!foundClass)
		{
			return fail(QStringLiteral("%1 has no audio module class").arg(path));
		}

		Steinberg::Vst::PluginContextFactory::instance().setPluginContext(&hostApplication());
		auto provider = std::make_unique<Steinberg::Vst::PlugProvider>(module->getFactory(), chosen, true);
		if (!provider->initialize())
		{
			return fail(QStringLiteral("Failed to initialize %1").arg(QString::fromStdString(chosen.name())));
		}

		auto component = provider->getComponentPtr();
		auto controller = provider->getControllerPtr();
		auto processor = Steinberg::U::cast<Steinberg::Vst::IAudioProcessor>(component);
		if (!component || !processor)
		{
			return fail(QStringLiteral("%1 did not provide an audio processor").arg(QString::fromStdString(chosen.name())));
		}

		using namespace Steinberg;
		using namespace Steinberg::Vst;
		const auto rate = sampleRate > 0 ? sampleRate : 48000;
		const auto block = maxBlock > 0 ? maxBlock : 256;
		SpeakerArrangement stereo = SpeakerArr::kStereo;
		const auto inputs = component->getBusCount(kAudio, kInput);
		const auto outputs = component->getBusCount(kAudio, kOutput);
		if (outputs < 1)
		{
			return fail(QStringLiteral("%1 has no audio output").arg(QString::fromStdString(chosen.name())));
		}
		processor->setBusArrangements(inputs > 0 ? &stereo : nullptr, inputs > 0 ? 1 : 0, &stereo, 1);
		if (inputs > 0) { component->activateBus(kAudio, kInput, 0, true); }
		component->activateBus(kAudio, kOutput, 0, true);
		const auto eventInputs = component->getBusCount(kEvent, kInput);
		if (eventInputs > 0) { component->activateBus(kEvent, kInput, 0, true); }

		ProcessSetup setup{};
		setup.processMode = kRealtime;
		setup.symbolicSampleSize = kSample32;
		setup.maxSamplesPerBlock = block;
		setup.sampleRate = rate;
		if (processor->setupProcessing(setup) != kResultOk)
		{
			return fail(QStringLiteral("setupProcessing failed for %1").arg(QString::fromStdString(chosen.name())));
		}
		if (component->setActive(true) != kResultOk)
		{
			return fail(QStringLiteral("setActive failed for %1").arg(QString::fromStdString(chosen.name())));
		}

		m_impl->module = std::move(module);
		m_impl->provider = std::move(provider);
		m_impl->component = component;
		m_impl->controller = controller;
		m_impl->processor = processor;
		m_impl->path = path;
		m_impl->classUid = QString::fromStdString(chosen.ID().toString(false));
		m_impl->pluginName = QString::fromStdString(chosen.name());
		m_impl->sampleRate = rate;
		m_impl->maxBlock = block;
		m_impl->latency = static_cast<int>(processor->getLatencySamples());
		m_impl->inputChannels = inputs > 0 ? 2 : 0;
		m_impl->outputChannels = 2;
		m_impl->inputLeft.assign(static_cast<size_t>(block), 0.f);
		m_impl->inputRight.assign(static_cast<size_t>(block), 0.f);
		m_impl->outputLeft.assign(static_cast<size_t>(block), 0.f);
		m_impl->outputRight.assign(static_cast<size_t>(block), 0.f);
		m_impl->processData.prepare(*component, 0, kSample32);
		m_impl->loaded = true;

		if (controller)
		{
			const auto count = controller->getParameterCount();
			for (int32 parameterIndex = 0; parameterIndex < count; ++parameterIndex)
			{
				ParameterInfo info{};
				if (controller->getParameterInfo(parameterIndex, info) != kResultOk) { continue; }
				if ((info.flags & ParameterInfo::kIsReadOnly) != 0) { continue; }
				Parameter parameter;
				parameter.id = info.id;
				parameter.title = QString::fromUtf16(reinterpret_cast<const char16_t*>(info.title));
				parameter.normalized = controller->getParamNormalized(info.id);
				m_impl->parameters.push_back(parameter);
			}
		}

		qInfo("VST3 loaded %s, latency %d samples", qUtf8Printable(m_impl->pluginName), m_impl->latency);
		if (error != nullptr) { error->clear(); }
		return true;
	}
	catch (const std::exception& exception)
	{
		return fail(QStringLiteral("VST3 threw while loading %1: %2").arg(path, exception.what()));
	}
	catch (...)
	{
		return fail(QStringLiteral("VST3 threw while loading %1").arg(path));
	}
}

auto Vst3Instance::isLoaded() const -> bool
{
	return m_impl->loaded && !m_impl->broken;
}

auto Vst3Instance::path() const -> QString { return m_impl->path; }
auto Vst3Instance::classUid() const -> QString { return m_impl->classUid; }
auto Vst3Instance::name() const -> QString { return m_impl->pluginName; }
auto Vst3Instance::error() const -> QString { return m_impl->error; }
auto Vst3Instance::latencySamples() const -> int { return m_impl->latency; }

auto Vst3Instance::parameters() const -> std::vector<Parameter>
{
	std::lock_guard<std::mutex> lock(m_impl->mutex);
	return m_impl->parameters;
}

void Vst3Instance::setNormalized(unsigned int id, double normalized)
{
	std::lock_guard<std::mutex> lock(m_impl->mutex);
	if (!m_impl->loaded || m_impl->broken) { return; }
	const auto value = std::clamp(normalized, 0.0, 1.0);
	for (auto& parameter : m_impl->parameters)
	{
		if (parameter.id == id) { parameter.normalized = value; }
	}
	if (m_impl->controller)
	{
		m_impl->controller->setParamNormalized(id, value);
	}
	if (std::find(m_impl->dirtyParameters.begin(), m_impl->dirtyParameters.end(), id) == m_impl->dirtyParameters.end())
	{
		m_impl->dirtyParameters.push_back(id);
	}
}

void Vst3Instance::addNote(const MidiNote& note)
{
	std::lock_guard<std::mutex> lock(m_impl->mutex);
	if (m_impl->notes.size() >= kMaxNotes) { m_impl->notes.erase(m_impl->notes.begin()); }
	m_impl->notes.push_back(note);
}

auto Vst3Instance::process(SampleFrame* buf, f_cnt_t frames) -> bool
{
	if (buf == nullptr || frames <= 0) { return false; }
	std::unique_lock<std::mutex> lock(m_impl->mutex, std::try_to_lock);
	if (!lock.owns_lock())
	{
		clearBuffer(buf, frames);
		return true;
	}
	if (!m_impl->loaded || m_impl->broken || !m_impl->processor)
	{
		clearBuffer(buf, frames);
		return m_impl->loaded && !m_impl->broken;
	}
	if (static_cast<int>(frames) > m_impl->maxBlock)
	{
		clearBuffer(buf, frames);
		m_impl->error = QStringLiteral("Audio block is larger than the VST3 was prepared for");
		return false;
	}

	try
	{
		using namespace Steinberg;
		using namespace Steinberg::Vst;
		if (!m_impl->processing)
		{
			const auto started = m_impl->processor->setProcessing(true);
			if (started != kResultOk && started != kNotImplemented)
			{
				m_impl->broken = true;
				m_impl->error = QStringLiteral("setProcessing failed");
				clearBuffer(buf, frames);
				qWarning("VST3: %s", qUtf8Printable(m_impl->error));
				return false;
			}
			m_impl->processing = true;
		}

		const auto count = static_cast<size_t>(frames);
		for (size_t frame = 0; frame < count; ++frame)
		{
			m_impl->inputLeft[frame] = buf[frame][0];
			m_impl->inputRight[frame] = buf[frame][1];
			m_impl->outputLeft[frame] = 0.f;
			m_impl->outputRight[frame] = 0.f;
		}

		if (m_impl->inputChannels > 0)
		{
			float* channels[2] = {m_impl->inputLeft.data(), m_impl->inputRight.data()};
			m_impl->processData.setChannelBuffers(kInput, 0, channels, 2);
		}
		float* outputs[2] = {m_impl->outputLeft.data(), m_impl->outputRight.data()};
		m_impl->processData.setChannelBuffers(kOutput, 0, outputs, 2);
		m_impl->processData.numSamples = frames;

		m_impl->events.clear();
		for (const auto& note : m_impl->notes)
		{
			Event event{};
			event.busIndex = 0;
			event.sampleOffset = std::clamp(note.offset, 0, static_cast<int>(frames) - 1);
			event.flags = Event::kIsLive;
			if (note.on)
			{
				event.type = Event::kNoteOnEvent;
				event.noteOn.channel = static_cast<int16>(note.channel);
				event.noteOn.pitch = static_cast<int16>(note.pitch);
				event.noteOn.tuning = 0.f;
				event.noteOn.velocity = std::clamp(note.velocity, 0.f, 1.f);
				event.noteOn.length = 0;
				event.noteOn.noteId = -1;
			}
			else
			{
				event.type = Event::kNoteOffEvent;
				event.noteOff.channel = static_cast<int16>(note.channel);
				event.noteOff.pitch = static_cast<int16>(note.pitch);
				event.noteOff.velocity = std::clamp(note.velocity, 0.f, 1.f);
				event.noteOff.noteId = -1;
				event.noteOff.tuning = 0.f;
			}
			m_impl->events.addEvent(event);
		}
		m_impl->notes.clear();
		m_impl->processData.inputEvents = &m_impl->events;

		m_impl->parameterChanges.clearQueue();
		for (const auto id : m_impl->dirtyParameters)
		{
			int32 queueIndex = 0;
			if (auto* queue = m_impl->parameterChanges.addParameterData(id, queueIndex))
			{
				double normalized = 0.0;
				for (const auto& parameter : m_impl->parameters)
				{
					if (parameter.id == id) { normalized = parameter.normalized; }
				}
				int32 pointIndex = 0;
				queue->addPoint(0, normalized, pointIndex);
			}
		}
		m_impl->dirtyParameters.clear();
		m_impl->processData.inputParameterChanges = &m_impl->parameterChanges;

		const auto result = m_impl->processor->process(m_impl->processData);
		if (result != kResultOk && result != kResultTrue)
		{
			m_impl->broken = true;
			m_impl->error = QStringLiteral("process() failed");
			clearBuffer(buf, frames);
			qWarning("VST3: %s (%s)", qUtf8Printable(m_impl->error), qUtf8Printable(m_impl->pluginName));
			return false;
		}

		for (size_t frame = 0; frame < count; ++frame)
		{
			buf[frame][0] = m_impl->outputLeft[frame];
			buf[frame][1] = m_impl->outputRight[frame];
		}
		return true;
	}
	catch (const std::exception& exception)
	{
		m_impl->broken = true;
		m_impl->error = QStringLiteral("VST3 threw during playback: %1").arg(exception.what());
		clearBuffer(buf, frames);
		qWarning("VST3: %s", qUtf8Printable(m_impl->error));
		return false;
	}
	catch (...)
	{
		m_impl->broken = true;
		m_impl->error = QStringLiteral("VST3 threw during playback");
		clearBuffer(buf, frames);
		qWarning("VST3: %s", qUtf8Printable(m_impl->error));
		return false;
	}
}

auto Vst3Instance::saveState() const -> QByteArray
{
	std::lock_guard<std::mutex> lock(m_impl->mutex);
	if (!m_impl->loaded || !m_impl->component) { return {}; }
	QByteArray state;
	auto append = [&state](Steinberg::Vst::IComponent* component, Steinberg::Vst::IEditController* controller, bool controllerState) {
		Steinberg::MemoryStream stream;
		const auto result = controllerState
			? (controller ? controller->getState(&stream) : Steinberg::kResultOk)
			: component->getState(&stream);
		if (result != Steinberg::kResultOk && result != Steinberg::kResultTrue && result != Steinberg::kNotImplemented)
		{
			return false;
		}
		const auto size = static_cast<uint32_t>(std::max<Steinberg::TSize>(stream.getSize(), 0));
		state.append(reinterpret_cast<const char*>(&size), 4);
		if (size > 0) { state.append(stream.getData(), static_cast<int>(size)); }
		return true;
	};
	if (!append(m_impl->component.get(), m_impl->controller.get(), false)) { return {}; }
	if (!append(m_impl->component.get(), m_impl->controller.get(), true)) { return {}; }
	return state;
}

auto Vst3Instance::restoreState(const QByteArray& state) -> bool
{
	std::lock_guard<std::mutex> lock(m_impl->mutex);
	if (!m_impl->loaded || state.isEmpty() || !m_impl->component) { return false; }
	auto cursor = 0;
	const auto componentChunk = readChunk(state, cursor);
	const auto controllerChunk = readChunk(state, cursor);
	if (componentChunk.isNull() && controllerChunk.isNull()) { return false; }
	auto apply = [](auto* target, const QByteArray& chunk, auto setter) {
		if (target == nullptr || chunk.isEmpty()) { return true; }
		Steinberg::MemoryStream stream(const_cast<char*>(chunk.constData()), chunk.size());
		const auto result = setter(target, &stream);
		return result == Steinberg::kResultOk || result == Steinberg::kResultTrue || result == Steinberg::kNotImplemented;
	};
	const auto componentOk = apply(m_impl->component.get(), componentChunk, [](Steinberg::Vst::IComponent* component, Steinberg::IBStream* stream) {
		return component->setState(stream);
	});
	const auto controllerOk = apply(m_impl->controller.get(), controllerChunk, [](Steinberg::Vst::IEditController* controller, Steinberg::IBStream* stream) {
		return controller->setState(stream);
	});
	if (m_impl->controller)
	{
		for (auto& parameter : m_impl->parameters)
		{
			parameter.normalized = m_impl->controller->getParamNormalized(parameter.id);
		}
	}
	return componentOk && controllerOk;
}

} // namespace lmms
