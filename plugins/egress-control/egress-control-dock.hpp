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

#include <QString>
#include <QWidget>

#include "egress-api-client.hpp"
#include "egress-state.hpp"

class QLabel;
class QPushButton;

/* Dock contents for the "Egress Control" panel.
 *
 * Owns the widgets and the state machine only. Every backend call is delegated
 * to EgressApiClient and completes asynchronously, so no handler here blocks.
 */
class EgressControlDock : public QWidget {
	Q_OBJECT

public:
	explicit EgressControlDock(QWidget *parent = nullptr);

	/* Called once the frontend has finished loading, so a session that is
	 * already running server-side shows up as Live. */
	void QueryInitialStatus();

	/* Called while OBS is shutting down. Only stops the service if the
	 * stop_egress_on_exit option is explicitly enabled. */
	void HandleExit();

private slots:
	void OnStartClicked();
	void OnStopClicked();
	void OnStartFinished(const EgressApiResult &result);
	void OnStopFinished(const EgressApiResult &result);
	void OnStatusFinished(const EgressApiResult &result);

private:
	void SetState(EgressState state);
	void UpdateButtons();
	void ShowMessage(const char *localeKey);
	void ClearMessage();
	void ShowApiError(EgressApiError error, const char *fallbackLocaleKey);
	void ClearSession();

	EgressApiClient *apiClient_ = nullptr;

	QLabel *statusValueLabel_ = nullptr;
	QLabel *messageLabel_ = nullptr;
	QPushButton *startButton_ = nullptr;
	QPushButton *stopButton_ = nullptr;

	EgressState state_ = EgressState::Offline;

	QString serviceId_;
	QString egressId_;

	/* Set when a request failed in a way that leaves the server-side session
	 * possibly running. Keeps Stop reachable from the Error state. */
	bool serviceMayExist_ = false;
};
