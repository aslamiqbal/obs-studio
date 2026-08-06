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
 * reshapes a route, this is the only file that changes, and the compiler finds
 * every caller. It mirrors `apiEndpoints` in the web client so the two stay
 * recognisably the same shape.
 *
 * Paths only: the base URL lives in EgressConfig, because it is deployment
 * configuration rather than a property of the API.
 *
 * ---------------------------------------------------------------------------
 * A note on "egress", because the naming is misleading.
 *
 * There is no /api/v1/egress/* on this backend and there never was; those paths
 * came from the original feature brief, not from the server. A broadcast
 * destination is a STREAM TARGET that belongs to a room, and starting or
 * stopping a broadcast is per-target:
 *
 *     POST /api/stream-targets/{id}/start
 *     POST /api/stream-targets/{id}/stop
 *
 * So "destinations" are already first-class server-side — a YouTube or Facebook
 * entry is a stream target row, with its token stored as the (encrypted) stream
 * key. Nothing needs a `destinations` array bolted onto a start call.
 * ---------------------------------------------------------------------------
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
		static QString Me() { return "/api/auth/me"; }
	};

	/* ---- Rooms ---- */
	struct Rooms {
		/* `mine` limits the list to rooms this account may operate. */
		static QString List(int page = 1, int pageSize = 20, bool mineOnly = true)
		{
			return QStringLiteral("/api/rooms?page=%1&page_size=%2%3")
				.arg(page)
				.arg(pageSize)
				.arg(mineOnly ? "&mine=true" : "");
		}

		static QString ById(const QString &roomId) { return "/api/rooms/" + roomId; }

		/* Mints the browser-source URL that renders this room. The credential
		 * arrives in the URL fragment, so it never reaches server logs. POST
		 * because each call issues a fresh one. */
		static QString ObsEgressUrl(const QString &roomId)
		{
			return "/api/rooms/" + roomId + "/obs-egress-url";
		}
	};

	/* ---- Broadcast destinations ----
	 *
	 * This is what the console's Destinations section maps onto: one target
	 * per platform, each with its own RTMP URL and key. */
	struct StreamTargets {
		static QString ForRoom(const QString &roomId)
		{
			return "/api/rooms/" + roomId + "/stream-targets";
		}

		/* Creates targets from already-linked platform accounts, so an
		 * operator with a connected YouTube account needs no manual key. */
		static QString FromConnections(const QString &roomId)
		{
			return "/api/rooms/" + roomId + "/stream-targets/from-connections";
		}

		static QString ById(const QString &targetId) { return "/api/stream-targets/" + targetId; }

		/* The real "start egress": begins pushing this destination. */
		static QString Start(const QString &targetId)
		{
			return "/api/stream-targets/" + targetId + "/start";
		}

		static QString Stop(const QString &targetId)
		{
			return "/api/stream-targets/" + targetId + "/stop";
		}

		static QString Status(const QString &targetId)
		{
			return "/api/stream-targets/" + targetId + "/status";
		}
	};

	/* ---- Renderer lease ----
	 *
	 * Ownership fencing for desktop rendering, shared with the Electron
	 * client. Every privileged call carries {lease_id, session_id,
	 * fence_token} as proof. */
	struct RendererLease {
		static QString Status(const QString &roomId)
		{
			return "/api/rooms/" + roomId + "/renderer-lease";
		}
		static QString Acquire(const QString &roomId)
		{
			return "/api/rooms/" + roomId + "/renderer-lease/acquire";
		}
		static QString Heartbeat(const QString &roomId)
		{
			return "/api/rooms/" + roomId + "/renderer-lease/heartbeat";
		}
		static QString Release(const QString &roomId)
		{
			return "/api/rooms/" + roomId + "/renderer-lease/release";
		}
		static QString Takeover(const QString &roomId)
		{
			return "/api/rooms/" + roomId + "/renderer-lease/takeover";
		}
		static QString SessionCapability(const QString &roomId)
		{
			return "/api/rooms/" + roomId + "/renderer-lease/renderer-session-capability";
		}
		/* Returns live RTMP publish URLs. Never log or persist the response. */
		static QString PublishManifest(const QString &roomId)
		{
			return "/api/rooms/" + roomId + "/renderer-lease/desktop-publish-manifest";
		}
	};

	/* ---- Legacy egress paths ----
	 *
	 * NOT IMPLEMENTED by this backend. They are the paths from the original
	 * brief and are kept only so the existing EgressController keeps building
	 * while its Start/Stop are re-pointed at StreamTargets above.
	 *
	 * Calling these against api.nadavox.com returns 404. Anything relying on
	 * them is unfinished, not merely unconfigured. */
	struct LegacyEgress {
		static QString Start() { return "/api/v1/egress/start"; }
		static QString Stop() { return "/api/v1/egress/stop"; }
		static QString Status() { return "/api/v1/egress/status"; }
	};

	/* ---- Desktop fleet: presence and remote control ---- */
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
