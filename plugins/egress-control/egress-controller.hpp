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
#include <QString>

#include "egress-api-client.hpp"
#include "egress-state.hpp"

/* Single owner of the Egress session state.
 *
 * The dock and the stream console are both views onto this object, so there is
 * exactly one API client and one state machine no matter how many widgets are
 * showing Egress controls.
 */
class EgressController : public QObject {
	Q_OBJECT

public:
	explicit EgressController(EgressConfig config, QObject *parent = nullptr);

	EgressState State() const { return state_; }
	QString MessageText() const { return messageText_; }

	bool StartAvailable() const;
	bool StopAvailable() const;
	bool IsConfigured() const;

	void Start();
	void Stop();

	/* Platforms the backend should broadcast to on the next Start. */
	void SetDestinations(const QList<EgressDestination> &destinations);

	void QueryInitialStatus();
	void HandleExit();

signals:
	/* Emitted whenever state or message text changes; views re-read and
	 * repaint themselves. */
	void Changed();

private slots:
	void OnStartFinished(const EgressApiResult &result);
	void OnStopFinished(const EgressApiResult &result);
	void OnStatusFinished(const EgressApiResult &result);

private:
	void SetState(EgressState state);
	void SetMessage(const char *localeKey);
	void ClearMessage();
	void SetApiErrorMessage(EgressApiError error, const char *fallbackLocaleKey);
	void ClearSession();

	EgressApiClient *apiClient_ = nullptr;

	EgressState state_ = EgressState::Offline;
	QString messageText_;

	QString serviceId_;
	QString egressId_;

	/* Set when a request failed in a way that leaves the server-side session
	 * possibly running. Keeps Stop reachable from the Error state. */
	bool serviceMayExist_ = false;
};
