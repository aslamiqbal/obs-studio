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

/* Lifecycle of the server-side Egress service as observed by this plugin.
 *
 * Values start at 1 so that a zero-initialized EgressState is never mistaken
 * for a valid state, per the project code style guidelines.
 */
enum class EgressState {
	Offline = 1,
	Starting = 2,
	Live = 3,
	Stopping = 4,
	Error = 5,
};

/* Locale key describing a state, resolved through obs_module_text() by the UI. */
inline const char *EgressStateLocaleKey(EgressState state)
{
	switch (state) {
	case EgressState::Offline:
		return "State.Offline";
	case EgressState::Starting:
		return "State.Starting";
	case EgressState::Live:
		return "State.Live";
	case EgressState::Stopping:
		return "State.Stopping";
	case EgressState::Error:
		return "State.Error";
	}

	return "State.Error";
}
