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

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

class NvsIdentity;
class QNetworkAccessManager;
class QNetworkReply;

/* One room this account may operate. */
struct NvsRoomInfo {
	QString id;
	QString name;
	QString code;
};

/* Reads the rooms this account can broadcast, and mints the browser-source URL
 * that renders one.
 *
 * The minted URL carries its credential in the fragment, so it is a secret:
 * it is split into address and token for display and never logged.
 */
class NvsRoomClient : public QObject {
	Q_OBJECT

public:
	NvsRoomClient(QString apiBaseUrl, NvsIdentity *identity, QObject *parent = nullptr);

	void FetchRooms();
	void FetchJoinUrl(const QString &roomId);

	/* Splits a minted URL into the part safe to show and the credential. */
	static QString AddressOf(const QString &url);
	static QString TokenOf(const QString &url);
	static QString Compose(const QString &address, const QString &token);

signals:
	void RoomsFetched(const QList<NvsRoomInfo> &rooms);
	void JoinUrlFetched(const QString &address, const QString &token);
	void Failed(const QString &message);

private:
	QNetworkReply *Send(const QString &path, bool isPost);

	QString apiBaseUrl_;
	NvsIdentity *identity_ = nullptr;
	QNetworkAccessManager *networkManager_ = nullptr;
};
