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

#include "egress-api-client.hpp"

#include <obs-module.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QStringList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace {

constexpr const char *CLIENT_NAME = "obs-studio";

#if defined(_WIN32)
constexpr const char *CLIENT_PLATFORM = "windows";
#elif defined(__APPLE__)
constexpr const char *CLIENT_PLATFORM = "macos";
#else
constexpr const char *CLIENT_PLATFORM = "linux";
#endif

const char *RequestName(int kind)
{
	switch (kind) {
	case 1:
		return "start";
	case 2:
		return "stop";
	case 3:
		return "status";
	}

	return "unknown";
}

QString ReadJsonString(const QJsonObject &object, const char *key)
{
	const QJsonValue value = object.value(QLatin1String(key));

	return value.isString() ? value.toString() : QString();
}

} // namespace

EgressApiClient::EgressApiClient(EgressConfig config, QObject *parent)
	: QObject(parent),
	  config_(std::move(config)),
	  networkManager_(new QNetworkAccessManager(this))
{
}

EgressApiClient::~EgressApiClient()
{
	AbortPending();
}

void EgressApiClient::RequestStart()
{
	QJsonObject body;
	body.insert("client", QLatin1String(CLIENT_NAME));
	body.insert("platform", QLatin1String(CLIENT_PLATFORM));

	if (!destinations_.isEmpty()) {
		QJsonArray destinations;

		for (const EgressDestination &destination : destinations_) {
			QJsonObject entry;
			entry.insert("platform", destination.platform);

			/* An empty token is omitted rather than sent blank: the
			 * backend may already hold a stored credential for the
			 * platform, and a blank value would look like an override. */
			if (!destination.token.isEmpty()) {
				entry.insert("token", destination.token);
			}

			destinations.append(entry);
		}

		body.insert("destinations", destinations);

		/* Platform names only — the tokens are credentials. */
		QStringList names;
		for (const EgressDestination &destination : destinations_) {
			names << destination.platform;
		}

		blog(LOG_INFO, "[egress-control] start request targets: %s",
		     names.join(QStringLiteral(", ")).toUtf8().constData());
	}

	Send(RequestKind::Start, config_.startPath, body, true);
}

void EgressApiClient::RequestStop(const QString &serviceId)
{
	QJsonObject body;
	if (!serviceId.isEmpty()) {
		body.insert("serviceId", serviceId);
	}

	Send(RequestKind::Stop, config_.stopPath, body, true);
}

void EgressApiClient::RequestStatus()
{
	Send(RequestKind::Status, config_.statusPath, QJsonObject(), false);
}

void EgressApiClient::AbortPending()
{
	if (pendingReply_.isNull()) {
		return;
	}

	QNetworkReply *reply = pendingReply_;
	pendingReply_.clear();

	/* Drop the completion handler before aborting so no signal is emitted for
	 * a request the caller has already given up on. */
	reply->disconnect(this);
	reply->abort();
	reply->deleteLater();
}

void EgressApiClient::Send(RequestKind kind, const QString &path, const QJsonObject &body, bool isPost)
{
	if (!config_.IsConfigured()) {
		Complete(kind, MakeFailure(EgressApiError::NotConfigured));
		return;
	}

	const QString url = config_.UrlFor(path);

	if (!EgressConfig::IsTransportSecure(url)) {
		blog(LOG_WARNING, "[egress-control] refusing %s request over an insecure transport",
		     RequestName(static_cast<int>(kind)));
		Complete(kind, MakeFailure(EgressApiError::InsecureUrl));
		return;
	}

	/* One request at a time: this is what makes a double-click on Start or Stop
	 * harmless, since the second click cannot reach the network. */
	if (!pendingReply_.isNull()) {
		blog(LOG_WARNING, "[egress-control] %s request ignored, another request is already in flight",
		     RequestName(static_cast<int>(kind)));
		return;
	}

	QNetworkRequest request{QUrl(url)};
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	request.setTransferTimeout(config_.requestTimeoutMs);
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

	if (config_.tokenProvider) {
		const std::string token = config_.tokenProvider->GetAccessToken();

		if (!token.empty()) {
			const QByteArray header = QByteArray("Bearer ") + QByteArray::fromStdString(token);
			request.setRawHeader("Authorization", header);
		}
	}

	blog(LOG_INFO, "[egress-control] sending %s request", RequestName(static_cast<int>(kind)));

	QNetworkReply *reply = nullptr;

	if (isPost) {
		reply = networkManager_->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
	} else {
		reply = networkManager_->get(request);
	}

	pendingReply_ = reply;

	connect(reply, &QNetworkReply::finished, this, [this, kind, reply]() {
		/* AbortPending() may have already claimed this reply. */
		if (pendingReply_ != reply) {
			reply->deleteLater();
			return;
		}

		pendingReply_.clear();

		const EgressApiResult result = BuildResult(reply);
		reply->deleteLater();

		Complete(kind, result);
	});
}

void EgressApiClient::Complete(RequestKind kind, const EgressApiResult &result)
{
	if (result.success) {
		blog(LOG_INFO, "[egress-control] %s request succeeded", RequestName(static_cast<int>(kind)));
	} else if (result.httpStatus != 0) {
		blog(LOG_WARNING, "[egress-control] %s request failed with HTTP %d",
		     RequestName(static_cast<int>(kind)), result.httpStatus);
	} else {
		blog(LOG_WARNING, "[egress-control] %s request failed before a response was received",
		     RequestName(static_cast<int>(kind)));
	}

	switch (kind) {
	case RequestKind::Start:
		emit StartFinished(result);
		break;
	case RequestKind::Stop:
		emit StopFinished(result);
		break;
	case RequestKind::Status:
		emit StatusFinished(result);
		break;
	}
}

EgressApiResult EgressApiClient::BuildResult(QNetworkReply *reply) const
{
	EgressApiResult result;

	const QVariant statusAttribute = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
	result.httpStatus = statusAttribute.isValid() ? statusAttribute.toInt() : 0;

	const QNetworkReply::NetworkError networkError = reply->error();

	if (networkError != QNetworkReply::NoError) {
		switch (networkError) {
		case QNetworkReply::OperationCanceledError:
		case QNetworkReply::TimeoutError:
			result.error = EgressApiError::Timeout;
			/* The request was already on the wire, so the backend may have
			 * acted on it even though no response arrived. */
			result.outcomeUnknown = true;
			break;
		case QNetworkReply::ConnectionRefusedError:
		case QNetworkReply::HostNotFoundError:
		case QNetworkReply::RemoteHostClosedError:
		case QNetworkReply::NetworkSessionFailedError:
		case QNetworkReply::UnknownNetworkError:
			result.error = EgressApiError::ConnectionFailed;
			break;
		case QNetworkReply::AuthenticationRequiredError:
		case QNetworkReply::ContentAccessDenied:
		case QNetworkReply::ContentOperationNotPermittedError:
			result.error = EgressApiError::Unauthorized;
			break;
		case QNetworkReply::ContentNotFoundError:
			result.error = EgressApiError::NotFound;
			break;
		default:
			if (result.httpStatus == 401 || result.httpStatus == 403) {
				result.error = EgressApiError::Unauthorized;
			} else if (result.httpStatus == 404) {
				result.error = EgressApiError::NotFound;
			} else if (result.httpStatus >= 500) {
				result.error = EgressApiError::ServerError;
				result.outcomeUnknown = true;
			} else {
				result.error = EgressApiError::RequestFailed;
			}
			break;
		}

		return result;
	}

	const QByteArray payload = reply->readAll();

	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);

	if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
		/* The response body is deliberately not logged: it may carry tokens or
		 * other sensitive backend detail. */
		blog(LOG_WARNING, "[egress-control] backend returned a response that could not be parsed as JSON");
		result.error = EgressApiError::MalformedResponse;
		result.outcomeUnknown = true;
		return result;
	}

	const QJsonObject object = document.object();

	result.serviceId = ReadJsonString(object, "serviceId");
	result.egressId = ReadJsonString(object, "egressId");
	result.status = ReadJsonString(object, "status");

	/* "success" is optional: a 2xx response without the field is treated as a
	 * success, while an explicit false is honoured. */
	const QJsonValue successValue = object.value(QLatin1String("success"));
	const bool reportedSuccess = successValue.isBool() ? successValue.toBool() : true;

	if (!reportedSuccess) {
		result.error = EgressApiError::RequestFailed;
		return result;
	}

	result.success = true;
	result.error = EgressApiError::None;

	return result;
}

EgressApiResult EgressApiClient::MakeFailure(EgressApiError error) const
{
	EgressApiResult result;
	result.success = false;
	result.error = error;

	return result;
}
