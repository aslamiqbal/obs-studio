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

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QString>

#include "egress-config.hpp"

class QNetworkAccessManager;
class QTcpServer;
class QTimer;

/* Sign-in for the NVS desktop client.
 *
 * Implements the RFC 8252 native-app pattern: the system browser performs the
 * Google sign-in, and the result comes back to a loopback listener bound to
 * 127.0.0.1 on an ephemeral port. PKCE (S256) binds the authorization code to
 * this process, so another local program cannot race the callback and redeem it.
 *
 * No password or Google credential ever passes through NVS.
 */
class NvsIdentity : public QObject {
	Q_OBJECT

public:
	explicit NvsIdentity(QString apiBaseUrl, QObject *parent = nullptr);
	~NvsIdentity() override;

	bool IsSignedIn() const { return !accessToken_.isEmpty(); }
	bool IsSigningIn() const { return signingIn_; }
	QString DisplayName() const { return displayName_; }
	QString AccessToken() const { return accessToken_; }

	/* Opens the system browser and waits for the loopback callback. */
	void SignIn();
	void CancelSignIn();
	void SignOut();

	/* Restores a previous session from the encrypted store, refreshing the
	 * access token if it has expired. */
	void RestoreSession();

	/* Adapts this service to the interface EgressApiClient already consumes.
	 * The fallback is used while nobody is signed in. */
	std::shared_ptr<AccessTokenProvider> TokenProvider(std::shared_ptr<AccessTokenProvider> fallback = nullptr);

signals:
	void Changed();
	void SignInFailed(const QString &message);

private:
	void StartFlowRequest(quint16 port);
	void HandleLoopbackConnection();
	void ExchangeCode(const QString &sessionId, const QString &code);
	void RefreshAccessToken();
	void ApplyLoginResponse(const QJsonObject &data);
	void Persist() const;
	void Fail(const QString &message);
	void StopListener();
	void ScheduleRefresh();

	QString apiBaseUrl_;

	QNetworkAccessManager *networkManager_ = nullptr;
	QPointer<QTcpServer> loopbackServer_;
	QTimer *refreshTimer_ = nullptr;
	QTimer *flowTimeout_ = nullptr;

	QString accessToken_;
	QString refreshToken_;
	QString displayName_;
	QString role_;
	QDateTime expiresAt_;

	/* Held only for the duration of one sign-in. */
	QString codeVerifier_;
	QString sessionId_;

	bool signingIn_ = false;
};
