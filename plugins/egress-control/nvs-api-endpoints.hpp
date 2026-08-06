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

#include <QString>

/* Every backend path NVS calls, in one place.
 *
 * No other file builds a URL by concatenating strings. When the backend
 * reshapes a route, this is the only file that changes — and the compiler finds
 * every caller. It mirrors `apiEndpoints` in the web client so the two stay
 * recognisably the same shape.
 *
 * Paths only: the base URL lives in EgressConfig, because it is deployment
 * configuration rather than a property of the API.
 */
class NvsApiEndpoints {
public:
	/* Default deployment. Overridable via config file or environment so a
	 * developer can point a build at a local backend. */
	static constexpr const char *DefaultBaseUrl = "https://api.nadavox.com";

	/* ---- Authentication (RFC 8252 native app flow) ---- */
	struct Auth {
		static QString NativeStart() { return "/api/auth/native/start"; }
		static QString NativeToken() { return "/api/auth/native/token"; }
		static QString NativeStatus(const QString &sessionId)
		{
			return "/api/auth/native/status?session_id=" + sessionId;
		}
		static QString Refresh() { return "/api/auth/refresh"; }
	};

	/* ---- Egress service control ---- */
	struct Egress {
		static QString Start() { return "/api/v1/egress/start"; }
		static QString Stop() { return "/api/v1/egress/stop"; }
		static QString Status() { return "/api/v1/egress/status"; }
	};

	/* ---- Desktop fleet: presence, remote control ---- */
	struct Fleet {
		static QString Register() { return "/api/nvs/clients/register"; }
		static QString Heartbeat(const QString &clientId)
		{
			return "/api/nvs/clients/" + clientId + "/heartbeat";
		}
	};

	/* Joins the base URL and a path without producing a doubled separator. */
	static QString Url(const QString &baseUrl, const QString &path)
	{
		QString base = baseUrl;

		while (base.endsWith('/')) {
			base.chop(1);
		}

		return path.startsWith('/') ? base + path : base + "/" + path;
	}
};
