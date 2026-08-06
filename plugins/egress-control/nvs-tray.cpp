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

#include "nvs-tray.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QAction>
#include <QCoreApplication>
#include <QMainWindow>
#include <QMenu>
#include <QSystemTrayIcon>

#include "egress-controller.hpp"
#include "egress-state.hpp"
#include "nvs-identity.hpp"
#include "stream-console-window.hpp"

namespace {

/* The flag the Windows startup entry passes; it is a stock OBS option that
 * already hides the main window during startup. */
constexpr const char *MINIMIZE_TO_TRAY_ARG = "--minimize-to-tray";

} // namespace

NvsTray::NvsTray(EgressController *controller, NvsIdentity *identity, StreamConsoleWindow *console, QObject *parent)
	: QObject(parent),
	  controller_(controller),
	  identity_(identity),
	  console_(console)
{
}

bool NvsTray::StartedHidden()
{
	const QStringList arguments = QCoreApplication::arguments();

	for (const QString &argument : arguments) {
		if (argument.compare(QLatin1String(MINIMIZE_TO_TRAY_ARG), Qt::CaseInsensitive) == 0) {
			return true;
		}
	}

	return false;
}

bool NvsTray::Attach()
{
	if (!trayIcon_.isNull()) {
		return true;
	}

	QMainWindow *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());

	if (!mainWindow) {
		return false;
	}

	/* The frontend owns the tray icon; find it rather than creating another. */
	trayIcon_ = mainWindow->findChild<QSystemTrayIcon *>();

	if (trayIcon_.isNull()) {
		blog(LOG_INFO, "[nvs] no system tray icon available; skipping tray entries");
		return false;
	}

	QMenu *menu = trayIcon_->contextMenu();

	if (!menu) {
		blog(LOG_INFO, "[nvs] system tray has no menu; skipping tray entries");
		return false;
	}

	consoleAction_ = new QAction(obs_module_text("StreamConsole"), menu);
	egressStartAction_ = new QAction(obs_module_text("StartService"), menu);
	egressStopAction_ = new QAction(obs_module_text("StopService"), menu);
	accountAction_ = new QAction(obs_module_text("SignIn"), menu);

	/* Insert above the trailing Exit entry so Exit stays last, which is where
	 * users expect it. */
	const QList<QAction *> existing = menu->actions();
	QAction *before = existing.isEmpty() ? nullptr : existing.last();

	separator_ = menu->insertSeparator(before);
	menu->insertAction(before, consoleAction_);
	menu->insertAction(before, accountAction_);
	menu->insertAction(before, egressStartAction_);
	menu->insertAction(before, egressStopAction_);

	connect(consoleAction_, &QAction::triggered, this, [this]() {
		if (console_.isNull()) {
			return;
		}

		console_->show();
		console_->raise();
		console_->activateWindow();
	});

	connect(egressStartAction_, &QAction::triggered, this, [this]() {
		controller_->Start();
	});

	connect(egressStopAction_, &QAction::triggered, this, [this]() {
		controller_->Stop();
	});

	connect(accountAction_, &QAction::triggered, this, [this]() {
		if (identity_->IsSignedIn()) {
			identity_->SignOut();
		} else if (!identity_->IsSigningIn()) {
			identity_->SignIn();
		}
	});

	connect(controller_, &EgressController::Changed, this, &NvsTray::Refresh);
	connect(identity_, &NvsIdentity::Changed, this, &NvsTray::Refresh);

	Refresh();

	blog(LOG_INFO, "[nvs] tray entries installed");

	return true;
}

void NvsTray::Detach()
{
	/* QPointer guards the case where the main window already destroyed the
	 * menu and its actions during shutdown. */
	for (QPointer<QAction> action :
	     {separator_, consoleAction_, accountAction_, egressStartAction_, egressStopAction_}) {
		if (!action.isNull()) {
			delete action.data();
		}
	}

	separator_.clear();
	consoleAction_.clear();
	accountAction_.clear();
	egressStartAction_.clear();
	egressStopAction_.clear();
	trayIcon_.clear();
}

void NvsTray::Refresh()
{
	if (egressStartAction_.isNull()) {
		return;
	}

	egressStartAction_->setEnabled(controller_->StartAvailable());
	egressStopAction_->setEnabled(controller_->StopAvailable());

	/* Show the live state on the entry itself, so the menu answers "is it
	 * running?" without opening the console. */
	const QString state = QString::fromUtf8(obs_module_text(EgressStateLocaleKey(controller_->State())));
	egressStartAction_->setText(QString::fromUtf8(obs_module_text("StartService")) + " (" + state + ")");

	if (identity_->IsSigningIn()) {
		accountAction_->setText(obs_module_text("SignIn.Cancel"));
		accountAction_->setEnabled(true);
	} else if (identity_->IsSignedIn()) {
		accountAction_->setText(QString::fromUtf8(obs_module_text("SignOut")) + " (" +
					identity_->DisplayName() + ")");
		accountAction_->setEnabled(true);
	} else {
		accountAction_->setText(obs_module_text("SignIn"));
		accountAction_->setEnabled(true);
	}
}
