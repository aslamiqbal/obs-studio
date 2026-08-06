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

#include <QObject>
#include <QPointer>

class EgressController;
class NvsIdentity;
class StreamConsoleWindow;
class QAction;
class QSystemTrayIcon;

/* Adds the NVS entries to the notification-area menu.
 *
 * NVS deliberately does not create a second tray icon: it attaches to the one
 * the frontend already owns and appends its own section above Exit. That keeps
 * a single icon in the notification area and leaves the existing entries
 * (Show/Hide, streaming, recording, projectors) untouched.
 */
class NvsTray : public QObject {
	Q_OBJECT

public:
	NvsTray(EgressController *controller, NvsIdentity *identity, StreamConsoleWindow *console,
		QObject *parent = nullptr);

	/* Finds the frontend's tray icon and installs the NVS entries. Safe to
	 * call once the frontend has finished loading. Returns false when no tray
	 * is available, in which case nothing is changed. */
	bool Attach();

	/* Removes the entries again, so unloading the plugin does not leave dead
	 * actions in a menu owned by the main window. */
	void Detach();

	/* True when this launch was asked to start hidden, i.e. the process was
	 * started with --minimize-to-tray (how the Windows startup entry runs). */
	static bool StartedHidden();

private slots:
	void Refresh();

private:
	EgressController *controller_ = nullptr;
	NvsIdentity *identity_ = nullptr;
	QPointer<StreamConsoleWindow> console_;

	QPointer<QSystemTrayIcon> trayIcon_;

	QPointer<QAction> separator_;
	QPointer<QAction> consoleAction_;
	QPointer<QAction> egressStartAction_;
	QPointer<QAction> egressStopAction_;
	QPointer<QAction> accountAction_;
};
