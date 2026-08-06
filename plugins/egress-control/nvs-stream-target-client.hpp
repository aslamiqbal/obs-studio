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
#include <QString>

class NvsIdentity;
class QNetworkAccessManager;
class QNetworkReply;

/* A broadcast destination as the backend stores it.
 *
 * The stream key is deliberately absent: the server keeps it encrypted and
 * never returns it, so the console can create or replace a key but not read
 * one back. */
struct NvsStreamTarget {
	QString id;
	QString platformType;
	QString name;
	QString url;
	bool enabled = true;
};

/* Talks to the existing stream-target API — the real broadcast destinations.
 *
 * Starting a broadcast is per target (POST /api/stream-targets/{id}/start), so
 * "start egress" here means starting every enabled target for the room.
 */
class NvsStreamTargetClient : public QObject {
	Q_OBJECT

public:
	NvsStreamTargetClient(QString apiBaseUrl, NvsIdentity *identity, QObject *parent = nullptr);

	void FetchTargets(const QString &roomId);

	/* Creates the target when id is empty, otherwise replaces it. An empty
	 * streamKey on an update leaves the stored key untouched server-side. */
	void SaveTarget(const QString &roomId, const NvsStreamTarget &target, const QString &streamKey);

	void DeleteTarget(const QString &targetId);

	void StartTarget(const QString &targetId);
	void StopTarget(const QString &targetId);

signals:
	void TargetsFetched(const QList<NvsStreamTarget> &targets);
	void TargetSaved(const QString &platformType);
	void TargetDeleted(const QString &targetId);
	void TargetStarted(const QString &targetId);
	void TargetStopped(const QString &targetId);
	void Failed(const QString &message);

private:
	QNetworkReply *Send(const QString &path, const QByteArray &verb, const QByteArray &body = {});

	QString apiBaseUrl_;
	NvsIdentity *identity_ = nullptr;
	QNetworkAccessManager *networkManager_ = nullptr;
};
