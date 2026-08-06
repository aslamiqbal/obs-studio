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

#include <obs-frontend-api.h>

#include <QList>
#include <QStringList>
#include <QWidget>

#include "nvs-multi-rtmp.hpp"
#include "nvs-room-client.hpp"
#include "nvs-stream-target-client.hpp"

class EgressController;
class NvsIdentity;
class ProgramPreviewWidget;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

/* Simplified operator console: program preview plus the few controls a
 * broadcast operator needs.
 *
 * A standalone top-level window. The normal OBS main window is left completely
 * untouched and stays fully usable behind it.
 */
class StreamConsoleWindow : public QWidget {
	Q_OBJECT

public:
	StreamConsoleWindow(EgressController *controller, NvsIdentity *identity, QWidget *parent = nullptr);
	~StreamConsoleWindow() override;

	/* Frontend events are routed here by the module. */
	void HandleFrontendEvent(enum obs_frontend_event event);

	/* Releases the preview display before OBS tears down graphics. */
	void ReleasePreview();

private slots:
	void OnRefreshRoomsClicked();
	void OnFetchJoinUrlClicked();
	void OnApplyRoomToBrowserSource();
	void OnRoomsFetched(const QList<NvsRoomInfo> &rooms);
	void OnTargetsFetched(const QList<NvsStreamTarget> &targets);
	void OnPushDestinationsClicked();
	void OnStartDestinationsClicked();
	void OnStopDestinationsClicked();
	void OnJoinUrlFetched(const QString &address, const QString &token);
	void OnSceneSelected(int index);
	void OnSignInClicked();
	void OnStartupToggled(bool checked);
	void RefreshEgress();
	void RefreshIdentity();

protected:
	void closeEvent(QCloseEvent *event) override;

private:
	void RefreshSceneList();
	void RefreshCurrentScene();
	void RefreshStreamingState();

	/* Room the console broadcasts, and the credentials that render it. */
	QWidget *BuildRoomGroup();
	void LoadRoomSettings();
	void SaveRoomSettings() const;
	QString SelectedRoomId() const;


	/* One-time setup once every module has loaded. */
	void InitStreamSettings();

	/* Broadcast destinations: load from the encrypted store, push the current
	 * selection to the controller, and persist edits. */
	void LoadDestinations();
	void ApplyDestinations();
	void SaveDestinations() const;
	void RefreshDestinationsNotice();

	EgressController *controller_ = nullptr;
	NvsIdentity *identity_ = nullptr;

	ProgramPreviewWidget *preview_ = nullptr;

	QPushButton *signInButton_ = nullptr;
	QLabel *accountLabel_ = nullptr;
	QCheckBox *startWithWindowsCheck_ = nullptr;

	/* Room selection and the credentials that render it. */
	NvsRoomClient *roomClient_ = nullptr;
	QComboBox *roomSelector_ = nullptr;
	QPushButton *refreshRoomsButton_ = nullptr;
	QLineEdit *roomAddressEdit_ = nullptr;
	QLineEdit *roomTokenEdit_ = nullptr;
	QPushButton *fetchJoinUrlButton_ = nullptr;
	QPushButton *applyRoomButton_ = nullptr;
	QLabel *roomNoticeLabel_ = nullptr;

	QComboBox *sceneSelector_ = nullptr;
	QLabel *streamStatusLabel_ = nullptr;



	QPushButton *egressStartButton_ = nullptr;
	QPushButton *egressStopButton_ = nullptr;
	QLabel *egressStatusLabel_ = nullptr;

	/* One row per destination. A room may carry several YouTube and several
	 * Facebook endpoints, so this is a list rather than one row per platform. */
	QTableWidget *destinationsTable_ = nullptr;
	QLabel *destinationsNoticeLabel_ = nullptr;
	QPushButton *pushDestinationsButton_ = nullptr;

	/* Server ids of rows the operator removed, deleted on the next save. */
	QStringList removedTargetIds_ = {};

	QWidget *BuildDestinationsGroup();
	void AddDestinationRow(const NvsStreamTarget &target);
	void RemoveDestinationRow(int row);

	/* Clears OBS's global audio capture: NVS forwards room audio only. */
	void EnforceAudioPolicy();

	/* Encodes once and pushes to every enabled destination. OBS's own single
	 * streaming output cannot reach more than one. */
	NvsMultiRtmp *multiRtmp_ = nullptr;
	QList<NvsRtmpDestination> EnabledRtmpDestinations() const;

	/* Server-side targets for the selected room, keyed by platform_type. */
	NvsStreamTargetClient *targetClient_ = nullptr;
	QList<NvsStreamTarget> serverTargets_ = {};

	QString TargetIdFor(const QString &platformType) const;

	/* Suppresses the combo box signal while the list is repopulated from OBS,
	 * so refreshing the UI cannot trigger a scene change. */
	bool updatingSceneList_ = false;
};
