/******************************************************************************
    Copyright (C) 2026 by Aslam Iqbal

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "nvs-startup.hpp"

#include <obs-module.h>

#ifdef _WIN32
#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QString>

namespace {

constexpr const char *RUN_KEY = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const char *VALUE_NAME = "NVS";

/* Quoted so a path containing spaces is parsed as one argument.
 *
 * --minimize-to-tray is what makes an automatic launch unobtrusive: the
 * frontend keeps the main window hidden and NVS lives in the notification area
 * until the user opens it. */
QString LaunchCommand()
{
	return QLatin1Char('"') + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) +
	       QLatin1String("\" --minimize-to-tray");
}

} // namespace
#endif

bool NvsStartup::IsSupported()
{
#ifdef _WIN32
	return true;
#else
	return false;
#endif
}

bool NvsStartup::IsEnabled()
{
#ifdef _WIN32
	QSettings run(QLatin1String(RUN_KEY), QSettings::NativeFormat);

	const QString stored = run.value(QLatin1String(VALUE_NAME)).toString();

	if (stored.isEmpty()) {
		return false;
	}

	/* A stale entry pointing at a different build should not read as enabled:
	 * the checkbox must describe this executable. Compared on the quoted
	 * executable path alone so the trailing arguments do not matter. */
	const QString executable =
		QLatin1Char('"') + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) +
		QLatin1Char('"');

	return stored.trimmed().startsWith(executable, Qt::CaseInsensitive);
#else
	return false;
#endif
}

bool NvsStartup::SetEnabled(bool enabled)
{
#ifdef _WIN32
	QSettings run(QLatin1String(RUN_KEY), QSettings::NativeFormat);

	if (enabled) {
		run.setValue(QLatin1String(VALUE_NAME), LaunchCommand());
	} else {
		run.remove(QLatin1String(VALUE_NAME));
	}

	run.sync();

	if (run.status() != QSettings::NoError) {
		blog(LOG_WARNING, "[nvs] could not update the Windows startup entry");
		return false;
	}

	blog(LOG_INFO, "[nvs] Windows startup entry %s", enabled ? "enabled" : "removed");

	return IsEnabled() == enabled;
#else
	UNUSED_PARAMETER(enabled);
	return false;
#endif
}
