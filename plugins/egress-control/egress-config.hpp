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

#include <memory>
#include <string>

#include <QString>

/* Supplies the application access token sent to the backend.
 *
 * This is the extension point for a future authentication provider. Nothing in
 * this plugin performs a login of any kind; an implementation of this interface
 * is expected to hand back an already-acquired token.
 */
class AccessTokenProvider {
public:
	virtual ~AccessTokenProvider() = default;
	virtual std::string GetAccessToken() const = 0;
};

/* Development-time provider that reads the token from an environment variable.
 *
 * Replace this with a real provider before shipping. The token is never written
 * to the OBS log by this plugin.
 */
class EnvironmentAccessTokenProvider : public AccessTokenProvider {
public:
	explicit EnvironmentAccessTokenProvider(std::string variableName);
	std::string GetAccessToken() const override;

private:
	std::string variableName_;
};

/* Everything the plugin needs to talk to the backend.
 *
 * No production URL or secret is compiled into the plugin: values come from an
 * optional JSON file in the module config directory, and may be overridden by
 * environment variables for development.
 */
struct EgressConfig {
	QString baseUrl;
	QString startPath = "/api/v1/egress/start";
	QString stopPath = "/api/v1/egress/stop";
	QString statusPath = "/api/v1/egress/status";

	int requestTimeoutMs = 15000;

	/* Query the backend once on startup so an Egress session that is already
	 * running for this user is reflected in the dock. */
	bool queryStatusOnStartup = true;

	/* Opt-in only. When false (the default) closing OBS leaves a running
	 * Egress session untouched; the server remains the source of truth. */
	bool stopEgressOnExit = false;

	std::shared_ptr<AccessTokenProvider> tokenProvider;

	/* Reads the config file, then applies environment overrides. Always
	 * returns a usable object; call IsConfigured() to test it. */
	static EgressConfig Load();

	bool IsConfigured() const { return !baseUrl.isEmpty(); }

	/* Full URL for one of the API paths above. */
	QString UrlFor(const QString &path) const;

	/* True for https, or for http on a loopback host (development only).
	 * Plain http to a remote host is refused rather than sent. */
	static bool IsTransportSecure(const QString &url);
};
