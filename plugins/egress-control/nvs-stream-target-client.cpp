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

#include "nvs-stream-target-client.hpp"

#include <obs-module.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include "nvs-api-endpoints.hpp"
#include "nvs-identity.hpp"

namespace {

constexpr int REQUEST_TIMEOUT_MS = 20000;

/* Reads the payload out of either envelope the API uses: a single object under
 * "data", or a bare list. */
QJsonArray ItemsOf(const QJsonObject &root)
{
	if (root.value("data").isArray()) {
		return root.value("data").toArray();
	}
	if (root.value("items").isArray()) {
		return root.value("items").toArray();
	}

	return {};
}

} // namespace

NvsStreamTargetClient::NvsStreamTargetClient(QString apiBaseUrl, NvsIdentity *identity, QObject *parent)
	: QObject(parent),
	  apiBaseUrl_(std::move(apiBaseUrl)),
	  identity_(identity),
	  networkManager_(new QNetworkAccessManager(this))
{
}

QNetworkReply *NvsStreamTargetClient::Send(const QString &path, const QByteArray &verb, const QByteArray &body)
{
	QNetworkRequest request{QUrl(NvsApiEndpoints::Url(apiBaseUrl_, path))};
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	request.setTransferTimeout(REQUEST_TIMEOUT_MS);

	const QString token = identity_ ? identity_->AccessToken() : QString();

	if (!token.isEmpty()) {
		request.setRawHeader("Authorization", QByteArray("Bearer ") + token.toUtf8());
	}

	return networkManager_->sendCustomRequest(request, verb, body);
}

void NvsStreamTargetClient::FetchTargets(const QString &roomId)
{
	if (roomId.isEmpty()) {
		return;
	}

	if (!identity_ || !identity_->IsSignedIn()) {
		emit Failed(QString::fromUtf8(obs_module_text("Room.SignInRequired")));
		return;
	}

	QNetworkReply *reply = Send(NvsApiEndpoints::StreamTargets::ForRoom(roomId), "GET");

	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		reply->deleteLater();

		if (reply->error() != QNetworkReply::NoError) {
			emit Failed(QString::fromUtf8(obs_module_text("Targets.LoadFailed")));
			return;
		}

		const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());

		if (!document.isObject()) {
			emit Failed(QString::fromUtf8(obs_module_text("Error.MalformedResponse")));
			return;
		}

		QList<NvsStreamTarget> targets;

		for (const QJsonValue &value : ItemsOf(document.object())) {
			const QJsonObject entry = value.toObject();

			NvsStreamTarget target;
			target.id = entry.value("id").toString();
			target.platformType = entry.value("platform_type").toString();
			target.name = entry.value("target_name").toString();
			target.url = entry.value("rtmp_url").toString();
			target.enabled = entry.value("is_enabled").toBool(true);

			if (!target.id.isEmpty()) {
				targets.append(target);
			}
		}

		blog(LOG_INFO, "[nvs] room has %lld stream target(s)", static_cast<long long>(targets.size()));

		emit TargetsFetched(targets);
	});
}

void NvsStreamTargetClient::SaveTarget(const QString &roomId, const NvsStreamTarget &target,
				       const QString &streamKey)
{
	if (!identity_ || !identity_->IsSignedIn()) {
		emit Failed(QString::fromUtf8(obs_module_text("Room.SignInRequired")));
		return;
	}

	QJsonObject body;
	body.insert("platform_type", target.platformType);
	body.insert("target_name", target.name);

	/* The operator supplies the live URL per destination, which is what allows
	 * several YouTube or Facebook endpoints for one room. */
	body.insert("rtmp_url", target.url);
	body.insert("stream_key", streamKey);
	body.insert("is_enabled", target.enabled);

	const bool creating = target.id.isEmpty();

	const QString path = creating ? NvsApiEndpoints::StreamTargets::ForRoom(roomId)
				      : NvsApiEndpoints::StreamTargets::ById(target.id);

	QNetworkReply *reply =
		Send(path, creating ? QByteArray("POST") : QByteArray("PUT"),
		     QJsonDocument(body).toJson(QJsonDocument::Compact));

	const QString platform = target.platformType;

	connect(reply, &QNetworkReply::finished, this, [this, reply, platform]() {
		reply->deleteLater();

		if (reply->error() != QNetworkReply::NoError) {
			emit Failed(QString::fromUtf8(obs_module_text("Targets.SaveFailed")).arg(platform));
			return;
		}

		/* Platform only: the body carried a stream key. */
		blog(LOG_INFO, "[nvs] saved stream target for %s", platform.toUtf8().constData());

		emit TargetSaved(platform);
	});
}

void NvsStreamTargetClient::DeleteTarget(const QString &targetId)
{
	if (targetId.isEmpty()) {
		return;
	}

	QNetworkReply *reply = Send(NvsApiEndpoints::StreamTargets::ById(targetId), "DELETE");

	connect(reply, &QNetworkReply::finished, this, [this, reply, targetId]() {
		reply->deleteLater();

		if (reply->error() != QNetworkReply::NoError) {
			emit Failed(QString::fromUtf8(obs_module_text("Targets.DeleteFailed")));
			return;
		}

		blog(LOG_INFO, "[nvs] removed a stream target");
		emit TargetDeleted(targetId);
	});
}

void NvsStreamTargetClient::StartTarget(const QString &targetId)
{
	if (targetId.isEmpty()) {
		return;
	}

	QNetworkReply *reply = Send(NvsApiEndpoints::StreamTargets::Start(targetId), "POST", "{}");

	connect(reply, &QNetworkReply::finished, this, [this, reply, targetId]() {
		reply->deleteLater();

		if (reply->error() != QNetworkReply::NoError) {
			emit Failed(QString::fromUtf8(obs_module_text("Targets.StartFailed")));
			return;
		}

		blog(LOG_INFO, "[nvs] started stream target");
		emit TargetStarted(targetId);
	});
}

void NvsStreamTargetClient::StopTarget(const QString &targetId)
{
	if (targetId.isEmpty()) {
		return;
	}

	QNetworkReply *reply = Send(NvsApiEndpoints::StreamTargets::Stop(targetId), "POST", "{}");

	connect(reply, &QNetworkReply::finished, this, [this, reply, targetId]() {
		reply->deleteLater();

		if (reply->error() != QNetworkReply::NoError) {
			emit Failed(QString::fromUtf8(obs_module_text("Targets.StopFailed")));
			return;
		}

		blog(LOG_INFO, "[nvs] stopped stream target");
		emit TargetStopped(targetId);
	});
}
