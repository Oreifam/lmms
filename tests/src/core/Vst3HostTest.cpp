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
};

QTEST_GUILESS_MAIN(Vst3HostTest)
#include "Vst3HostTest.moc"
