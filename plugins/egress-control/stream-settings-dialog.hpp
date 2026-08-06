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

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;

/* Minimal stream destination editor for the console window.
 *
 * Covers the custom RTMP case (server + stream key), which is what an operator
 * needs from the simplified console. Preset services with OAuth are configured
 * in the full OBS settings dialog; this dialog never touches them.
 *
 * Built entirely on public obs_service_* and obs_frontend_* calls.
 */
class StreamSettingsDialog : public QDialog {
	Q_OBJECT

public:
	explicit StreamSettingsDialog(QWidget *parent = nullptr);

private slots:
	void Save();

private:
	void LoadCurrentService();

	QLineEdit *serverEdit_ = nullptr;
	QLineEdit *streamKeyEdit_ = nullptr;
	QLabel *noticeLabel_ = nullptr;
	QPushButton *saveButton_ = nullptr;
};
