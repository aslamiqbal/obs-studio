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

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QMainWindow>
#include <QPointer>

#include "egress-config.hpp"
#include "egress-control-dock.hpp"
#include "egress-controller.hpp"
#include "nvs-fleet-client.hpp"
#include "nvs-identity.hpp"
#include "nvs-tray.hpp"
#include "nvs-win-engress.hpp"
#include "stream-console-window.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("egress-control", "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Simplified stream console and server-side Egress start/stop control";
}

namespace {

/* Identifies the dock for obs_frontend_add_dock_by_id() / _remove_dock(). */
constexpr const char *DOCK_ID = "egress_control_dock";

/* Shared state behind both the dock and the console window. */
EgressController *controller = nullptr;

/* Account session shared by every component that calls the backend. */
NvsIdentity *identity = nullptr;

/* Owned by the OBS frontend once registered: obs_frontend_add_dock_by_id()
 * reparents the widget into a dock owned by the main window. QPointer so this
 * goes null if the frontend tears the dock down first. */
QPointer<EgressControlDock> dock;

/* Parented to the OBS main window so it is destroyed with the frontend. */
QPointer<StreamConsoleWindow> console;

/* The simplified egress window from the new design; opens at startup. */
QPointer<NvsWinEngress> engressWindow;

/* Adds the NVS entries to the frontend's notification-area menu. */
NvsTray *tray = nullptr;

/* Reports this install to the backend and applies admin commands. */
NvsFleetClient *fleet = nullptr;

void ShowConsole()
{
	if (console.isNull()) {
		return;
	}

	console->show();
	console->raise();
	console->activateWindow();
}

void OBSFrontendEvent(enum obs_frontend_event event, void *)
{
	if (!console.isNull()) {
		console->HandleFrontendEvent(event);
	}

	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		/* Restore before probing so the status call carries a token. */
		if (identity) {
			identity->RestoreSession();
		}

		if (controller) {
			controller->QueryInitialStatus();
		}

		if (tray) {
			tray->Attach();
		}

		/* Registers as soon as a session is available; a signed-out client
		 * simply reports nothing. */
		if (fleet) {
			fleet->Start();
		}

		/* The new engress window is the startup window; the full console
		 * stays reachable from the tray and the Tools menu. A hidden launch
		 * still opens nothing. */
		if (NvsTray::StartedHidden()) {
			blog(LOG_INFO, "[nvs] started hidden; windows are available from the tray menu");
		} else if (!engressWindow.isNull()) {
			engressWindow->show();
			engressWindow->raise();
			engressWindow->activateWindow();
		}
	} else if (event == OBS_FRONTEND_EVENT_EXIT) {
		/* Stop reporting before teardown so no heartbeat races shutdown. */
		if (fleet) {
			fleet->Stop();
		}

		if (controller) {
			controller->HandleExit();
		}

		/* Release the preview displays while the graphics subsystem is still
		 * alive. */
		if (!console.isNull()) {
			console->ReleasePreview();
		}

		if (!engressWindow.isNull()) {
			engressWindow->ReleasePreview();
		}
	}
}

} // namespace

bool obs_module_load(void)
{
	/* Modules are loaded from OBSBasic::OBSInit(), so the main window already
	 * exists and UI can be created here. */
	QMainWindow *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());

	/* Loaded once and shared: the identity service and the Egress client talk
	 * to the same backend. */
	EgressConfig config = EgressConfig::Load();

	/* Captured before the config is moved into the controller below. */
	const QString baseUrl = config.baseUrl;

	identity = new NvsIdentity(baseUrl);

	/* Requests carry the signed-in account's token, falling back to the
	 * development environment variable when nobody is signed in. */
	config.tokenProvider = identity->TokenProvider(config.tokenProvider);

	controller = new EgressController(std::move(config));

	dock = new EgressControlDock(controller);

	if (!obs_frontend_add_dock_by_id(DOCK_ID, obs_module_text("EgressControl"), dock.data())) {
		blog(LOG_WARNING, "[egress-control] failed to register dock, plugin will not load");
		delete dock.data();
		dock.clear();
		delete controller;
		controller = nullptr;
		delete identity;
		identity = nullptr;
		return false;
	}

	console = new StreamConsoleWindow(controller, identity, mainWindow);
	engressWindow = new NvsWinEngress(mainWindow);

	/* Attached later: the frontend creates its tray icon during startup, so
	 * the entries go in once loading has finished. */
	tray = new NvsTray(controller, identity, console.data());

	fleet = new NvsFleetClient(baseUrl, controller, identity);
	fleet->SetShowConsoleHandler([]() { ShowConsole(); });

	obs_frontend_add_tools_menu_item(
		obs_module_text("StreamConsole"), [](void *) { ShowConsole(); }, nullptr);

	obs_frontend_add_event_callback(OBSFrontendEvent, nullptr);

	blog(LOG_INFO, "[egress-control] plugin loaded");

	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(OBSFrontendEvent, nullptr);

	/* Remove the tray entries first: they live in a menu owned by the main
	 * window and would outlive this module otherwise. */
	if (fleet) {
		fleet->Stop();
		delete fleet;
		fleet = nullptr;
	}

	if (tray) {
		tray->Detach();
		delete tray;
		tray = nullptr;
	}

	if (!engressWindow.isNull()) {
		engressWindow->ReleasePreview();
		delete engressWindow.data();
		engressWindow.clear();
	}

	if (!console.isNull()) {
		console->ReleasePreview();
		delete console.data();
		console.clear();
	}

	/* Destroys the dock along with the widget it adopted; deleting the widget
	 * here as well would be a double free. */
	obs_frontend_remove_dock(DOCK_ID);
	dock.clear();

	delete controller;
	controller = nullptr;

	delete identity;
	identity = nullptr;

	blog(LOG_INFO, "[egress-control] plugin unloaded");
}
