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

#include <functional>

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>

class EgressController;
class NvsIdentity;
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

/* Reports this install to the backend and carries out admin instructions.
 *
 * Registration happens once per sign-in; after that a heartbeat runs on the
 * interval the server asks for. The heartbeat response is also the command
 * channel — the desktop sits behind arbitrary NAT, so nothing is pushed to it.
 *
 * Commands are executed, then acknowledged on the *following* heartbeat, so the
 * dashboard reflects what actually happened rather than what was dispatched.
 */
class NvsFleetClient : public QObject {
	Q_OBJECT

public:
	NvsFleetClient(QString apiBaseUrl, EgressController *controller, NvsIdentity *identity,
		       QObject *parent = nullptr);

	/* Invoked for the show_console command; set by the module. */
	void SetShowConsoleHandler(std::function<void()> handler) { showConsole_ = std::move(handler); }

	/* Begins registration when signed in, and stops when signed out. */
	void Start();
	void Stop();

	bool IsRegistered() const { return !clientId_.isEmpty(); }
	bool IsBlocked() const { return isBlocked_; }

signals:
	void Changed();

private slots:
	void OnIdentityChanged();
	void SendHeartbeat();

private:
	void Register();
	void HandleCommands(const QJsonArray &commands);
	void ApplyCommand(const QString &commandId, const QString &command);
	void QueueAck(const QString &commandId, bool success, const QString &message);
	QNetworkReply *Post(const QString &path, const QJsonObject &body);

	/* Stable across restarts so one machine stays one row in the dashboard. */
	static QString DeviceId();

	QString apiBaseUrl_;
	EgressController *controller_ = nullptr;
	NvsIdentity *identity_ = nullptr;

	QNetworkAccessManager *networkManager_ = nullptr;
	QTimer *heartbeatTimer_ = nullptr;

	QString clientId_;
	bool isBlocked_ = false;
	bool registering_ = false;

	/* Results waiting to ride along on the next heartbeat. */
	QJsonArray pendingAcks_;

	std::function<void()> showConsole_;
};
