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

#include "nvs-fleet-client.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/platform.h>
#include <util/util.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSysInfo>
#include <QTimer>
#include <QUuid>

#include "egress-controller.hpp"
#include "egress-state.hpp"
#include "nvs-api-endpoints.hpp"
#include "nvs-identity.hpp"

namespace {

constexpr const char *DEVICE_FILE_NAME = "nvs-device.json";
constexpr int DEFAULT_HEARTBEAT_MS = 15000;
constexpr int REQUEST_TIMEOUT_MS = 20000;

/* Backoff after a failed heartbeat so a backend outage does not turn into a
 * request flood from every client at once. */
constexpr int RETRY_HEARTBEAT_MS = 60000;

#if defined(_WIN32)
constexpr const char *CLIENT_PLATFORM = "windows";
#elif defined(__APPLE__)
constexpr const char *CLIENT_PLATFORM = "macos";
#else
constexpr const char *CLIENT_PLATFORM = "linux";
#endif

} // namespace

NvsFleetClient::NvsFleetClient(QString apiBaseUrl, EgressController *controller, NvsIdentity *identity,
			       QObject *parent)
	: QObject(parent),
	  apiBaseUrl_(std::move(apiBaseUrl)),
	  controller_(controller),
	  identity_(identity),
	  networkManager_(new QNetworkAccessManager(this)),
	  heartbeatTimer_(new QTimer(this))
{
	heartbeatTimer_->setInterval(DEFAULT_HEARTBEAT_MS);
	connect(heartbeatTimer_, &QTimer::timeout, this, &NvsFleetClient::SendHeartbeat);

	connect(identity_, &NvsIdentity::Changed, this, &NvsFleetClient::OnIdentityChanged);
}

QString NvsFleetClient::DeviceId()
{
	BPtr<char> configPath = obs_module_get_config_path(obs_current_module(), DEVICE_FILE_NAME);
	const char *raw = configPath;

	if (!raw) {
		return {};
	}

	const QString path = QString::fromUtf8(raw);

	QFile file(path);

	if (file.exists() && file.open(QIODevice::ReadOnly)) {
		const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
		file.close();

		const QString stored = document.object().value("device_id").toString();

		if (!stored.isEmpty()) {
			return stored;
		}
	}

	/* First run on this machine: mint an identifier and keep it. It is opaque
	 * and carries nothing about the user or the hardware. */
	const QString deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces);

	QDir().mkpath(QFileInfo(path).absolutePath());

	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		QJsonObject payload;
		payload.insert("device_id", deviceId);
		file.write(QJsonDocument(payload).toJson(QJsonDocument::Compact));
		file.close();
	} else {
		blog(LOG_WARNING, "[nvs] could not persist the device identifier; it will change on restart");
	}

	return deviceId;
}

void NvsFleetClient::Start()
{
	OnIdentityChanged();
}

void NvsFleetClient::Stop()
{
	heartbeatTimer_->stop();
	clientId_.clear();
	pendingAcks_ = QJsonArray();
	isBlocked_ = false;

	emit Changed();
}

void NvsFleetClient::OnIdentityChanged()
{
	if (apiBaseUrl_.isEmpty()) {
		return;
	}

	if (!identity_->IsSignedIn()) {
		/* Nothing to report as: the fleet is keyed on the account. */
		if (!clientId_.isEmpty() || heartbeatTimer_->isActive()) {
			blog(LOG_INFO, "[nvs] signed out; fleet reporting stopped");
			Stop();
		}

		return;
	}

	if (clientId_.isEmpty() && !registering_) {
		Register();
	}
}

void NvsFleetClient::Register()
{
	const QString deviceId = DeviceId();

	if (deviceId.isEmpty()) {
		return;
	}

	registering_ = true;

	QJsonObject body;
	body.insert("device_id", deviceId);
	body.insert("label", QSysInfo::machineHostName());
	body.insert("app_version", QString::fromUtf8(obs_get_version_string()));
	body.insert("platform", QLatin1String(CLIENT_PLATFORM));

	QNetworkReply *reply = Post(NvsApiEndpoints::Fleet::Register(), body);

	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		reply->deleteLater();
		registering_ = false;

		if (reply->error() != QNetworkReply::NoError) {
			blog(LOG_WARNING, "[nvs] fleet registration failed; retrying later");
			/* Retry on the heartbeat cadence rather than giving up: the
			 * backend may simply be restarting. */
			heartbeatTimer_->start(RETRY_HEARTBEAT_MS);
			return;
		}

		const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
		const QJsonObject data = document.object().value("data").toObject();

		clientId_ = data.value("client_id").toString();
		isBlocked_ = data.value("is_blocked").toBool();

		if (clientId_.isEmpty()) {
			blog(LOG_WARNING, "[nvs] fleet registration returned no client id");
			return;
		}

		const int interval = data.value("heartbeat_interval_sec").toInt(15);
		heartbeatTimer_->setInterval(interval > 0 ? interval * 1000 : DEFAULT_HEARTBEAT_MS);
		heartbeatTimer_->start();

		blog(LOG_INFO, "[nvs] registered with the fleet; heartbeat every %d s", interval);

		emit Changed();

		/* Report immediately so the dashboard does not show a blank row for
		 * a full interval. */
		SendHeartbeat();
	});
}

void NvsFleetClient::SendHeartbeat()
{
	if (clientId_.isEmpty()) {
		if (identity_->IsSignedIn() && !registering_) {
			Register();
		}

		return;
	}

	QJsonObject body;
	body.insert("egress_state", QString::fromUtf8(EgressStateName(controller_->State())));
	body.insert("is_streaming", obs_frontend_streaming_active());

	const QString message = controller_->MessageText();

	if (!message.isEmpty()) {
		body.insert("status_message", message);
	}

	if (!pendingAcks_.isEmpty()) {
		body.insert("acks", pendingAcks_);
		pendingAcks_ = QJsonArray();
	}

	QNetworkReply *reply = Post(NvsApiEndpoints::Fleet::Heartbeat(clientId_), body);

	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		reply->deleteLater();

		if (reply->error() != QNetworkReply::NoError) {
			const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

			/* The server no longer recognises this client — most likely it
			 * was removed. Register again rather than heartbeating into a
			 * void. */
			if (status == 400 || status == 404) {
				blog(LOG_INFO, "[nvs] fleet rejected the heartbeat; re-registering");
				clientId_.clear();
			} else {
				blog(LOG_WARNING, "[nvs] heartbeat failed; will retry");
			}

			return;
		}

		const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
		const QJsonObject data = document.object().value("data").toObject();

		const bool blocked = data.value("is_blocked").toBool();

		if (blocked != isBlocked_) {
			isBlocked_ = blocked;
			blog(LOG_INFO, "[nvs] this client is now %s by an administrator",
			     blocked ? "blocked" : "unblocked");
			emit Changed();
		}

		const int interval = data.value("heartbeat_interval_sec").toInt(0);

		if (interval > 0 && interval * 1000 != heartbeatTimer_->interval()) {
			heartbeatTimer_->setInterval(interval * 1000);
		}

		HandleCommands(data.value("commands").toArray());
	});
}

void NvsFleetClient::HandleCommands(const QJsonArray &commands)
{
	for (const QJsonValue &value : commands) {
		const QJsonObject command = value.toObject();

		ApplyCommand(command.value("command_id").toString(), command.value("command").toString());
	}
}

void NvsFleetClient::ApplyCommand(const QString &commandId, const QString &command)
{
	if (commandId.isEmpty() || command.isEmpty()) {
		return;
	}

	blog(LOG_INFO, "[nvs] applying remote command '%s'", command.toUtf8().constData());

	/* A blocked client still reports, but must not act on control commands. */
	if (isBlocked_ && command != QLatin1String("refresh")) {
		QueueAck(commandId, false, "Client is blocked by an administrator.");
		return;
	}

	if (command == QLatin1String("start_egress")) {
		if (!controller_->StartAvailable()) {
			QueueAck(commandId, false, "Egress cannot be started in its current state.");
			return;
		}

		controller_->Start();
		QueueAck(commandId, true, "Start requested.");
		return;
	}

	if (command == QLatin1String("stop_egress")) {
		if (!controller_->StopAvailable()) {
			QueueAck(commandId, false, "Egress is not running.");
			return;
		}

		controller_->Stop();
		QueueAck(commandId, true, "Stop requested.");
		return;
	}

	if (command == QLatin1String("start_stream")) {
		if (obs_frontend_streaming_active()) {
			QueueAck(commandId, true, "Already streaming.");
			return;
		}

		obs_frontend_streaming_start();
		QueueAck(commandId, true, "Streaming started.");
		return;
	}

	if (command == QLatin1String("stop_stream")) {
		if (!obs_frontend_streaming_active()) {
			QueueAck(commandId, true, "Not streaming.");
			return;
		}

		obs_frontend_streaming_stop();
		QueueAck(commandId, true, "Streaming stopped.");
		return;
	}

	if (command == QLatin1String("show_console")) {
		if (showConsole_) {
			showConsole_();
			QueueAck(commandId, true, "Console shown.");
		} else {
			QueueAck(commandId, false, "No console available.");
		}

		return;
	}

	if (command == QLatin1String("sign_out")) {
		QueueAck(commandId, true, "Signing out.");

		/* Acknowledge first: signing out stops the heartbeat that would carry
		 * the acknowledgement, so send it before tearing the session down. */
		SendHeartbeat();
		identity_->SignOut();
		return;
	}

	if (command == QLatin1String("refresh")) {
		controller_->QueryInitialStatus();
		QueueAck(commandId, true, "Status refreshed.");
		return;
	}

	QueueAck(commandId, false, "Unsupported command.");
}

void NvsFleetClient::QueueAck(const QString &commandId, bool success, const QString &message)
{
	QJsonObject ack;
	ack.insert("command_id", commandId);
	ack.insert("success", success);
	ack.insert("message", message);

	pendingAcks_.append(ack);
}

QNetworkReply *NvsFleetClient::Post(const QString &path, const QJsonObject &body)
{
	QNetworkRequest request{QUrl(NvsApiEndpoints::Url(apiBaseUrl_, path))};
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	request.setTransferTimeout(REQUEST_TIMEOUT_MS);

	const QString token = identity_->AccessToken();

	if (!token.isEmpty()) {
		request.setRawHeader("Authorization", QByteArray("Bearer ") + token.toUtf8());
	}

	return networkManager_->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
}
