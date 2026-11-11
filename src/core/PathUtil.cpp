/*
 * PathUtil.cpp
 *
 * Copyright (c) 2019-2022 Spekular <Spekularr@gmail.com>
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

#include "PathUtil.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>

#include "ConfigManager.h"
#include "Engine.h"
#include "lmmsconfig.h" // IWYU pragma: keep
#include "IoHelper.h" // IWYU pragma: keep
#include "Song.h"

namespace
{
using lmms::PathUtil::Base;

auto pathHasComponent(const QString& path, const QString& component) -> bool
{
	const auto parts = path.split('/', Qt::SkipEmptyParts);
	for (const auto& part : parts)
	{
		if (part.compare(component, Qt::CaseInsensitive) == 0) { return true; }
	}
	return false;
}

auto isPluginFile(const QString& path) -> bool
{
	const auto suffix = QFileInfo(path).suffix().toLower();
	return suffix == QLatin1String("dll") || suffix == QLatin1String("so") || suffix == QLatin1String("vst3");
}

void addRemainder(QStringList& remainders, const QString& candidate)
{
	auto remainder = QDir::cleanPath(candidate);
	while (remainder.startsWith(QLatin1String("./"))) { remainder = remainder.mid(2); }
	if (remainder.isEmpty() || remainder == QLatin1String(".") || QFileInfo(remainder).isAbsolute()) { return; }
	if (!remainders.contains(remainder)) { remainders.append(remainder); }
}

auto remaindersFor(const QString& original) -> QStringList
{
	QStringList remainders;
	addRemainder(remainders, lmms::PathUtil::stripPrefix(original));

	if (QFileInfo(original).isAbsolute())
	{
		const auto lowered = original.toLower();
		for (const auto& component : {QStringLiteral("samples"), QStringLiteral("presets"), QStringLiteral("vst")})
		{
			const auto needle = QStringLiteral("/") + component + QStringLiteral("/");
			const auto index = lowered.indexOf(needle);
			if (index >= 0) { addRemainder(remainders, original.mid(index + needle.size())); }
		}
	}

	const auto snapshot = remainders;
	for (const auto& remainder : snapshot)
	{
		if (remainder.startsWith(QLatin1String("samples/"), Qt::CaseInsensitive))
		{
			addRemainder(remainders, remainder.mid(int(QStringLiteral("samples/").size())));
		}
	}
	// The bare file name is the least specific remainder, so it is tried last.
	addRemainder(remainders, QFileInfo(original).fileName());
	return remainders;
}

auto findVstByFileName(const QString& fileName) -> QString
{
	static QString cachedDir;
	static QHash<QString, QString> cache;
	const auto dir = lmms::PathUtil::baseLocation(Base::UserVST);
	if (cachedDir != dir)
	{
		cache.clear();
		cachedDir = dir;
		QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
		while (it.hasNext())
		{
			it.next();
			const auto key = it.fileName().toLower();
			if (!cache.contains(key)) { cache.insert(key, QDir::cleanPath(it.filePath())); }
		}
	}
	return cache.value(fileName.toLower());
}

auto locateMovedFile(const QString& original) -> QString
{
	const auto remainders = remaindersFor(original);
	const auto pluginLike = isPluginFile(original)
		|| lmms::PathUtil::baseLookup(original) == Base::UserVST
		|| pathHasComponent(original, QStringLiteral("presets"))
		|| pathHasComponent(original, QStringLiteral("vst"));

	for (const auto& remainder : remainders)
	{
		const auto userSample = lmms::PathUtil::baseLocation(Base::UserSample) + remainder;
		if (QFileInfo::exists(userSample)) { return QDir::cleanPath(userSample); }
		const auto factorySample = lmms::PathUtil::baseLocation(Base::FactorySample) + remainder;
		if (QFileInfo::exists(factorySample)) { return QDir::cleanPath(factorySample); }
	}

	if (!pluginLike) { return {}; }

	for (const auto& remainder : remainders)
	{
		const auto vstPath = lmms::PathUtil::baseLocation(Base::UserVST) + remainder;
		if (QFileInfo::exists(vstPath)) { return QDir::cleanPath(vstPath); }
	}
	const auto fileName = QFileInfo(original).fileName();
	const auto direct = lmms::PathUtil::baseLocation(Base::UserVST) + fileName;
	if (QFileInfo::exists(direct)) { return QDir::cleanPath(direct); }
	return findVstByFileName(fileName);
}
} // namespace

namespace lmms::PathUtil
{
	auto relativeBases = std::array{ Base::ProjectDir, Base::FactoryProjects, Base::FactorySample, Base::UserSample, Base::UserVST, Base::Preset,
		Base::FactoryPresets, Base::UserLADSPA, Base::DefaultLADSPA, Base::UserSoundfont, Base::DefaultSoundfont, Base::UserGIG, Base::DefaultGIG,
		Base::LocalDir };

	QString baseLocation(const Base base, bool* error /* = nullptr*/)
	{
		// error is false unless something goes wrong
		if (error) { *error = false; }

		QString loc = "";
		switch (base)
		{
			case Base::ProjectDir       : loc = ConfigManager::inst()->userProjectsDir(); break;
			case Base::FactoryProjects :
			{
				QDir fpd = QDir(ConfigManager::inst()->factoryProjectsDir());
				loc = fpd.absolutePath(); break;
			}
			case Base::FactorySample    :
			{
				QDir fsd = QDir(ConfigManager::inst()->factorySamplesDir());
				loc = fsd.absolutePath(); break;
			}
			case Base::UserSample       : loc = ConfigManager::inst()->userSamplesDir(); break;
			case Base::UserVST          : loc = ConfigManager::inst()->userVstDir(); break;
			case Base::Preset           : loc = ConfigManager::inst()->userPresetsDir(); break;
			case Base::FactoryPresets    :
			{
				QDir fpd = QDir(ConfigManager::inst()->factoryPresetsDir());
				loc = fpd.absolutePath(); break;
			}
			case Base::UserLADSPA       : loc = ConfigManager::inst()->ladspaDir(); break;
			case Base::DefaultLADSPA    : loc = ConfigManager::inst()->userLadspaDir(); break;
			case Base::UserSoundfont    : loc = ConfigManager::inst()->sf2Dir(); break;
			case Base::DefaultSoundfont : loc = ConfigManager::inst()->userSf2Dir(); break;
			case Base::UserGIG          : loc = ConfigManager::inst()->gigDir(); break;
			case Base::DefaultGIG       : loc = ConfigManager::inst()->userGigDir(); break;
			case Base::LocalDir:
			{
				const Song* s = Engine::getSong();
				QString projectPath;
				if (s)
				{
					projectPath = s->projectFileName();
					loc = QFileInfo(projectPath).path();
				}
				// We resolved it properly if we had an open Song and the project
				// filename wasn't empty
				if (error) { *error = (!s || projectPath.isEmpty()); }
				break;
			}
			default                   : return QString("");
		}
		return QDir::cleanPath(loc) + "/";
	}

	QDir baseQDir (const Base base, bool* error /* = nullptr*/)
	{
		if (base == Base::Absolute)
		{
			if (error) { *error = false; }
			return QDir::root();
		}
		return QDir(baseLocation(base, error));
	}

	QString basePrefix(const Base base)
	{
		switch (base)
		{
			case Base::ProjectDir       : return QStringLiteral("userprojects:");
			case Base::FactoryProjects  : return QStringLiteral("factoryprojects:");
			case Base::FactorySample    : return QStringLiteral("factorysample:");
			case Base::UserSample       : return QStringLiteral("usersample:");
			case Base::UserVST          : return QStringLiteral("uservst:");
			case Base::Preset           : return QStringLiteral("preset:");
			case Base::FactoryPresets   : return QStringLiteral("factorypreset:");
			case Base::UserLADSPA       : return QStringLiteral("userladspa:");
			case Base::DefaultLADSPA    : return QStringLiteral("defaultladspa:");
			case Base::UserSoundfont    : return QStringLiteral("usersoundfont:");
			case Base::DefaultSoundfont : return QStringLiteral("defaultsoundfont:");
			case Base::UserGIG          : return QStringLiteral("usergig:");
			case Base::DefaultGIG       : return QStringLiteral("defaultgig:");
			case Base::LocalDir         : return QStringLiteral("local:");
			default                     : return QStringLiteral("");
		}
	}

	Base baseLookup(const QString& input)
	{
		if (input.isEmpty()) { return Base::Absolute; };

		for (auto base: relativeBases)
		{
			QString prefix = basePrefix(base);
			if (input.startsWith(prefix)) { return base; }
		}
		return Base::Absolute;
	}




	// Enforce forward slashes for cross-platform compatibility due to limitations of QDir::fromNativeSeparators, see #8107.
	QString serializePath(const QString& input)
	{
		if (input.isEmpty()) { return input; }

		return QString(input).replace("\\", "/");
	}

	QString stripPrefix(const QString& input)
	{
		if (input.isEmpty()) { return input; }

		QString path = serializePath(input);
		return path.mid(basePrefix(baseLookup(path)).length());
	}

	QString cleanName(const QString& input)
	{
		if (input.isEmpty()) { return input; }

		QString path = serializePath(input);
		return stripPrefix(QFileInfo(path).completeBaseName());
	}




	QString oldRelativeUpgrade(const QString& input)
	{
		Base assumedBase = Base::Absolute;

		if (input.isEmpty()) { return basePrefix(assumedBase); }

		QString path = serializePath(input);

		//Check if it's a user sample.
		QString userPath = baseLocation(Base::UserSample) + path;
		QFileInfo userInfo(userPath);
		if (userInfo.exists()) { assumedBase = Base::UserSample; }

		//Check if it's a factory sample
		QString factoryPath = baseLocation(Base::FactorySample) + path;
		QFileInfo factoryInfo(factoryPath);
		if (factoryInfo.exists()) { assumedBase = Base::FactorySample; }

		//Check if it's a VST
		QString vstPath = baseLocation(Base::UserVST) + path;
		QFileInfo vstInfo(vstPath);
		if (vstInfo.exists()) { assumedBase = Base::UserVST; }

		//Assume we've found the correct base location, return the full path
		return basePrefix(assumedBase) + path;
	}

	QString toAbsolute(const QString& input, bool* error /* = nullptr*/)
	{
		if (input.isEmpty()) { return input; }

		QString path = serializePath(input);
		// Note: Paths starting with a colon (:) are always considered absolute, as they denote a QResource.
		QFileInfo inputFileInfo = QFileInfo(path);
		QString resolved;
		if (inputFileInfo.isAbsolute() || path.startsWith(QLatin1Char(':')))
		{
			if (error) { *error = false; }
			resolved = path;
		}
		else
		{
			// Handle old relative paths with no prefix
			QString upgraded = oldRelativeUpgrade(path);
			Base base = baseLookup(upgraded);
			resolved = baseLocation(base, error) + upgraded.remove(0, basePrefix(base).length());
		}

		if (QFileInfo::exists(resolved)) { return resolved; }

		if (const auto moved = locateMovedFile(path); !moved.isEmpty())
		{
			if (error) { *error = false; }
			return moved;
		}
		return resolved;
	}

	QString relativeOrAbsolute(const QString& input, const Base base)
	{
		if (input.isEmpty()) { return input; }

		QString absolutePath = toAbsolute(serializePath(input));
		if (base == Base::Absolute) { return absolutePath; }
		bool error;
		QString relativePath = baseQDir(base, &error).relativeFilePath(absolutePath);
		// Return the relative path if it didn't result in a path starting with ..
		// and the baseQDir was resolved properly
		return (relativePath.startsWith("..") || error)
			? absolutePath
			: relativePath;
	}

	QString toShortestRelative(const QString& input, bool allowLocal /* = false*/)
	{
		if (input.isEmpty()) { return basePrefix(Base::Absolute); }

		QString path = serializePath(input);
		QFileInfo inputFileInfo = QFileInfo(path);
		QString absolutePath = inputFileInfo.isAbsolute() ? path : toAbsolute(path);

		Base shortestBase = Base::Absolute;
		QString shortestPath = relativeOrAbsolute(absolutePath, shortestBase);
		for (auto base: relativeBases)
		{
			// Skip local paths when searching for the shortest relative if those
			// are not allowed for that resource
			if (base == Base::LocalDir && !allowLocal) { continue; }

			QString otherPath = relativeOrAbsolute(absolutePath, base);
			if (otherPath.length() < shortestPath.length())
			{
				shortestBase = base;
				shortestPath = otherPath;
			}
		}
		return basePrefix(shortestBase) + relativeOrAbsolute(absolutePath, shortestBase);
	}

	auto stringToPath(std::string_view path) -> std::filesystem::path
	{
#if defined(LMMS_BUILD_WIN32)
		return toWString(path).get();
#else
		// Assume UTF-8 is the narrow encoding on non-Windows
		return path;
#endif
	}

	auto stringToPath(const QString& path) -> std::filesystem::path
	{
#if defined(LMMS_BUILD_WIN32)
		// Cast QString's UTF-16 encoded unsigned short* to wchar_t*
		return reinterpret_cast<const wchar_t*>(path.utf16());
#else
		// Convert to UTF-8, which we assume is the narrow encoding on non-Windows
		return path.toStdString();
#endif
	}

	auto pathToString(const std::filesystem::path& path) -> std::string
	{
#if defined(LMMS_BUILD_WIN32)
		const auto utf8String = path.u8string();
		return {reinterpret_cast<const char*>(utf8String.c_str()), utf8String.size()};
#else
		// Assume UTF-8 is the narrow encoding on non-Windows
		return path.string();
#endif
	}
} // namespace lmms::PathUtil
