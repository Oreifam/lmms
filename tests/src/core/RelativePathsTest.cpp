/*
 * RelativePathsTest.cpp
 *
 * Copyright (c) 2017 Tres Finocchiaro <tres/dot/finocchiaro/at/gmail.com>
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

#include <QDir>
#include <QFile>
#include <QObject>
#include <QTemporaryDir>
#include <QtTest>

#include "ConfigManager.h"
#include "PathUtil.h"

class RelativePathsTest : public QObject
{
	Q_OBJECT
private slots:
	void PathUtilComparisonTests()
	{
		using namespace lmms;

		QFileInfo fi(ConfigManager::inst()->factorySamplesDir() + "/drums/kick01.ogg");
		QVERIFY(fi.exists());

		QString absPath = fi.absoluteFilePath();
		QString oldRelPath = "drums/kick01.ogg";
		QString relPath = PathUtil::basePrefix(PathUtil::Base::FactorySample) + "drums/kick01.ogg";
		QString fuzPath = absPath;
		fuzPath.replace(relPath, "drums/.///kick01.ogg");

		//Test nicely formatted paths
		QCOMPARE(PathUtil::toShortestRelative(absPath), relPath);
		QCOMPARE(PathUtil::toAbsolute(relPath), absPath);

		//Test upgrading old paths
		QCOMPARE(PathUtil::toShortestRelative(oldRelPath), relPath);
		QCOMPARE(PathUtil::toAbsolute(oldRelPath), absPath);

		//Test weird but valid paths
		QCOMPARE(PathUtil::toShortestRelative(fuzPath), relPath);
		QCOMPARE(PathUtil::toAbsolute(fuzPath), absPath);

		//Empty paths should stay empty
		QString empty = QString("");
		QCOMPARE(PathUtil::stripPrefix(""), empty);
		QCOMPARE(PathUtil::cleanName(""), empty);
		QCOMPARE(PathUtil::toAbsolute(""), empty);
		QCOMPARE(PathUtil::toShortestRelative(""), empty);
	}

	void MissingPathFallback()
	{
		using namespace lmms;

		QTemporaryDir root;
		QVERIFY(root.isValid());

		auto* config = ConfigManager::inst();
		const auto previousSamples = config->userSamplesDir();
		const auto previousVst = config->vstDir();
		const auto userSamples = QDir(root.path() + "/library/samples").absolutePath() + "/";
		const auto userVst = QDir(root.path() + "/library/vst").absolutePath() + "/";
		config->setUserSamplesDir(userSamples);
		config->setVSTDir(userVst);

		QDir().mkpath(userSamples + "Pack");
		QFile sample(userSamples + "Pack/file.wav");
		QVERIFY(sample.open(QIODevice::WriteOnly));
		sample.close();
		QFile decoy(userSamples + "file.wav");
		QVERIFY(decoy.open(QIODevice::WriteOnly));
		decoy.close();

		QDir().mkpath(userVst + "Xakt");
		QFile plugin(userVst + "Xakt.dll");
		QVERIFY(plugin.open(QIODevice::WriteOnly));
		plugin.close();
		QFile module(userVst + "Xakt/module.sem");
		QVERIFY(module.open(QIODevice::WriteOnly));
		module.close();

		QDir().mkpath(userVst + "vendor");
		QFile nested(userVst + "vendor/Deep.dll");
		QVERIFY(nested.open(QIODevice::WriteOnly));
		nested.close();

		const auto factoryPack = PathUtil::basePrefix(PathUtil::Base::FactorySample) + "Pack/file.wav";
		QCOMPARE(QDir::cleanPath(PathUtil::toAbsolute(factoryPack)), QDir::cleanPath(userSamples + "Pack/file.wav"));

		const auto prefixedSamples = PathUtil::basePrefix(PathUtil::Base::UserSample) + "samples/Pack/file.wav";
		QCOMPARE(QDir::cleanPath(PathUtil::toAbsolute(prefixedSamples)), QDir::cleanPath(userSamples + "Pack/file.wav"));

		const auto oldSample = QDir(root.path() + "/Program Files/LMMS/data/samples/Pack/file.wav").absolutePath();
		QCOMPARE(QDir::cleanPath(PathUtil::toAbsolute(oldSample)), QDir::cleanPath(userSamples + "Pack/file.wav"));

		const auto oldPlugin = QDir(root.path() + "/Program Files/LMMS/data/presets/Xakt.dll").absolutePath();
		const auto resolvedPlugin = PathUtil::toAbsolute(oldPlugin);
		QCOMPARE(QDir::cleanPath(resolvedPlugin), QDir::cleanPath(userVst + "Xakt.dll"));
		QVERIFY(QFileInfo(QFileInfo(resolvedPlugin).absolutePath() + "/Xakt/module.sem").exists());

		const auto oldNested = QDir(root.path() + "/Program Files/LMMS/data/presets/Deep.dll").absolutePath();
		QCOMPARE(QDir::cleanPath(PathUtil::toAbsolute(oldNested)), QDir::cleanPath(userVst + "vendor/Deep.dll"));

		const auto stillThere = userSamples + "Pack/file.wav";
		QCOMPARE(QDir::cleanPath(PathUtil::toAbsolute(stillThere)), QDir::cleanPath(QFileInfo(stillThere).absoluteFilePath()));

		const auto missing = PathUtil::basePrefix(PathUtil::Base::FactorySample) + "no/such.wav";
		const auto expectedMissing = QDir::cleanPath(PathUtil::baseLocation(PathUtil::Base::FactorySample) + "no/such.wav");
		QCOMPARE(QDir::cleanPath(PathUtil::toAbsolute(missing)), expectedMissing);

		config->setUserSamplesDir(previousSamples);
		config->setVSTDir(previousVst);
	}

	//! Test UTF-8 encoded std::string --> std::filesystem:path conversion
	void StringToPathTest()
	{
		using namespace lmms;

		const auto qPath = ConfigManager::inst()->factorySamplesDir() + QString::fromUtf16(u"/fake/音楽.wav");
		const std::filesystem::path expected = qPath.toStdU16String();

		const std::filesystem::path actual = PathUtil::stringToPath(qPath.toStdString());

		const auto actualComparable = QString::fromStdU16String(actual.u16string());
		const auto expectedComparable = QString::fromStdU16String(expected.u16string());
		QCOMPARE(actualComparable, expectedComparable);
	}

	//! Test QString --> std::filesystem:path conversion
	void QStringToPathTest()
	{
		using namespace lmms;

		const auto qPath = ConfigManager::inst()->factorySamplesDir() + QString::fromUtf16(u"/fake/音楽.wav");
		const std::filesystem::path expected = qPath.toStdU16String();

		const std::filesystem::path actual = PathUtil::stringToPath(qPath);

		const auto actualComparable = QString::fromStdU16String(actual.u16string());
		const auto expectedComparable = QString::fromStdU16String(expected.u16string());
		QCOMPARE(actualComparable, expectedComparable);
	}

	//! Test std::filesystem:path --> UTF-8 encoded std::string conversion
	void PathToStringTest()
	{
		using namespace lmms;

		const auto expected = ConfigManager::inst()->factorySamplesDir() + QString::fromUtf16(u"/fake/音楽.wav");
		const std::filesystem::path filePath = expected.toStdU16String();

		const std::string actual = PathUtil::pathToString(filePath);

		const auto actualComparable = QString::fromStdString(actual);
		QCOMPARE(actualComparable, expected);
	}
};

QTEST_GUILESS_MAIN(RelativePathsTest)
#include "RelativePathsTest.moc"
