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
#include <QObject>
#include <QPointer>
#include <QString>

#include "egress-config.hpp"

class QNetworkAccessManager;
class QNetworkReply;

/* Failure categories the UI can turn into a localized message.
 *
 * Values start at 1 so a zero-initialized error is never read as "no error".
 */
enum class EgressApiError {
	None = 1,
	NotConfigured = 2,
	InsecureUrl = 3,
	ConnectionFailed = 4,
	Timeout = 5,
	Unauthorized = 6,
	NotFound = 7,
	ServerError = 8,
	MalformedResponse = 9,
	RequestFailed = 10,
};

/* A platform the backend should broadcast to, and the credential it needs.
 *
 * The token is a live stream credential: it is sent to the backend and never
 * written to the log. */
struct EgressDestination {
	QString platform;
	QString token;
};

/* Outcome of a single backend call. Carries no raw response body so that
 * nothing sensitive reaches the UI or the log by accident. */
struct EgressApiResult {
	bool success = false;
	EgressApiError error = EgressApiError::None;
	int httpStatus = 0;

	QString serviceId;
	QString egressId;
	QString status;

	/* True when the request left the machine but the outcome is unknown, so
	 * the backend may have started or still hold a session. */
	bool outcomeUnknown = false;
};

/* All backend communication lives here. The dock never touches the network
 * directly, which keeps API details replaceable.
 *
 * Requests are asynchronous: QNetworkAccessManager delivers completion on the
 * Qt event loop, so the OBS UI thread is never blocked.
 */
class EgressApiClient : public QObject {
	Q_OBJECT

public:
	explicit EgressApiClient(EgressConfig config, QObject *parent = nullptr);
	~EgressApiClient() override;

	void RequestStart();
	void RequestStop(const QString &serviceId);
	void RequestStatus();

	/* Platforms sent with the next start request. Replaced wholesale rather
	 * than merged, so unticking a destination actually removes it. */
	void SetDestinations(const QList<EgressDestination> &destinations) { destinations_ = destinations; }
	const QList<EgressDestination> &Destinations() const { return destinations_; }

	/* Cancels an in-flight request without emitting a completion signal. */
	void AbortPending();

	bool HasPendingRequest() const { return !pendingReply_.isNull(); }

	const EgressConfig &Config() const { return config_; }

signals:
	void StartFinished(const EgressApiResult &result);
	void StopFinished(const EgressApiResult &result);
	void StatusFinished(const EgressApiResult &result);

private:
	enum class RequestKind {
		Start = 1,
		Stop = 2,
		Status = 3,
	};

	void Send(RequestKind kind, const QString &path, const QJsonObject &body, bool isPost);
	void Complete(RequestKind kind, const EgressApiResult &result);
	EgressApiResult BuildResult(QNetworkReply *reply) const;
	EgressApiResult MakeFailure(EgressApiError error) const;

	EgressConfig config_;
	QNetworkAccessManager *networkManager_ = nullptr;
	QPointer<QNetworkReply> pendingReply_;
	QList<EgressDestination> destinations_;
};
