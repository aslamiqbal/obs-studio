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

#include <QWidget>

class EgressController;
class NvsIdentity;
class ProgramPreviewWidget;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

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
	void OnStreamButtonClicked();
	void OnStreamSettingsClicked();
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

	EgressController *controller_ = nullptr;
	NvsIdentity *identity_ = nullptr;

	ProgramPreviewWidget *preview_ = nullptr;

	QPushButton *signInButton_ = nullptr;
	QLabel *accountLabel_ = nullptr;
	QCheckBox *startWithWindowsCheck_ = nullptr;

	QComboBox *sceneSelector_ = nullptr;
	QPushButton *streamButton_ = nullptr;
	QPushButton *streamSettingsButton_ = nullptr;
	QLabel *streamStatusLabel_ = nullptr;

	QPushButton *egressStartButton_ = nullptr;
	QPushButton *egressStopButton_ = nullptr;
	QLabel *egressStatusLabel_ = nullptr;

	QCheckBox *youtubeCheck_ = nullptr;
	QLineEdit *youtubeLiveTokenEdit_ = nullptr;
	QCheckBox *facebookCheck_ = nullptr;
	QLineEdit *facebookLiveTokenEdit_ = nullptr;

	/* Suppresses the combo box signal while the list is repopulated from OBS,
	 * so refreshing the UI cannot trigger a scene change. */
	bool updatingSceneList_ = false;
};
