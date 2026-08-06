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

#pragma once

#include <QJsonObject>
#include <QString>

/* At-rest storage for NVS sign-in credentials.
 *
 * On Windows the payload is sealed with DPAPI under the current user account,
 * so the file is unreadable by other users and unusable if copied to another
 * machine. Where DPAPI is unavailable the store refuses to persist rather than
 * writing tokens in clear text — the user simply signs in again each launch.
 */
class NvsCredentialStore {
public:
	static bool Save(const QJsonObject &credentials);
	static QJsonObject Load();
	static void Clear();

	/* False when no platform sealing is available, so callers can explain why
	 * the session will not be remembered. */
	static bool IsEncryptionAvailable();

private:
	static QString FilePath();
};
