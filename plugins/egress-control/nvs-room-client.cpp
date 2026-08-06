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

#include "nvs-room-client.hpp"

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

/* The server puts the capability in the fragment as
 * "#signaling_token=<urlencoded>", deliberately, so it never reaches an access
 * log. Splitting on the same key keeps the two halves interchangeable with what
 * the API mints. */
constexpr const char *TOKEN_FRAGMENT_KEY = "signaling_token=";

} // namespace

NvsRoomClient::NvsRoomClient(QString apiBaseUrl, NvsIdentity *identity, QObject *parent)
	: QObject(parent),
	  apiBaseUrl_(std::move(apiBaseUrl)),
	  identity_(identity),
	  networkManager_(new QNetworkAccessManager(this))
{
}

QString NvsRoomClient::AddressOf(const QString &url)
{
	const int hash = url.indexOf('#');

	return hash < 0 ? url : url.left(hash);
}

QString NvsRoomClient::TokenOf(const QString &url)
{
	const int hash = url.indexOf('#');

	if (hash < 0) {
		return {};
	}

	const QString fragment = url.mid(hash + 1);
	const QString key = QLatin1String(TOKEN_FRAGMENT_KEY);

	if (!fragment.startsWith(key)) {
		/* An unrecognised fragment is still the credential half; hand it back
		 * whole rather than silently dropping it. */
		return QUrl::fromPercentEncoding(fragment.toUtf8());
	}

	return QUrl::fromPercentEncoding(fragment.mid(key.size()).toUtf8());
}

QString NvsRoomClient::Compose(const QString &address, const QString &token)
{
	const QString trimmedAddress = address.trimmed();

	if (trimmedAddress.isEmpty() || token.trimmed().isEmpty()) {
		return trimmedAddress;
	}

	return trimmedAddress + "#" + QLatin1String(TOKEN_FRAGMENT_KEY) +
	       QString::fromUtf8(QUrl::toPercentEncoding(token.trimmed()));
}

QNetworkReply *NvsRoomClient::Send(const QString &path, bool isPost)
{
	QNetworkRequest request{QUrl(NvsApiEndpoints::Url(apiBaseUrl_, path))};
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	request.setTransferTimeout(REQUEST_TIMEOUT_MS);

	/* The minted URL is a credential, so the response must not be cached. */
	request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);

	const QString token = identity_ ? identity_->AccessToken() : QString();

	if (!token.isEmpty()) {
		request.setRawHeader("Authorization", QByteArray("Bearer ") + token.toUtf8());
	}

	return isPost ? networkManager_->post(request, QByteArray("{}")) : networkManager_->get(request);
}

void NvsRoomClient::FetchRooms()
{
	if (!identity_ || !identity_->IsSignedIn()) {
		emit Failed(QString::fromUtf8(obs_module_text("Room.SignInRequired")));
		return;
	}

	QNetworkReply *reply = Send(NvsApiEndpoints::Rooms::List(1, 50, true), false);

	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		reply->deleteLater();

		if (reply->error() != QNetworkReply::NoError) {
			emit Failed(QString::fromUtf8(obs_module_text("Room.LoadFailed")));
			return;
		}

		const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());

		if (!document.isObject()) {
			emit Failed(QString::fromUtf8(obs_module_text("Error.MalformedResponse")));
			return;
		}

		/* The rooms list is a paged envelope, so the array is at the top level
		 * rather than under "data" like the single-object responses. */
		const QJsonObject root = document.object();
		const QJsonArray items = root.value("items").isArray() ? root.value("items").toArray()
								      : root.value("data").toArray();

		QList<NvsRoomInfo> rooms;

		for (const QJsonValue &value : items) {
			const QJsonObject entry = value.toObject();

			NvsRoomInfo room;
			room.id = entry.value("id").toString();
			room.code = entry.value("room_code").toString();
			room.name = entry.value("name").toString();

			if (room.name.isEmpty()) {
				room.name = room.code.isEmpty() ? room.id : room.code;
			}

			if (!room.id.isEmpty()) {
				rooms.append(room);
			}
		}

		blog(LOG_INFO, "[nvs] loaded %lld room(s)", static_cast<long long>(rooms.size()));

		emit RoomsFetched(rooms);
	});
}

void NvsRoomClient::FetchJoinUrl(const QString &roomId)
{
	if (roomId.isEmpty()) {
		return;
	}

	if (!identity_ || !identity_->IsSignedIn()) {
		emit Failed(QString::fromUtf8(obs_module_text("Room.SignInRequired")));
		return;
	}

	QNetworkReply *reply = Send(NvsApiEndpoints::Rooms::ObsEgressUrl(roomId), true);

	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		reply->deleteLater();

		if (reply->error() != QNetworkReply::NoError) {
			emit Failed(QString::fromUtf8(obs_module_text("Room.JoinUrlFailed")));
			return;
		}

		const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());

		if (!document.isObject()) {
			emit Failed(QString::fromUtf8(obs_module_text("Error.MalformedResponse")));
			return;
		}

		const QString url = document.object().value("data").toObject().value("url").toString();

		if (url.isEmpty()) {
			emit Failed(QString::fromUtf8(obs_module_text("Room.JoinUrlFailed")));
			return;
		}

		/* Only the address half is logged; the fragment is the credential. */
		blog(LOG_INFO, "[nvs] minted a room join address");

		emit JoinUrlFetched(AddressOf(url), TokenOf(url));
	});
}
