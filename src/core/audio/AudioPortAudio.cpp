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
#endif // _WIN32

#include <algorithm>
#include <array>
#include <exception>
#include <iostream>
#include <vector>

#include "lmmsconfig.h"

#ifdef LMMS_HAVE_PORTAUDIO

#include <QCoreApplication>
#include <QDebug>
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
	double outputLatency = 0;
	bool exclusive = false;
	bool fellBackToShared = false;
	bool autoConvert = false;
	bool withInput = false;
	int outputChannels = 0;
	int attemptCount = 0;
	int formatRejected = 0;
	int openFailed = 0;
	PaError lastError = paNoError;
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

	opened.withInput = inputDevice != paNoDevice;
	if (const auto* streamInfo = Pa_GetStreamInfo(opened.stream))
	{
		opened.sampleRate = streamInfo->sampleRate;
		opened.outputLatency = streamInfo->outputLatency;
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
	// Preferred size, then larger sizes ascending, then smaller sizes.
	// 256 -> 256, 512, 1024. 512 -> 512, 1024, 256. 1024 -> 1024, 512, 256.
	std::array<unsigned long, 3> order{};
	auto index = 0;
	order[index++] = preferred;
	for (const auto frames : {256ul, 512ul, 1024ul})
	{
		if (frames > preferred) { order[index++] = frames; }
	}
	for (const auto frames : {1024ul, 512ul, 256ul})
	{
		if (frames < preferred) { order[index++] = frames; }
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

	// Keep a configured input as long as possible. Shared full-duplex is tried
	// before any exclusive output-only open, and auto-convert is tried at the
	// requested rate before switching to the device mix rate.
	auto addPass = [&](bool exclusive, bool withInput) {
		addMode(exclusive, false, withInput, requestedRate);
		if (!exclusive) { addMode(false, true, withInput, requestedRate); }
		for (const auto rate : rates)
		{
			if (rate == requestedRate) { continue; }
			addMode(exclusive, false, withInput, rate);
		}
		if (!exclusive)
		{
			for (const auto rate : rates)
			{
				if (rate == requestedRate) { continue; }
				addMode(false, true, withInput, rate);
			}
		}
	};
	if (wantExclusive && hasInput) { addPass(true, true); }
	if (hasInput) { addPass(false, true); }
	if (wantExclusive) { addPass(true, false); }
	addPass(false, false);

	OpenedStream failed;
	failed.attemptCount = static_cast<int>(attempts.size());
	for (const auto& attempt : attempts)
	{
		PaWasapiStreamInfo wasapiInfo{};
		wasapiInfo.size = sizeof(wasapiInfo);
		wasapiInfo.hostApiType = paWASAPI;
		wasapiInfo.version = 1;
		// PortAudio always boosts this thread (PaWasapi_ThreadPriorityBoost).
		// The default class is "Audio", or "Pro Audio" when exclusive. Passing
		// eThreadPriorityNone is ignored. Ask for Pro Audio here and do not
		// register the callback thread again in processCallback.
		wasapiInfo.flags |= paWinWasapiThreadPriority;
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
			failed.lastError = supported;
			++failed.formatRejected;
			continue;
		}

		PaStream* stream = nullptr;
		const auto err = Pa_OpenStream(&stream, inputParametersPtr, &outputParameters, attempt.sampleRate,
			attempt.frames, paNoFlag, callback, userData);
		if (err != paNoError || !stream)
		{
			failed.lastError = err;
			++failed.openFailed;
			continue;
		}

		OpenedStream opened;
		opened.stream = stream;
		opened.sampleRate = attempt.sampleRate;
		opened.frames = attempt.frames;
		opened.exclusive = attempt.exclusive;
		opened.fellBackToShared = wantExclusive && !attempt.exclusive;
		opened.autoConvert = attempt.autoConvert;
		opened.withInput = attempt.withInput;
		opened.outputChannels = outputChannels;
		opened.attemptCount = failed.attemptCount;
		opened.formatRejected = failed.formatRejected;
		opened.openFailed = failed.openFailed;
		if (const auto* streamInfo = Pa_GetStreamInfo(stream))
		{
			opened.sampleRate = streamInfo->sampleRate;
			opened.outputLatency = streamInfo->outputLatency;
		}
		return opened;
	}

	qWarning("WASAPI: no stream after %d attempts (format rejected %d, open failed %d, last error %s)",
		failed.attemptCount, failed.formatRejected, failed.openFailed, Pa_GetErrorText(failed.lastError));
	return failed;
}
#endif // LMMS_BUILD_WIN32

struct OpenReport
{
	QString status;
	QString notice;
};

auto audioText(const char* sourceText) -> QString
{
	return QCoreApplication::translate("AudioPortAudio", sourceText);
}

auto makeOpenReport(const OpenedStream& opened, double requestedRate, unsigned long requestedFrames, bool wantExclusive,
	bool hadInput, const char* apiName) -> OpenReport
{
	const auto openedRate = static_cast<int>(opened.sampleRate + 0.5);
	const auto askedRate = static_cast<int>(requestedRate + 0.5);
	const auto latencyMs = opened.outputLatency * 1000.0;
	const auto mode = opened.exclusive ? audioText("exclusive") : audioText("shared");

	OpenReport report;
	report.status = audioText("%1 %2, %3 Hz, %4 frames, output latency %5 ms")
						.arg(QString::fromUtf8(apiName), mode)
						.arg(openedRate)
						.arg(static_cast<qulonglong>(opened.frames))
						.arg(latencyMs, 0, 'f', 1);
	if (opened.autoConvert) { report.status += audioText(". Windows format conversion is on"); }
	if (opened.withInput) { report.status += audioText(". Input is open"); }

	QStringList notes;
	if (wantExclusive && !opened.exclusive)
	{
		notes.append(audioText("Exclusive mode did not open. Using shared mode."));
	}
	if (hadInput && !opened.withInput)
	{
		notes.append(audioText("Recording input was not opened. Output continues without it."));
	}
	if (openedRate != askedRate)
	{
		notes.append(audioText("Requested %1 Hz. Opened %2 Hz because %1 Hz could not be started, "
							   "including with Windows format conversion.")
						 .arg(askedRate)
						 .arg(openedRate));
	}
	if (opened.frames != requestedFrames)
	{
		notes.append(audioText("Requested %1 frames. Opened %2 frames.")
						 .arg(static_cast<qulonglong>(requestedFrames))
						 .arg(static_cast<qulonglong>(opened.frames)));
	}
	report.notice = notes.join(QStringLiteral("\n"));
	return report;
}

auto currentAudioSignature() -> QString
{
	const auto* config = lmms::ConfigManager::inst();
	auto signature = config->value("audioengine", "audiodev") + QLatin1Char('|')
		+ config->value(tag(), backendAttribute()) + QLatin1Char('|');
#ifdef LMMS_BUILD_WIN32
	signature += config->value(tag(), wasapiModeAttribute()) + QLatin1Char('|');
#endif
	signature += config->value("audioengine", "samplerate") + QLatin1Char('|')
		+ config->value("audioengine", "framesperaudiobuffer") + QLatin1Char('|')
		+ config->value(tag(), deviceNameAttribute(Direction::Output)) + QLatin1Char('|')
		+ config->value(tag(), deviceNameAttribute(Direction::Input));
	return signature;
}

} // namespace

namespace lmms {
AudioPortAudio::AudioPortAudio(bool& successful, AudioEngine* engine)
	: AudioDevice(DEFAULT_CHANNELS, engine)
{
	s_openStatus.clear();
	s_startupNotice.clear();
	s_noticeSignature = currentAudioSignature();
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
		const auto frames = snapWasapiFrames(static_cast<unsigned long>(engine->framesPerAudioBuffer()));
		m_skipCallbackMmcss = true;
		m_wasapiInputDevice = inputDevice;
		m_wasapiInputChannels = inputChannels;
		m_wasapiOutputDevice = outputDevice;
		m_wasapiOutputChannels = outputChannels;
		m_wasapiRequestedRate = sampleRate;
		m_wasapiFrames = frames;
		opened = openWasapiStream(inputDevice, inputChannels, outputDevice, outputChannels, sampleRate, frames,
			wantExclusive, &AudioPortAudio::processCallback, this);
		m_wasapiExclusive = opened.exclusive;
		if (opened.stream)
		{
			const auto hadInput = inputDevice != paNoDevice && inputChannels > 0;
			const auto report = makeOpenReport(opened, sampleRate, frames, wantExclusive, hadInput, "WASAPI");
			s_openStatus = report.status;
			s_startupNotice = report.notice;
		}
	}
	else
#endif // LMMS_BUILD_WIN32
	{
		// MME and DirectSound keep the period size that the PortAudio refactor settled on.
		const auto frames = static_cast<unsigned long>(engine->framesPerPeriod());
		opened = openGenericStream(inputDevice, inputChannels, outputDevice, outputChannels, sampleRate, frames,
			&AudioPortAudio::processCallback, this);
		if (opened.stream)
		{
			const auto hadInput = inputDevice != paNoDevice && inputChannels > 0;
			const auto apiName = hostApiInfo ? hostApiInfo->name : "PortAudio";
			const auto report = makeOpenReport(opened, sampleRate, frames, false, hadInput, apiName);
			s_openStatus = report.status;
			s_startupNotice = report.notice;
		}
	}

	if (!s_openStatus.isEmpty()) { qWarning("%s", qUtf8Printable(s_openStatus)); }
	if (!s_startupNotice.isEmpty()) { qWarning("%s", qUtf8Printable(s_startupNotice)); }

	if (!opened.stream) { return; }

	m_paStream = opened.stream;
	successful = true;
	setSampleRate(static_cast<sample_rate_t>(opened.sampleRate + 0.5));
	setChannels(opened.outputChannels);
}

QString AudioPortAudio::s_openStatus;
QString AudioPortAudio::s_startupNotice;
QString AudioPortAudio::s_noticeSignature;

auto AudioPortAudio::openStatus() -> QString
{
	return s_openStatus;
}

auto AudioPortAudio::startupNotice() -> QString
{
	return s_startupNotice;
}

auto AudioPortAudio::unseenStartupNotice() -> QString
{
	auto notice = s_startupNotice;
	const auto migration = ConfigManager::inst()->value(tag(), "migrationnotice");
	if (migration == QLatin1String("switched"))
	{
		const auto text = QCoreApplication::translate("AudioPortAudio",
			"This Windows setup was switched from SDL to PortAudio using WASAPI shared mode for lower latency. "
			"Your buffer size was kept when it was 256, 512, or 1024 frames. "
			"You can switch the audio interface back in Settings.");
		notice = notice.isEmpty() ? text : text + QLatin1Char('\n') + notice;
	}
	else if (migration == QLatin1String("fallback"))
	{
		const auto text = QCoreApplication::translate("AudioPortAudio",
			"LMMS tried to switch this Windows setup from SDL to WASAPI, but PortAudio did not open. SDL is still in use.");
		notice = notice.isEmpty() ? text : text + QLatin1Char('\n') + notice;
	}

	if (notice.isEmpty()) { return {}; }
	if (migration.isEmpty())
	{
		const auto seen = ConfigManager::inst()->value(tag(), "noticesignature");
		if (seen == s_noticeSignature) { return {}; }
	}
	return notice;
}

void AudioPortAudio::acknowledgeStartupNotice()
{
	ConfigManager::inst()->deleteValue(tag(), "migrationnotice");
	if (!s_noticeSignature.isEmpty())
	{
		ConfigManager::inst()->setValue(tag(), "noticesignature", s_noticeSignature);
	}
	ConfigManager::inst()->saveConfigFile();
}

AudioPortAudio::~AudioPortAudio()
{
	stopProcessing();
	if (m_paStream) { Pa_CloseStream(m_paStream); }
}

void AudioPortAudio::startProcessingImpl()
{
	if (!m_paStream) { return; }

	const auto err = Pa_StartStream(m_paStream);
	if (err == paNoError) { return; }

	const auto startFailed = QCoreApplication::translate("AudioPortAudio", "Playback did not start: %1")
								 .arg(QString::fromUtf8(Pa_GetErrorText(err)));
	s_openStatus = startFailed;
	s_startupNotice = startFailed;
	qWarning("%s", qUtf8Printable(startFailed));

#ifdef LMMS_BUILD_WIN32
	if (!m_wasapiExclusive) { return; }

	Pa_CloseStream(m_paStream);
	m_paStream = nullptr;
	m_wasapiExclusive = false;
	auto opened = openWasapiStream(m_wasapiInputDevice, m_wasapiInputChannels, m_wasapiOutputDevice,
		m_wasapiOutputChannels, m_wasapiRequestedRate, m_wasapiFrames, false, &AudioPortAudio::processCallback, this);
	if (!opened.stream)
	{
		s_startupNotice = QCoreApplication::translate("AudioPortAudio",
			"Exclusive mode opened but did not start, and shared mode could not be opened.");
		s_openStatus = s_startupNotice;
		qWarning("%s", qUtf8Printable(s_startupNotice));
		return;
	}

	const auto hadInput = m_wasapiInputDevice != paNoDevice && m_wasapiInputChannels > 0;
	const auto report = makeOpenReport(opened, m_wasapiRequestedRate, m_wasapiFrames, true, hadInput, "WASAPI");
	s_openStatus = report.status;
	s_startupNotice = report.notice.isEmpty()
		? QCoreApplication::translate("AudioPortAudio",
			"Exclusive mode opened but did not start. Switched to shared mode.")
		: report.notice;
	qWarning("%s", qUtf8Printable(s_openStatus));
	qWarning("%s", qUtf8Printable(s_startupNotice));

	const auto previousRate = sampleRate();
	m_paStream = opened.stream;
	setSampleRate(static_cast<sample_rate_t>(opened.sampleRate + 0.5));
	setChannels(opened.outputChannels);
	if (sampleRate() != previousRate) { audioEngine()->notifyDeviceSampleRateChanged(); }

	const auto retry = Pa_StartStream(m_paStream);
	if (retry != paNoError)
	{
		const auto sharedFailed = QCoreApplication::translate("AudioPortAudio", "Shared mode did not start: %1")
									  .arg(QString::fromUtf8(Pa_GetErrorText(retry)));
		s_openStatus = sharedFailed;
		s_startupNotice = sharedFailed;
		qWarning("%s", qUtf8Printable(sharedFailed));
	}
#endif // LMMS_BUILD_WIN32
}

void AudioPortAudio::stopProcessingImpl()
{
	if (m_paStream) { Pa_StopStream(m_paStream); }
}

int AudioPortAudio::processCallback(const void*, void* output, unsigned long frameCount,
	const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* userData)
{
	const auto device = static_cast<AudioPortAudio*>(userData);
	// WASAPI already boosted this thread inside PortAudio. MME and DirectSound have not.
	if (device != nullptr && !device->m_skipCallbackMmcss)
	{
		[[maybe_unused]] thread_local const MmThreadRegistration mmcssRegistration{true};
	}
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
		if (m_direction == Direction::Input) { m_deviceComboBox->addItem(tr("[None]"), -1); }

		for (auto i = 0, deviceCount = Pa_GetDeviceCount(); i < deviceCount; ++i)
		{
			const auto deviceInfo = Pa_GetDeviceInfo(i);

			if (deviceInfo && maxChannels(deviceInfo, m_direction) > 0 && deviceInfo->hostApi == backendIndex)
			{
				m_deviceComboBox->addItem(QString::fromUtf8(deviceInfo->name), i);
			}
		}

		const auto selectedDeviceName = ConfigManager::inst()->value(tag(), deviceNameAttribute(m_direction));
		if (m_deviceComboBox->count() == 0) { return; }
		if (m_direction == Direction::Input && selectedDeviceName.isEmpty())
		{
			m_deviceComboBox->setCurrentIndex(0);
			return;
		}
		const auto found = m_deviceComboBox->findText(selectedDeviceName);
		m_deviceComboBox->setCurrentIndex(found >= 0 ? found : 0);
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
		const auto device = m_deviceComboBox->currentData().toInt();
		const auto name = m_direction == Direction::Input && device < 0 ? QString{} : m_deviceComboBox->currentText();
		ConfigManager::inst()->setValue(tag(), deviceNameAttribute(m_direction), name);
		if (device >= 0)
		{
			ConfigManager::inst()->setValue(tag(), channelsAttribute(m_direction), QString::number(m_channelModel.value()));
		}
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
		"If exclusive mode cannot start, playback falls back to shared mode. "
		"With an input device selected, exclusive mode usually cannot open full-duplex and "
		"falls back to shared mode with the input kept. "
		"Buffer size follows the Audio settings slider, snapped to 256, 512, or 1024 frames."));

	const auto savedMode = ConfigManager::inst()->value(tag(), wasapiModeAttribute(), QStringLiteral("shared"));
	const auto modeIndex = m_wasapiModeComboBox->findData(savedMode);
	m_wasapiModeComboBox->setCurrentIndex(modeIndex < 0 ? 0 : modeIndex);

	form->addRow(m_wasapiModeLabel, m_wasapiModeComboBox);
#endif // LMMS_BUILD_WIN32

	m_openStatusLabel = new QLabel{this};
	m_openStatusLabel->setWordWrap(true);
	form->addRow(m_openStatusLabel);
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

void AudioPortAudioSetupWidget::updateWasapiOptions(const QString& backendName)
{
#ifdef LMMS_BUILD_WIN32
	const auto wasapi = backendName.contains(QStringLiteral("WASAPI"), Qt::CaseInsensitive);
	m_wasapiModeLabel->setVisible(wasapi);
	m_wasapiModeComboBox->setVisible(wasapi);
#else
	(void)backendName;
#endif
}

void AudioPortAudioSetupWidget::show()
{
	try
	{
		static auto s_initGuard = detail::PortAudioInitializationGuard{};
		(void)s_initGuard;
	}
	catch (const std::exception& ex)
	{
		qWarning("PortAudio: %s", ex.what());
		m_openStatusLabel->setText(tr("PortAudio failed to initialize."));
		return;
	}

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

	const auto status = AudioPortAudio::openStatus();
	m_openStatusLabel->setText(status.isEmpty()
		? tr("No PortAudio stream is open. Audio changes apply after restart.")
		: status);

	AudioDeviceSetupWidget::show();
}

void AudioPortAudioSetupWidget::saveSettings()
{
	ConfigManager::inst()->setValue(tag(), backendAttribute(), m_backendComboBox->currentText());
	m_inputDevice->saveToConfig();
	m_outputDevice->saveToConfig();
#ifdef LMMS_BUILD_WIN32
	ConfigManager::inst()->setValue(tag(), wasapiModeAttribute(), m_wasapiModeComboBox->currentData().toString());
#endif // LMMS_BUILD_WIN32
}
} // namespace lmms::gui

#endif // LMMS_HAVE_PORTAUDIO
