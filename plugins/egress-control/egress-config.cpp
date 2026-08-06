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

#include "egress-config.hpp"

#include "nvs-api-endpoints.hpp"

#include <obs-module.h>
#include <obs.hpp>
#include <util/platform.h>
#include <util/util.hpp>

#include <QUrl>

namespace {

constexpr const char *CONFIG_FILE_NAME = "egress-control.json";
constexpr const char *ENV_BASE_URL = "EGRESS_CONTROL_BASE_URL";
constexpr const char *ENV_ACCESS_TOKEN = "EGRESS_CONTROL_ACCESS_TOKEN";

QString ReadString(obs_data_t *data, const char *key, const QString &fallback)
{
	if (!obs_data_has_user_value(data, key)) {
		return fallback;
	}

	const char *value = obs_data_get_string(data, key);
	if (!value || !*value) {
		return fallback;
	}

	return QString::fromUtf8(value);
}

/* Trailing slashes on the base URL would produce doubled separators once a
 * path is appended, so they are trimmed on load. */
QString NormalizeBaseUrl(QString url)
{
	while (url.endsWith('/')) {
		url.chop(1);
	}

	return url;
}

} // namespace

EnvironmentAccessTokenProvider::EnvironmentAccessTokenProvider(std::string variableName)
	: variableName_(std::move(variableName))
{
}

std::string EnvironmentAccessTokenProvider::GetAccessToken() const
{
	const char *value = getenv(variableName_.c_str());

	return value ? std::string(value) : std::string();
}

EgressConfig EgressConfig::Load()
{
	EgressConfig config;

	config.baseUrl = QString::fromUtf8(NvsApiEndpoints::DefaultBaseUrl);
	config.startPath = NvsApiEndpoints::Egress::Start();
	config.stopPath = NvsApiEndpoints::Egress::Stop();
	config.statusPath = NvsApiEndpoints::Egress::Status();

	BPtr<char> configPath = obs_module_get_config_path(obs_current_module(), CONFIG_FILE_NAME);

	if (configPath && os_file_exists(configPath)) {
		BPtr<char> jsonData = os_quick_read_utf8_file(configPath);

		if (jsonData) {
			OBSDataAutoRelease data = obs_data_create_from_json(jsonData);

			if (data) {
				config.baseUrl = ReadString(data, "base_url", config.baseUrl);
				config.startPath = ReadString(data, "start_path", config.startPath);
				config.stopPath = ReadString(data, "stop_path", config.stopPath);
				config.statusPath = ReadString(data, "status_path", config.statusPath);

				if (obs_data_has_user_value(data, "request_timeout_ms")) {
					const int timeout =
						static_cast<int>(obs_data_get_int(data, "request_timeout_ms"));
					if (timeout > 0) {
						config.requestTimeoutMs = timeout;
					}
				}

				if (obs_data_has_user_value(data, "query_status_on_startup")) {
					config.queryStatusOnStartup =
						obs_data_get_bool(data, "query_status_on_startup");
				}

				if (obs_data_has_user_value(data, "stop_egress_on_exit")) {
					config.stopEgressOnExit = obs_data_get_bool(data, "stop_egress_on_exit");
				}
			} else {
				blog(LOG_WARNING, "[egress-control] config file is not valid JSON, using defaults");
			}
		}
	}

	/* Environment overrides take precedence so a developer can point a build at
	 * a local backend without editing the config file. */
	const char *baseUrlOverride = getenv(ENV_BASE_URL);
	if (baseUrlOverride && *baseUrlOverride) {
		config.baseUrl = QString::fromUtf8(baseUrlOverride);
	}

	config.baseUrl = NormalizeBaseUrl(config.baseUrl.trimmed());

	config.tokenProvider = std::make_shared<EnvironmentAccessTokenProvider>(ENV_ACCESS_TOKEN);

	if (config.IsConfigured()) {
		/* The host is logged to make misconfiguration diagnosable; the token
		 * and full request bodies are never logged. */
		blog(LOG_INFO, "[egress-control] backend configured: %s",
		     QUrl(config.baseUrl).host().toUtf8().constData());
	} else {
		blog(LOG_INFO, "[egress-control] no backend URL configured");
	}

	return config;
}

QString EgressConfig::UrlFor(const QString &path) const
{
	return NvsApiEndpoints::Url(baseUrl, path);
}

bool EgressConfig::IsTransportSecure(const QString &url)
{
	const QUrl parsed(url);

	if (parsed.scheme().compare("https", Qt::CaseInsensitive) == 0) {
		return true;
	}

	if (parsed.scheme().compare("http", Qt::CaseInsensitive) != 0) {
		return false;
	}

	const QString host = parsed.host();

	return host == "localhost" || host == "127.0.0.1" || host == "::1";
}
