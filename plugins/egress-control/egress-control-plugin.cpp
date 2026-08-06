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

#include <QPointer>

#include "egress-control-dock.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("egress-control", "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Start and stop a server-side Egress service from an OBS dock";
}

namespace {

/* Identifies the dock for obs_frontend_add_dock_by_id() / _remove_dock(). */
constexpr const char *DOCK_ID = "egress_control_dock";

/* Owned by the OBS frontend once registered: obs_frontend_add_dock_by_id()
 * reparents the widget into a dock owned by the main window. QPointer so this
 * goes null if the frontend tears the dock down first. */
QPointer<EgressControlDock> dock;

void OBSFrontendEvent(enum obs_frontend_event event, void *)
{
	if (dock.isNull()) {
		return;
	}

	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		dock->QueryInitialStatus();
	} else if (event == OBS_FRONTEND_EVENT_EXIT) {
		dock->HandleExit();
	}
}

} // namespace

bool obs_module_load(void)
{
	/* Modules are loaded from OBSBasic::OBSInit(), so the main window already
	 * exists and the dock can be registered directly. */
	dock = new EgressControlDock();

	if (!obs_frontend_add_dock_by_id(DOCK_ID, obs_module_text("EgressControl"), dock.data())) {
		blog(LOG_WARNING, "[egress-control] failed to register dock, plugin will not load");
		delete dock.data();
		dock.clear();
		return false;
	}

	obs_frontend_add_event_callback(OBSFrontendEvent, nullptr);

	blog(LOG_INFO, "[egress-control] plugin loaded");

	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(OBSFrontendEvent, nullptr);

	/* Destroys the dock along with the widget it adopted; deleting the widget
	 * here as well would be a double free. */
	obs_frontend_remove_dock(DOCK_ID);
	dock.clear();

	blog(LOG_INFO, "[egress-control] plugin unloaded");
}
