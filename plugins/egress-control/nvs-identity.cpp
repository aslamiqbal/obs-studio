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

#include "nvs-identity.hpp"

#include <obs-module.h>

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include "nvs-credential-store.hpp"

namespace {

constexpr const char *CLIENT_ID = "nvs-desktop";
constexpr int FLOW_TIMEOUT_MS = 300000; /* 5 min, matching the server session TTL */
constexpr int REQUEST_TIMEOUT_MS = 20000;

/* Refresh this long before the access token actually expires. */
constexpr qint64 REFRESH_LEAD_SECONDS = 120;

QByteArray Base64Url(const QByteArray &bytes)
{
	return bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

/* RFC 7636 unreserved-character verifier. */
QString GenerateCodeVerifier()
{
	QByteArray raw(48, Qt::Uninitialized);
	QRandomGenerator::system()->generate(raw.begin(), raw.end());

	return QString::fromLatin1(Base64Url(raw));
}

QString ComputeChallenge(const QString &verifier)
{
	const QByteArray digest = QCryptographicHash::hash(verifier.toUtf8(), QCryptographicHash::Sha256);

	return QString::fromLatin1(Base64Url(digest));
}

QString ResponsePage(const QString &heading, const QString &detail)
{
	const QString body = QStringLiteral(
				     "<!doctype html><meta charset=\"utf-8\">"
				     "<title>NVS</title>"
				     "<style>body{font-family:system-ui,sans-serif;background:#1a1a1a;color:#eee;"
				     "display:flex;align-items:center;justify-content:center;height:100vh;margin:0}"
				     "div{text-align:center}h1{font-size:1.3rem;margin:0 0 .5rem}"
				     "p{opacity:.7;margin:0}</style>"
				     "<div><h1>%1</h1><p>%2</p></div>")
				     .arg(heading.toHtmlEscaped(), detail.toHtmlEscaped());

	return QStringLiteral("HTTP/1.1 200 OK\r\n"
			      "Content-Type: text/html; charset=utf-8\r\n"
			      "Content-Length: %1\r\n"
			      "Cache-Control: no-store\r\n"
			      "Connection: close\r\n\r\n%2")
		.arg(body.toUtf8().size())
		.arg(body);
}

/* Bridges NvsIdentity to the interface EgressApiClient already consumes.
 *
 * Falls back to the development environment variable when nobody is signed in,
 * so a local backend can still be exercised without a full sign-in. */
class IdentityTokenProvider : public AccessTokenProvider {
public:
	IdentityTokenProvider(QPointer<NvsIdentity> identity, std::shared_ptr<AccessTokenProvider> fallback)
		: identity_(std::move(identity)),
		  fallback_(std::move(fallback))
	{
	}

	std::string GetAccessToken() const override
	{
		if (!identity_.isNull()) {
			const std::string token = identity_->AccessToken().toStdString();

			if (!token.empty()) {
				return token;
			}
		}

		return fallback_ ? fallback_->GetAccessToken() : std::string();
	}

private:
	QPointer<NvsIdentity> identity_;
	std::shared_ptr<AccessTokenProvider> fallback_;
};

} // namespace

NvsIdentity::NvsIdentity(QString apiBaseUrl, QObject *parent)
	: QObject(parent),
	  apiBaseUrl_(std::move(apiBaseUrl)),
	  networkManager_(new QNetworkAccessManager(this)),
	  refreshTimer_(new QTimer(this)),
	  flowTimeout_(new QTimer(this))
{
	refreshTimer_->setSingleShot(true);
	connect(refreshTimer_, &QTimer::timeout, this, &NvsIdentity::RefreshAccessToken);

	flowTimeout_->setSingleShot(true);
	flowTimeout_->setInterval(FLOW_TIMEOUT_MS);
	connect(flowTimeout_, &QTimer::timeout, this, [this]() {
		Fail(QString::fromUtf8(obs_module_text("SignIn.TimedOut")));
	});
}

NvsIdentity::~NvsIdentity()
{
	StopListener();
}

std::shared_ptr<AccessTokenProvider> NvsIdentity::TokenProvider(std::shared_ptr<AccessTokenProvider> fallback)
{
	return std::make_shared<IdentityTokenProvider>(QPointer<NvsIdentity>(this), std::move(fallback));
}

void NvsIdentity::SignIn()
{
	if (signingIn_) {
		return;
	}

	if (apiBaseUrl_.isEmpty()) {
		Fail(QString::fromUtf8(obs_module_text("Error.NotConfigured")));
		return;
	}

	StopListener();

	/* Bind to loopback only: no other host may reach this listener. Port 0
	 * lets the OS pick a free ephemeral port. */
	loopbackServer_ = new QTcpServer(this);

	if (!loopbackServer_->listen(QHostAddress::LocalHost, 0)) {
		Fail(QString::fromUtf8(obs_module_text("SignIn.NoLoopback")));
		return;
	}

	connect(loopbackServer_, &QTcpServer::newConnection, this, &NvsIdentity::HandleLoopbackConnection);

	codeVerifier_ = GenerateCodeVerifier();
	signingIn_ = true;

	emit Changed();

	StartFlowRequest(loopbackServer_->serverPort());
}

void NvsIdentity::StartFlowRequest(quint16 port)
{
	QJsonObject body;
	body.insert("client_id", QLatin1String(CLIENT_ID));
	body.insert("code_challenge", ComputeChallenge(codeVerifier_));
	body.insert("code_challenge_method", "S256");
	body.insert("redirect_port", int(port));
	body.insert("device_label", QSysInfo::machineHostName());

	QNetworkRequest request{QUrl(apiBaseUrl_ + "/api/auth/native/start")};
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	request.setTransferTimeout(REQUEST_TIMEOUT_MS);

	QNetworkReply *reply = networkManager_->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));

	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		reply->deleteLater();

		if (reply->error() != QNetworkReply::NoError) {
			Fail(QString::fromUtf8(obs_module_text("Error.ConnectionFailed")));
			return;
		}

		const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());

		if (!document.isObject()) {
			Fail(QString::fromUtf8(obs_module_text("Error.MalformedResponse")));
			return;
		}

		const QJsonObject data = document.object().value("data").toObject();
		const QString verificationUrl = data.value("verification_url").toString();
		sessionId_ = data.value("session_id").toString();

		if (verificationUrl.isEmpty() || sessionId_.isEmpty()) {
			Fail(QString::fromUtf8(obs_module_text("Error.MalformedResponse")));
			return;
		}

		blog(LOG_INFO, "[nvs] opening the system browser for sign-in");

		flowTimeout_->start();

		if (!QDesktopServices::openUrl(QUrl(verificationUrl))) {
			Fail(QString::fromUtf8(obs_module_text("SignIn.NoBrowser")));
		}
	});
}

void NvsIdentity::HandleLoopbackConnection()
{
	if (loopbackServer_.isNull()) {
		return;
	}

	QTcpSocket *socket = loopbackServer_->nextPendingConnection();

	if (!socket) {
		return;
	}

	connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
		const QByteArray request = socket->readAll();
		const int lineEnd = request.indexOf("\r\n");

		if (lineEnd < 0) {
			socket->close();
			socket->deleteLater();
			return;
		}

		/* "GET /nvs-auth/callback?session_id=...&code=... HTTP/1.1" */
		const QList<QByteArray> parts = request.left(lineEnd).split(' ');

		QString heading = QString::fromUtf8(obs_module_text("SignIn.BrowserFailed"));
		QString detail = QString::fromUtf8(obs_module_text("SignIn.BrowserFailedDetail"));

		if (parts.size() >= 2) {
			const QUrl url(QStringLiteral("http://127.0.0.1") + QString::fromUtf8(parts[1]));
			const QUrlQuery query(url);

			const QString code = query.queryItemValue("code", QUrl::FullyDecoded);
			const QString session = query.queryItemValue("session_id", QUrl::FullyDecoded);

			/* The session must match the one this process started, or the
			 * callback did not come from our own flow. */
			if (!code.isEmpty() && session == sessionId_) {
				heading = QString::fromUtf8(obs_module_text("SignIn.BrowserSuccess"));
				detail = QString::fromUtf8(obs_module_text("SignIn.BrowserSuccessDetail"));

				ExchangeCode(session, code);
			}
		}

		socket->write(ResponsePage(heading, detail).toUtf8());
		socket->flush();
		socket->disconnectFromHost();
		socket->deleteLater();
	});
}

void NvsIdentity::ExchangeCode(const QString &sessionId, const QString &code)
{
	flowTimeout_->stop();

	QJsonObject body;
	body.insert("session_id", sessionId);
	body.insert("code", code);
	body.insert("code_verifier", codeVerifier_);

	QNetworkRequest request{QUrl(apiBaseUrl_ + "/api/auth/native/token")};
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	request.setTransferTimeout(REQUEST_TIMEOUT_MS);

	QNetworkReply *reply = networkManager_->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));

	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		reply->deleteLater();

		/* The verifier has served its purpose either way. */
		codeVerifier_.clear();
		sessionId_.clear();
		StopListener();

		if (reply->error() != QNetworkReply::NoError) {
			Fail(QString::fromUtf8(obs_module_text("SignIn.Rejected")));
			return;
		}

		const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());

		if (!document.isObject()) {
			Fail(QString::fromUtf8(obs_module_text("Error.MalformedResponse")));
			return;
		}

		signingIn_ = false;
		ApplyLoginResponse(document.object().value("data").toObject());

		blog(LOG_INFO, "[nvs] signed in successfully");
	});
}

void NvsIdentity::ApplyLoginResponse(const QJsonObject &data)
{
	accessToken_ = data.value("token").toString();
	refreshToken_ = data.value("refresh_token").toString();
	displayName_ = data.value("display_name").toString();
	role_ = data.value("role").toString();
	expiresAt_ = QDateTime::fromString(data.value("expires_at").toString(), Qt::ISODate);

	if (accessToken_.isEmpty()) {
		Fail(QString::fromUtf8(obs_module_text("SignIn.Rejected")));
		return;
	}

	Persist();
	ScheduleRefresh();

	emit Changed();
}

void NvsIdentity::ScheduleRefresh()
{
	refreshTimer_->stop();

	if (refreshToken_.isEmpty() || !expiresAt_.isValid()) {
		return;
	}

	const qint64 secondsLeft = QDateTime::currentDateTimeUtc().secsTo(expiresAt_.toUTC()) - REFRESH_LEAD_SECONDS;

	/* Already expired or nearly so: refresh on the next event loop turn. */
	refreshTimer_->start(secondsLeft > 0 ? int(secondsLeft * 1000) : 0);
}

void NvsIdentity::RefreshAccessToken()
{
	if (refreshToken_.isEmpty()) {
		return;
	}

	QJsonObject body;
	body.insert("refresh_token", refreshToken_);

	QNetworkRequest request{QUrl(apiBaseUrl_ + "/api/auth/refresh")};
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	request.setTransferTimeout(REQUEST_TIMEOUT_MS);

	QNetworkReply *reply = networkManager_->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));

	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		reply->deleteLater();

		if (reply->error() != QNetworkReply::NoError) {
			/* A refresh failure is not fatal on its own — the network may
			 * simply be down. Only a rejected token ends the session. */
			const int status =
				reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

			if (status == 400 || status == 401) {
				blog(LOG_INFO, "[nvs] stored session is no longer valid; signing out");
				SignOut();
			} else {
				refreshTimer_->start(60000);
			}

			return;
		}

		const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());

		if (document.isObject()) {
			ApplyLoginResponse(document.object().value("data").toObject());
		}
	});
}

void NvsIdentity::RestoreSession()
{
	const QJsonObject stored = NvsCredentialStore::Load();

	if (stored.isEmpty()) {
		return;
	}

	accessToken_ = stored.value("token").toString();
	refreshToken_ = stored.value("refresh_token").toString();
	displayName_ = stored.value("display_name").toString();
	role_ = stored.value("role").toString();
	expiresAt_ = QDateTime::fromString(stored.value("expires_at").toString(), Qt::ISODate);

	if (refreshToken_.isEmpty()) {
		return;
	}

	blog(LOG_INFO, "[nvs] restored a stored session for the signed-in account");

	emit Changed();

	/* Always refresh on restore: the stored access token is usually stale. */
	RefreshAccessToken();
}

void NvsIdentity::Persist() const
{
	QJsonObject payload;
	payload.insert("token", accessToken_);
	payload.insert("refresh_token", refreshToken_);
	payload.insert("display_name", displayName_);
	payload.insert("role", role_);
	payload.insert("expires_at", expiresAt_.toUTC().toString(Qt::ISODate));

	NvsCredentialStore::Save(payload);
}

void NvsIdentity::CancelSignIn()
{
	if (!signingIn_) {
		return;
	}

	flowTimeout_->stop();
	StopListener();

	codeVerifier_.clear();
	sessionId_.clear();
	signingIn_ = false;

	emit Changed();
}

void NvsIdentity::SignOut()
{
	CancelSignIn();

	refreshTimer_->stop();

	accessToken_.clear();
	refreshToken_.clear();
	displayName_.clear();
	role_.clear();
	expiresAt_ = QDateTime();

	NvsCredentialStore::Clear();

	blog(LOG_INFO, "[nvs] signed out");

	emit Changed();
}

void NvsIdentity::StopListener()
{
	if (loopbackServer_.isNull()) {
		return;
	}

	loopbackServer_->close();
	loopbackServer_->deleteLater();
	loopbackServer_.clear();
}

void NvsIdentity::Fail(const QString &message)
{
	flowTimeout_->stop();
	StopListener();

	codeVerifier_.clear();
	sessionId_.clear();
	signingIn_ = false;

	blog(LOG_WARNING, "[nvs] sign-in did not complete");

	emit Changed();
	emit SignInFailed(message);
}
