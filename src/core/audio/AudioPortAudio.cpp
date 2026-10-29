/*
 * AudioPortAudio.cpp - device-class that performs PCM-output via PortAudio
 *
 * Copyright (c) 2008 Csaba Hruska <csaba.hruska/at/gmail.com>
 * Copyright (c) 2010 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include <algorithm>
#include <array>
#include <iostream>
#include <vector>

#include "lmmsconfig.h"

#ifdef LMMS_HAVE_PORTAUDIO

#include <QLabel>

#include "AudioEngine.h"
#include "AudioPortAudio.h"
#include "ConfigManager.h"
#include "LcdSpinBox.h"
#include "MmThread.h"

#ifdef LMMS_BUILD_WIN32
#include <pa_win_wasapi.h>
#endif

namespace {
enum class Direction
{
	Input,
	Output
};

constexpr auto tag()
{
	return "audioportaudio";
}

constexpr auto backendAttribute()
{
	return "backend";
}

#ifdef LMMS_BUILD_WIN32
constexpr auto wasapiModeAttribute()
{
	return "wasapimode";
}

constexpr auto wasapiFramesAttribute()
{
	return "wasapiframes";
}

constexpr auto wasapiHostApiName()
{
	return "Windows WASAPI";
}

constexpr unsigned long defaultWasapiFrames = 512;
constexpr std::array<unsigned long, 3> wasapiFrameChoices{256, 512, 1024};
#endif

constexpr auto deviceNameAttribute(Direction direction)
{
	switch (direction)
	{
	case Direction::Input:
		return "inputdevice";
	case Direction::Output:
		return "outputdevice";
	}

	return "";
}

constexpr auto channelsAttribute(Direction direction)
{
	switch (direction)
	{
	case Direction::Input:
		return "inputchannels";
	case Direction::Output:
		return "outputchannels";
	}

	return "";
}

int maxChannels(const PaDeviceInfo* info, Direction direction)
{
	switch (direction)
	{
	case Direction::Input:
		return info->maxInputChannels;
	case Direction::Output:
		return info->maxOutputChannels;
	}

	return 0;
}

auto findHostApi(const QString& backendName) -> int
{
	const auto backendCount = Pa_GetHostApiCount();
	if (backendCount < 0) { return -1; }

	if (!backendName.isEmpty())
	{
		for (auto i = 0; i < backendCount; ++i)
		{
			const auto info = Pa_GetHostApiInfo(i);
			if (info && backendName == QString::fromUtf8(info->name)) { return i; }
		}
		return -1;
	}

#ifdef LMMS_BUILD_WIN32
	for (auto i = 0; i < backendCount; ++i)
	{
		const auto info = Pa_GetHostApiInfo(i);
		if (info && info->type == paWASAPI) { return i; }
	}
#endif

	return -1;
}

auto findDevice(int hostApi, const QString& name, Direction direction) -> PaDeviceIndex
{
	if (hostApi < 0) { return paNoDevice; }

	if (!name.isEmpty())
	{
		const auto deviceCount = Pa_GetDeviceCount();
		for (auto i = 0; i < deviceCount; ++i)
		{
			const auto info = Pa_GetDeviceInfo(i);
			if (!info || info->hostApi != hostApi || maxChannels(info, direction) <= 0) { continue; }
			if (name == QString::fromUtf8(info->name)) { return i; }
		}

		std::cerr << "PortAudio: device \"" << name.toUtf8().constData() << "\" was not found on this backend\n";
		if (direction == Direction::Input) { return paNoDevice; }
	}
	else if (direction == Direction::Input)
	{
		return paNoDevice;
	}

	const auto apiInfo = Pa_GetHostApiInfo(hostApi);
	if (!apiInfo) { return paNoDevice; }

	const auto fallback = direction == Direction::Output ? apiInfo->defaultOutputDevice : apiInfo->defaultInputDevice;
	if (fallback == paNoDevice) { return paNoDevice; }

	const auto info = Pa_GetDeviceInfo(fallback);
	if (!info || info->hostApi != hostApi || maxChannels(info, direction) <= 0) { return paNoDevice; }
	return fallback;
}

auto resolveChannels(PaDeviceIndex device, Direction direction) -> int
{
	const auto info = Pa_GetDeviceInfo(device);
	if (!info) { return 0; }

	const auto available = maxChannels(info, direction);
	if (available <= 0) { return 0; }

	const auto configured = lmms::ConfigManager::inst()->value(tag(), channelsAttribute(direction)).toInt();
	const auto requested = configured > 0 ? configured : static_cast<int>(lmms::DEFAULT_CHANNELS);
	return std::clamp(requested, 1, available);
}

#ifdef LMMS_BUILD_WIN32
auto snapWasapiFrames(unsigned long requested) -> unsigned long
{
	auto best = defaultWasapiFrames;
	auto bestDistance = requested + 1;
	for (const auto frames : wasapiFrameChoices)
	{
		const auto distance = requested > frames ? requested - frames : frames - requested;
		if (distance < bestDistance)
		{
			bestDistance = distance;
			best = frames;
		}
	}
	return best;
}
#endif

struct OpenedStream
{
	PaStream* stream = nullptr;
	double sampleRate = 0;
	unsigned long frames = 0;
	bool exclusive = false;
	bool fellBackToShared = false;
	int outputChannels = 0;
};

auto openGenericStream(PaDeviceIndex inputDevice, int inputChannels, PaDeviceIndex outputDevice, int outputChannels,
	double sampleRate, unsigned long framesPerBuffer, PaStreamCallback callback, void* userData) -> OpenedStream
{
	OpenedStream opened;
	opened.outputChannels = outputChannels;
	opened.sampleRate = sampleRate;
	opened.frames = framesPerBuffer;

	const auto inputLatency = inputDevice == paNoDevice ? 0. : Pa_GetDeviceInfo(inputDevice)->defaultLowInputLatency;
	const auto outputLatency = Pa_GetDeviceInfo(outputDevice)->defaultLowOutputLatency;

	const auto inputParameters = PaStreamParameters{.device = inputDevice,
		.channelCount = inputChannels,
		.sampleFormat = paFloat32,
		.suggestedLatency = inputLatency,
		.hostApiSpecificStreamInfo = nullptr};

	const auto outputParameters = PaStreamParameters{.device = outputDevice,
		.channelCount = outputChannels,
		.sampleFormat = paFloat32,
		.suggestedLatency = outputLatency,
		.hostApiSpecificStreamInfo = nullptr};

	const auto inputParametersPtr = inputDevice == paNoDevice ? nullptr : &inputParameters;
	auto err = Pa_IsFormatSupported(inputParametersPtr, &outputParameters, sampleRate);
	if (err != paFormatIsSupported)
	{
		std::cerr << "Pa_IsFormatSupported() failed: " << Pa_GetErrorText(err) << '\n';
		return {};
	}

	err = Pa_OpenStream(&opened.stream, inputParametersPtr, &outputParameters, sampleRate, framesPerBuffer, paNoFlag,
		callback, userData);
	if (err != paNoError)
	{
		std::cerr << "Pa_OpenStream() failed: " << Pa_GetErrorText(err) << '\n';
		opened.stream = nullptr;
		return {};
	}

	return opened;
}

#ifdef LMMS_BUILD_WIN32
struct WasapiAttempt
{
	bool exclusive = false;
	bool autoConvert = false;
	bool withInput = false;
	double sampleRate = 0;
	unsigned long frames = 0;
};

auto wasapiFrameOrder(unsigned long preferred) -> std::array<unsigned long, 3>
{
	// Ask for the chosen size first, then larger sizes. Shared mode rejects a
	// buffer shorter than the engine period (often ~10 ms, sometimes ~20 ms).
	std::array<unsigned long, 3> order{preferred, 0, 0};
	auto index = std::size_t{1};
	for (const auto frames : {1024ul, 512ul, 256ul})
	{
		if (frames != preferred) { order[index++] = frames; }
	}
	return order;
}

auto openWasapiStream(PaDeviceIndex inputDevice, int inputChannels, PaDeviceIndex outputDevice, int outputChannels,
	double requestedRate, unsigned long requestedFrames, bool wantExclusive, PaStreamCallback callback, void* userData)
	-> OpenedStream
{
	const auto outputInfo = Pa_GetDeviceInfo(outputDevice);
	if (!outputInfo) { return {}; }

	const auto deviceRate = outputInfo->defaultSampleRate;
	const auto framesOrder = wasapiFrameOrder(snapWasapiFrames(requestedFrames));
	const auto hasInput = inputDevice != paNoDevice && inputChannels > 0;

	auto rates = std::vector<double>{};
	auto pushRate = [&rates](double rate) {
		if (rate < 1.0) { return; }
		if (std::find(rates.begin(), rates.end(), rate) == rates.end()) { rates.push_back(rate); }
	};
	pushRate(requestedRate);
	pushRate(deviceRate);
	pushRate(48000.0);
	pushRate(44100.0);

	auto attempts = std::vector<WasapiAttempt>{};
	auto addAttempt = [&attempts](WasapiAttempt attempt) {
		for (const auto& existing : attempts)
		{
			if (existing.exclusive == attempt.exclusive && existing.autoConvert == attempt.autoConvert
				&& existing.withInput == attempt.withInput && existing.sampleRate == attempt.sampleRate
				&& existing.frames == attempt.frames)
			{
				return;
			}
		}
		attempts.push_back(attempt);
	};

	auto addMode = [&](bool exclusive, bool autoConvert, bool withInput, double rate) {
		if (withInput && !hasInput) { return; }
		for (const auto frames : framesOrder)
		{
			addAttempt(WasapiAttempt{exclusive, autoConvert, withInput, rate, frames});
		}
	};

	if (wantExclusive)
	{
		for (const auto rate : rates) { addMode(true, false, hasInput, rate); }
		if (hasInput)
		{
			for (const auto rate : rates) { addMode(true, false, false, rate); }
		}
	}

	for (const auto rate : rates) { addMode(false, false, hasInput, rate); }
	if (hasInput)
	{
		for (const auto rate : rates) { addMode(false, false, false, rate); }
	}
	// Shared-mode SRC. Lets a project rate that is not the mix format still play.
	for (const auto rate : rates) { addMode(false, true, false, rate); }

	PaError lastError = paNoError;
	for (const auto& attempt : attempts)
	{
		PaWasapiStreamInfo wasapiInfo{};
		wasapiInfo.size = sizeof(wasapiInfo);
		wasapiInfo.hostApiType = paWASAPI;
		wasapiInfo.version = 1;
		wasapiInfo.flags = paWinWasapiThreadPriority;
		wasapiInfo.threadPriority = eThreadPriorityProAudio;
		if (attempt.exclusive) { wasapiInfo.flags |= paWinWasapiExclusive; }
		if (attempt.autoConvert) { wasapiInfo.flags |= paWinWasapiAutoConvert; }

		const auto bufferSeconds = static_cast<double>(attempt.frames) / attempt.sampleRate;
		const auto outputLatency = attempt.exclusive ? bufferSeconds : (std::max)(outputInfo->defaultLowOutputLatency, bufferSeconds);

		PaStreamParameters inputParameters{};
		const PaStreamParameters* inputParametersPtr = nullptr;
		if (attempt.withInput)
		{
			const auto inputInfo = Pa_GetDeviceInfo(inputDevice);
			const auto inputLatency = attempt.exclusive || !inputInfo
				? bufferSeconds
				: (std::max)(inputInfo->defaultLowInputLatency, bufferSeconds);
			inputParameters.device = inputDevice;
			inputParameters.channelCount = inputChannels;
			inputParameters.sampleFormat = paFloat32;
			inputParameters.suggestedLatency = inputLatency;
			inputParameters.hostApiSpecificStreamInfo = &wasapiInfo;
			inputParametersPtr = &inputParameters;
		}

		PaStreamParameters outputParameters{};
		outputParameters.device = outputDevice;
		outputParameters.channelCount = outputChannels;
		outputParameters.sampleFormat = paFloat32;
		outputParameters.suggestedLatency = outputLatency;
		outputParameters.hostApiSpecificStreamInfo = &wasapiInfo;

		const auto supported = Pa_IsFormatSupported(inputParametersPtr, &outputParameters, attempt.sampleRate);
		if (supported != paFormatIsSupported)
		{
			lastError = supported;
			continue;
		}

		PaStream* stream = nullptr;
		const auto err = Pa_OpenStream(&stream, inputParametersPtr, &outputParameters, attempt.sampleRate,
			attempt.frames, paNoFlag, callback, userData);
		if (err != paNoError || !stream)
		{
			lastError = err;
			std::cerr << "WASAPI: failed to open " << (attempt.exclusive ? "exclusive" : "shared") << " mode at "
					  << attempt.sampleRate << " Hz / " << attempt.frames << " frames: " << Pa_GetErrorText(err)
					  << '\n';
			continue;
		}

		OpenedStream opened;
		opened.stream = stream;
		opened.sampleRate = attempt.sampleRate;
		opened.frames = attempt.frames;
		opened.exclusive = attempt.exclusive;
		opened.fellBackToShared = wantExclusive && !attempt.exclusive;
		opened.outputChannels = outputChannels;

		std::cerr << "WASAPI: opened " << (attempt.exclusive ? "exclusive" : "shared") << " mode, "
				  << outputInfo->name << ", " << attempt.sampleRate << " Hz, " << attempt.frames << " frames";
		if (attempt.autoConvert) { std::cerr << ", Windows format conversion"; }
		if (attempt.withInput) { std::cerr << ", full duplex"; }
		std::cerr << '\n';
		return opened;
	}

	std::cerr << "WASAPI: could not open an output stream (" << Pa_GetErrorText(lastError) << ")\n";
	return {};
}
#endif

} // namespace

namespace lmms {
AudioPortAudio::AudioPortAudio(bool& successful, AudioEngine* engine)
	: AudioDevice(DEFAULT_CHANNELS, engine)
{
	successful = false;

	const auto numDevices = Pa_GetDeviceCount();
	if (numDevices < 0)
	{
		std::cerr << "Pa_GetDeviceCount() failed: " << Pa_GetErrorText(numDevices) << '\n';
		return;
	}

	const auto backendName = ConfigManager::inst()->value(tag(), backendAttribute());
	const auto hostApi = findHostApi(backendName);
	if (hostApi < 0)
	{
		std::cerr << "PortAudio: backend \"" << backendName.toUtf8().constData() << "\" is not available\n";
		return;
	}

	const auto hostApiInfo = Pa_GetHostApiInfo(hostApi);
	const auto outputDevice = findDevice(hostApi, ConfigManager::inst()->value(tag(), deviceNameAttribute(Direction::Output)),
		Direction::Output);
	if (outputDevice == paNoDevice)
	{
		std::cerr << "PortAudio: no output device for backend \"" << (hostApiInfo ? hostApiInfo->name : "?") << "\"\n";
		return;
	}

	const auto inputDevice = findDevice(hostApi, ConfigManager::inst()->value(tag(), deviceNameAttribute(Direction::Input)),
		Direction::Input);
	const auto outputChannels = resolveChannels(outputDevice, Direction::Output);
	const auto inputChannels = inputDevice == paNoDevice ? 0 : resolveChannels(inputDevice, Direction::Input);
	if (outputChannels <= 0)
	{
		std::cerr << "PortAudio: output device has no channels\n";
		return;
	}

	const auto sampleRate = static_cast<double>(engine->baseSampleRate());
	OpenedStream opened;

#ifdef LMMS_BUILD_WIN32
	if (hostApiInfo && hostApiInfo->type == paWASAPI)
	{
		const auto mode = ConfigManager::inst()->value(tag(), wasapiModeAttribute(), QStringLiteral("shared"));
		const auto wantExclusive = mode.compare(QStringLiteral("exclusive"), Qt::CaseInsensitive) == 0;
		const auto configuredFrames = ConfigManager::inst()->value(tag(), wasapiFramesAttribute(),
			QString::number(static_cast<int>(defaultWasapiFrames))).toUInt();
		const auto frames = snapWasapiFrames(configuredFrames == 0
			? defaultWasapiFrames
			: static_cast<unsigned long>(configuredFrames));
		opened = openWasapiStream(inputDevice, inputChannels, outputDevice, outputChannels, sampleRate, frames,
			wantExclusive, &AudioPortAudio::processCallback, this);
		if (opened.fellBackToShared)
		{
			std::cerr << "WASAPI: exclusive mode was not available, using shared mode\n";
		}
	}
	else
#endif
	{
		// MME and DirectSound keep the period size that the PortAudio refactor settled on.
		// WASAPI uses its own 256/512/1024 frame setting above.
		opened = openGenericStream(inputDevice, inputChannels, outputDevice, outputChannels, sampleRate,
			static_cast<unsigned long>(engine->framesPerPeriod()), &AudioPortAudio::processCallback, this);
	}

	if (!opened.stream) { return; }

	m_paStream = opened.stream;

	if (static_cast<sample_rate_t>(opened.sampleRate + 0.5) != engine->baseSampleRate())
	{
		std::cerr << "PortAudio: opened at " << opened.sampleRate << " Hz because " << sampleRate
				  << " Hz is not supported by this device. Set the sample rate in Audio settings to match.\n";
	}

	successful = true;
	setSampleRate(static_cast<sample_rate_t>(opened.sampleRate + 0.5));
	setChannels(opened.outputChannels);
}

AudioPortAudio::~AudioPortAudio()
{
	stopProcessing();
	if (m_paStream) { Pa_CloseStream(m_paStream); }
}

void AudioPortAudio::startProcessingImpl()
{
	if (m_paStream) { Pa_StartStream(m_paStream); }
}

void AudioPortAudio::stopProcessingImpl()
{
	if (m_paStream) { Pa_StopStream(m_paStream); }
}

int AudioPortAudio::processCallback(const void*, void* output, unsigned long frameCount,
	const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* userData)
{
	[[maybe_unused]] thread_local const MmThreadRegistration mmcssRegistration;

	const auto device = static_cast<AudioPortAudio*>(userData);
	if (!device || !output)
	{
		return paContinue;
	}

	const auto channels = device->channels();
	const auto outputBuffer = static_cast<float*>(output);

	if (!device->isRunning())
	{
		std::fill_n(outputBuffer, frameCount * channels, 0.f);
		return paComplete;
	}

	device->audioEngine()->renderNextBuffer({outputBuffer, channels, frameCount});
	return paContinue;
}
} // namespace lmms

namespace lmms::gui {

class AudioPortAudioSetupWidget::DeviceSelectorWidget : public QGroupBox
{
public:
	DeviceSelectorWidget(const QString& deviceLabel, Direction direction, QWidget* parent = nullptr)
		: QGroupBox{parent}
		, m_deviceComboBox{new QComboBox{this}}
		, m_channelSpinBox{new LcdSpinBox{1, this}}
		, m_direction(direction)
	{
		m_channelSpinBox->setModel(&m_channelModel);

		const auto layout = new QFormLayout{this};
		layout->addRow(deviceLabel, m_deviceComboBox);
		layout->addRow(tr("Channels"), m_channelSpinBox);

		connect(m_deviceComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
			[this](int index) { refreshChannels(m_deviceComboBox->itemData(index).toInt()); });
	}

	void refreshFromConfig(PaHostApiIndex backendIndex)
	{
		using namespace lmms;

		m_deviceComboBox->clear();

		for (auto i = 0, deviceCount = Pa_GetDeviceCount(); i < deviceCount; ++i)
		{
			const auto deviceInfo = Pa_GetDeviceInfo(i);

			if (deviceInfo && maxChannels(deviceInfo, m_direction) > 0 && deviceInfo->hostApi == backendIndex)
			{
				m_deviceComboBox->addItem(QString::fromUtf8(deviceInfo->name), i);
			}
		}

		const auto selectedDeviceName = ConfigManager::inst()->value(tag(), deviceNameAttribute(m_direction));
		const auto selectedDeviceIndex = std::max(0, m_deviceComboBox->findText(selectedDeviceName));
		if (m_deviceComboBox->count() == 0) { return; }
		m_deviceComboBox->setCurrentIndex(selectedDeviceIndex);
	}

	void refreshChannels(PaDeviceIndex deviceIndex)
	{
		if (deviceIndex < 0 || Pa_GetDeviceInfo(deviceIndex) == nullptr) { return; }

		const auto maxChannelCount = maxChannels(Pa_GetDeviceInfo(deviceIndex), m_direction);
		const auto channelCount = ConfigManager::inst()->value(tag(), channelsAttribute(m_direction)).toInt();
		m_channelModel.setRange(1, std::max(1, maxChannelCount));
		m_channelModel.setValue(channelCount == 0 ? DEFAULT_CHANNELS : channelCount);
		m_channelSpinBox->setNumDigits(QString::number(std::max(1, maxChannelCount)).length());
	}

	void saveToConfig()
	{
		ConfigManager::inst()->setValue(tag(), deviceNameAttribute(m_direction), m_deviceComboBox->currentText());
		ConfigManager::inst()->setValue(tag(), channelsAttribute(m_direction), QString::number(m_channelModel.value()));
	}

private:
	QComboBox* m_deviceComboBox = nullptr;
	LcdSpinBox* m_channelSpinBox = nullptr;
	IntModel m_channelModel;
	Direction m_direction;
};

AudioPortAudioSetupWidget::AudioPortAudioSetupWidget(QWidget* parent)
	: AudioDeviceSetupWidget{AudioPortAudio::name(), parent}
	, m_backendComboBox{new QComboBox{this}}
	, m_inputDevice{new DeviceSelectorWidget{tr("Input device"), Direction::Input}}
	, m_outputDevice(new DeviceSelectorWidget{tr("Output device"), Direction::Output})
{
	constexpr auto formVerticalSpacing = 10;
	const auto form = new QFormLayout{this};
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	form->setVerticalSpacing(formVerticalSpacing);

	form->addRow(tr("Backend"), m_backendComboBox);

#ifdef LMMS_BUILD_WIN32
	m_wasapiModeLabel = new QLabel{tr("WASAPI mode"), this};
	m_wasapiModeComboBox = new QComboBox{this};
	m_wasapiModeComboBox->addItem(tr("Shared"), QStringLiteral("shared"));
	m_wasapiModeComboBox->addItem(tr("Exclusive"), QStringLiteral("exclusive"));
	m_wasapiModeComboBox->setToolTip(tr(
		"Shared mode keeps Windows audio mixing and works alongside other programs. "
		"Exclusive mode takes the device and can reach lower latency. "
		"If exclusive mode cannot start, playback falls back to shared mode."));

	m_wasapiFramesLabel = new QLabel{tr("WASAPI buffer"), this};
	m_wasapiFramesComboBox = new QComboBox{this};
	m_wasapiFramesComboBox->addItem(tr("256 frames"), 256);
	m_wasapiFramesComboBox->addItem(tr("512 frames"), 512);
	m_wasapiFramesComboBox->addItem(tr("1024 frames"), 1024);
	m_wasapiFramesComboBox->setToolTip(tr(
		"At 48 kHz, 256 frames is about 5.3 ms (best with exclusive mode), "
		"512 frames is about 10.7 ms (default, suits onboard Realtek shared mode), "
		"and 1024 frames is about 21.3 ms."));

	m_wasapiLatencyLabel = new QLabel{this};

	const auto savedMode = ConfigManager::inst()->value(tag(), wasapiModeAttribute(), QStringLiteral("shared"));
	const auto modeIndex = m_wasapiModeComboBox->findData(savedMode);
	m_wasapiModeComboBox->setCurrentIndex(modeIndex < 0 ? 0 : modeIndex);

	const auto savedFrames = ConfigManager::inst()->value(tag(), wasapiFramesAttribute(),
		QString::number(defaultWasapiFrames)).toInt();
	const auto framesIndex = m_wasapiFramesComboBox->findData(savedFrames);
	m_wasapiFramesComboBox->setCurrentIndex(framesIndex < 0 ? 1 : framesIndex);

	form->addRow(m_wasapiModeLabel, m_wasapiModeComboBox);
	form->addRow(m_wasapiFramesLabel, m_wasapiFramesComboBox);
	form->addRow(QString{}, m_wasapiLatencyLabel);

	connect(m_wasapiFramesComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
		[this](int) { updateWasapiLatency(); });
	updateWasapiLatency();
#endif

	form->addRow(m_outputDevice);
	form->addRow(m_inputDevice);

	connect(m_backendComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
		const auto backend = m_backendComboBox->itemData(index).toInt();
		if (backend >= 0)
		{
			m_inputDevice->refreshFromConfig(backend);
			m_outputDevice->refreshFromConfig(backend);
		}
		updateWasapiOptions(m_backendComboBox->itemText(index));
	});

	updateWasapiOptions({});
}

void AudioPortAudioSetupWidget::updateWasapiLatency()
{
#ifdef LMMS_BUILD_WIN32
	const auto frames = m_wasapiFramesComboBox->currentData().toInt();
	const auto rate = ConfigManager::inst()->value("audioengine", "samplerate",
		QString::number(DEFAULT_SAMPLE_RATE)).toDouble();
	if (frames <= 0 || rate <= 0.0)
	{
		m_wasapiLatencyLabel->clear();
		return;
	}

	const auto ms = 1000.0 * static_cast<double>(frames) / rate;
	m_wasapiLatencyLabel->setText(tr("About %1 ms at %2 Hz").arg(ms, 0, 'f', 1).arg(static_cast<int>(rate)));
#endif
}

void AudioPortAudioSetupWidget::updateWasapiOptions(const QString& backendName)
{
#ifdef LMMS_BUILD_WIN32
	const auto wasapi = backendName.contains(QStringLiteral("WASAPI"), Qt::CaseInsensitive);
	m_wasapiModeLabel->setVisible(wasapi);
	m_wasapiModeComboBox->setVisible(wasapi);
	m_wasapiFramesLabel->setVisible(wasapi);
	m_wasapiFramesComboBox->setVisible(wasapi);
	m_wasapiLatencyLabel->setVisible(wasapi);
#else
	(void)backendName;
#endif
}

void AudioPortAudioSetupWidget::show()
{
	static auto s_initGuard = detail::PortAudioInitializationGuard{};

	if (m_backendComboBox->count() == 0)
	{
		for (auto i = 0, backendCount = Pa_GetHostApiCount(); i < backendCount; ++i)
		{
			const auto info = Pa_GetHostApiInfo(i);
			if (!info) { continue; }
			m_backendComboBox->addItem(QString::fromUtf8(info->name), i);
		}

		const auto selectedBackendName = ConfigManager::inst()->value(tag(), backendAttribute());
		auto selectedBackendIndex = m_backendComboBox->findText(selectedBackendName);
#ifdef LMMS_BUILD_WIN32
		if (selectedBackendIndex < 0) { selectedBackendIndex = m_backendComboBox->findText(wasapiHostApiName()); }
#endif
		if (selectedBackendIndex < 0) { selectedBackendIndex = 0; }
		m_backendComboBox->setCurrentIndex(selectedBackendIndex);
	}

	AudioDeviceSetupWidget::show();
}

void AudioPortAudioSetupWidget::saveSettings()
{
	ConfigManager::inst()->setValue(tag(), backendAttribute(), m_backendComboBox->currentText());
	m_inputDevice->saveToConfig();
	m_outputDevice->saveToConfig();
#ifdef LMMS_BUILD_WIN32
	ConfigManager::inst()->setValue(tag(), wasapiModeAttribute(), m_wasapiModeComboBox->currentData().toString());
	ConfigManager::inst()->setValue(tag(), wasapiFramesAttribute(),
		QString::number(m_wasapiFramesComboBox->currentData().toInt()));
#endif
}
} // namespace lmms::gui

#endif // LMMS_HAVE_PORTAUDIO
