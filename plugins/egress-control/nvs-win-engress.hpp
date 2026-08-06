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

class ProgramPreviewWidget;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QTimer;

/* The simplified egress window ("nvswinengress").
 *
 * UI skeleton only for now: room preview, room code/token, a Facebook/YouTube
 * choice, the live URL and token, and Start Egress. The behaviour behind the
 * buttons lands separately.
 */
class NvsWinEngress : public QWidget {
	Q_OBJECT

public:
	explicit NvsWinEngress(QWidget *parent = nullptr);
	~NvsWinEngress() override;

	/* Releases the preview display before OBS tears down graphics. */
	void ReleasePreview();

	/* The room link built from the two fields. Empty until both are filled. */
	QString RoomUrl() const;

	/* Streaming state changes arrive here, routed by the module. */
	void HandleFrontendEvent(enum obs_frontend_event event);

private slots:
	/* Points the scene's browser source at the composed room link, which is
	 * what makes the Room window show the room. */
	void ApplyRoomUrl();

	/* Drives OBS's own streaming output — the same path as the main window's
	 * Start Streaming button. */
	void OnStartEgressClicked();

protected:
	/* Closing hides rather than quits, same as the console. */
	void closeEvent(QCloseEvent *event) override;

private:
	ProgramPreviewWidget *preview_ = nullptr;

	QLineEdit *roomCodeEdit_ = nullptr;
	QLineEdit *roomTokenEdit_ = nullptr;

	QRadioButton *facebookRadio_ = nullptr;
	QRadioButton *youtubeRadio_ = nullptr;
	QPushButton *tryOpenButton_ = nullptr;

	/* Sits under Open FB Live: the key an operator copies from the page that
	 * button opens. */
	QLineEdit *streamKeyEdit_ = nullptr;
	QPushButton *showStreamKeyButton_ = nullptr;

	QLineEdit *liveUrlEdit_ = nullptr;

	QPushButton *startEgressButton_ = nullptr;
	QPushButton *exitButton_ = nullptr;

	/* Pulses the record dot while live. A steady dot reads as "armed"; a
	 * blinking one is the convention for "recording right now". */
	QTimer *blinkTimer_ = nullptr;
	bool blinkOn_ = true;

	void SetIdleButton();
	void StartBlinking();
	void StopBlinking();

	QLabel *roomNoticeLabel_ = nullptr;
};
