/*
 * Vst3HostTest.cpp - scan, load, process, and state round-trip for the VST3 host
 *
 * Copyright (c) 2026 LMMS developers
 *
 * This file is part of LMMS - https://lmms.io
 */

#include <cmath>
#include <vector>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QTemporaryDir>
#include <QtTest>

#include "ConfigManager.h"
#include "SampleFrame.h"
#include "Vst3Instance.h"

class Vst3HostTest : public QObject
{
	Q_OBJECT
private slots:
	void ScanLoadProcessAndRestore()
	{
		QTemporaryDir root;
		QVERIFY(root.isValid());
		QDir().mkpath(root.path() + QStringLiteral("/empty/nested"));
		QFile marker(root.path() + QStringLiteral("/empty/nested/NotAPlugin.txt"));
		QVERIFY(marker.open(QIODevice::WriteOnly));
		marker.close();
		QDir().mkpath(root.path() + QStringLiteral("/Pack.vst3/Contents"));
		QCOMPARE(lmms::Vst3Instance::scanDirectory(root.path() + QStringLiteral("/empty")).size(), 0);
		const auto bundles = lmms::Vst3Instance::scanDirectory(root.path());
		QCOMPARE(bundles.size(), 1);
		QVERIFY(bundles.first().endsWith(QStringLiteral("Pack.vst3")));

		const QString probeSo = QStringLiteral(LMMS_VST3_PROBE_PATH);
		QVERIFY2(QFileInfo::exists(probeSo), qPrintable(probeSo));
		const auto machine = QSysInfo::currentCpuArchitecture() == QStringLiteral("x86_64")
			? QStringLiteral("x86_64")
			: QSysInfo::currentCpuArchitecture();
#if defined(Q_OS_WIN)
		const auto folder = machine + QStringLiteral("-win");
		const auto binaryName = QStringLiteral("LmmsVst3Probe.vst3");
#else
		const auto folder = machine + QStringLiteral("-linux");
		const auto binaryName = QStringLiteral("LmmsVst3Probe.so");
#endif
		const auto bundle = root.path() + QStringLiteral("/LmmsVst3Probe.vst3");
		QDir().mkpath(bundle + QStringLiteral("/Contents/") + folder);
		const auto binary = bundle + QStringLiteral("/Contents/") + folder + QStringLiteral("/") + binaryName;
		QVERIFY(QFile::copy(probeSo, binary));

		lmms::Vst3Instance plugin;
		QString error;
		QVERIFY2(plugin.load(bundle, 48000, 256, &error), qPrintable(error));
		QCOMPARE(plugin.latencySamples(), 17);
		QCOMPARE(plugin.name(), QStringLiteral("Lmms VST3 Probe"));

		std::vector<lmms::SampleFrame> audio(64);
		for (auto& frame : audio)
		{
			frame[0] = 0.5f;
			frame[1] = 0.5f;
		}
		QVERIFY(plugin.process(audio.data(), 64));
		QCOMPARE(audio[0][0], 0.5f);

		lmms::Vst3Instance::MidiNote note;
		note.on = true;
		note.pitch = 60;
		note.velocity = 1.f;
		plugin.addNote(note);
		QVERIFY(plugin.process(audio.data(), 64));
		QVERIFY(std::fabs(audio[0][0] - 0.7f) < 0.0001f);

		const auto state = plugin.saveState();
		QVERIFY(!state.isEmpty());
		QVERIFY(!plugin.parameters().empty());
		plugin.setNormalized(plugin.parameters().front().id, 0.0);
		for (auto& frame : audio)
		{
			frame[0] = 0.5f;
			frame[1] = 0.5f;
		}
		QVERIFY(plugin.process(audio.data(), 64));
		QVERIFY(std::fabs(audio[0][0]) < 0.0001f);
		QVERIFY(plugin.restoreState(state));
		for (auto& frame : audio)
		{
			frame[0] = 0.5f;
			frame[1] = 0.5f;
		}
		QVERIFY(plugin.process(audio.data(), 64));
		QVERIFY(std::fabs(audio[0][0] - 0.7f) < 0.0001f);

		QString missing;
		QVERIFY(!plugin.load(root.path() + QStringLiteral("/missing.vst3"), 48000, 256, &missing));
		QVERIFY(missing.contains(QStringLiteral("not found")));
	}

	void ProjectPathMustStayInsideAllowedFolders()
	{
		QString error;
		auto rejected = [&error](const QString& path, const QString& reason) {
			error.clear();
			const auto accepted = lmms::Vst3Instance::acceptedProjectPath(path, &error);
			return accepted.isEmpty() && error.contains(reason);
		};
		QVERIFY(rejected(QStringLiteral("\\\\server\\share\\Plugin.vst3"), QStringLiteral("UNC")));
		QVERIFY(rejected(QStringLiteral("//server/share/Plugin.vst3"), QStringLiteral("UNC")));
		QVERIFY(rejected(QStringLiteral("/\\server\\share\\X.vst3"), QStringLiteral("UNC")));
		QVERIFY(rejected(QStringLiteral("\\/server/share/X.vst3"), QStringLiteral("UNC")));
		QVERIFY(rejected(QStringLiteral("\\\\?\\C:\\Plugin.vst3"), QStringLiteral("UNC")));
		QVERIFY(rejected(QStringLiteral("\\\\.\\C:\\Plugin.vst3"), QStringLiteral("UNC")));
		QVERIFY(rejected(QStringLiteral("Plugin.vst3"), QStringLiteral("relative")));
		QVERIFY(rejected(QStringLiteral("/tmp/../etc/Plugin.vst3"), QStringLiteral("..")));
		QVERIFY(rejected(QStringLiteral("/tmp/Plugin.dll"), QStringLiteral(".vst3")));

		QTemporaryDir root;
		QVERIFY(root.isValid());
		const auto allowed = root.path() + QStringLiteral("/Allowed");
		const auto outside = root.path() + QStringLiteral("/Outside");
		QDir().mkpath(allowed + QStringLiteral("/Plug.vst3"));
		QDir().mkpath(outside + QStringLiteral("/Plug.vst3"));
		auto* config = lmms::ConfigManager::inst();
		const auto previous = config->value(QStringLiteral("vst3"), QStringLiteral("extradir"));
		config->setValue(QStringLiteral("vst3"), QStringLiteral("extradir"), allowed);

		error.clear();
		const auto inside = lmms::Vst3Instance::acceptedProjectPath(allowed + QStringLiteral("/Plug.vst3"), &error);
		QVERIFY2(!inside.isEmpty(), qPrintable(error));
		QVERIFY(inside.endsWith(QStringLiteral("Plug.vst3")));

		const auto escaped = lmms::Vst3Instance::acceptedProjectPath(outside + QStringLiteral("/Plug.vst3"), &error);
		QVERIFY(escaped.isEmpty());
		QVERIFY(error.contains(QStringLiteral("allowed VST3 folder")));

		config->setValue(QStringLiteral("vst3"), QStringLiteral("extradir"), previous);
	}
};

QTEST_GUILESS_MAIN(Vst3HostTest)
#include "Vst3HostTest.moc"
