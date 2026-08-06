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

#include "egress-controller.hpp"

#include <obs-module.h>

#include <QEventLoop>
#include <QTimer>

namespace {

/* Backend status strings that mean an Egress session is already running. */
bool StatusMeansLive(const QString &status)
{
	return status.compare("live", Qt::CaseInsensitive) == 0 ||
	       status.compare("active", Qt::CaseInsensitive) == 0 ||
	       status.compare("started", Qt::CaseInsensitive) == 0 ||
	       status.compare("running", Qt::CaseInsensitive) == 0;
}

const char *LocaleKeyForError(EgressApiError error, const char *fallbackLocaleKey)
{
	switch (error) {
	case EgressApiError::NotConfigured:
		return "Error.NotConfigured";
	case EgressApiError::InsecureUrl:
		return "Error.InsecureUrl";
	case EgressApiError::ConnectionFailed:
		return "Error.ConnectionFailed";
	case EgressApiError::Timeout:
		return "Error.Timeout";
	case EgressApiError::Unauthorized:
		return "Error.Unauthorized";
	case EgressApiError::NotFound:
		return "Error.NoActiveSession";
	case EgressApiError::ServerError:
		return "Error.ServerError";
	case EgressApiError::MalformedResponse:
		return "Error.MalformedResponse";
	case EgressApiError::None:
	case EgressApiError::RequestFailed:
		break;
	}

	return fallbackLocaleKey;
}

} // namespace

EgressController::EgressController(QObject *parent) : QObject(parent)
{
	apiClient_ = new EgressApiClient(EgressConfig::Load(), this);

	connect(apiClient_, &EgressApiClient::StartFinished, this, &EgressController::OnStartFinished);
	connect(apiClient_, &EgressApiClient::StopFinished, this, &EgressController::OnStopFinished);
	connect(apiClient_, &EgressApiClient::StatusFinished, this, &EgressController::OnStatusFinished);

	if (apiClient_->Config().IsConfigured()) {
		SetMessage("Message.Ready");
	} else {
		SetMessage("Error.NotConfigured");
	}
}

bool EgressController::IsConfigured() const
{
	return apiClient_->Config().IsConfigured();
}

bool EgressController::StartAvailable() const
{
	return state_ == EgressState::Offline || state_ == EgressState::Error;
}

bool EgressController::StopAvailable() const
{
	switch (state_) {
	case EgressState::Offline:
	case EgressState::Stopping:
		return false;
	case EgressState::Starting:
	case EgressState::Live:
		return true;
	case EgressState::Error:
		/* Only offer Stop when a session may still be running server-side. */
		return serviceMayExist_;
	}

	return false;
}

void EgressController::Start()
{
	if (state_ == EgressState::Starting || state_ == EgressState::Stopping) {
		return;
	}

	serviceMayExist_ = false;
	ClearMessage();
	SetState(EgressState::Starting);

	apiClient_->RequestStart();
}

void EgressController::Stop()
{
	if (state_ == EgressState::Stopping) {
		return;
	}

	/* Cancelling an in-progress Start: drop the pending request so its late
	 * response cannot move the state back to Live. */
	if (apiClient_->HasPendingRequest()) {
		apiClient_->AbortPending();
	}

	ClearMessage();
	SetState(EgressState::Stopping);

	apiClient_->RequestStop(serviceId_);
}

void EgressController::QueryInitialStatus()
{
	if (!apiClient_->Config().IsConfigured() || !apiClient_->Config().queryStatusOnStartup) {
		return;
	}

	apiClient_->RequestStatus();
}

void EgressController::HandleExit()
{
	/* The server is the source of truth: a running Egress session is left alone
	 * unless the user opted into stopping it. */
	if (!apiClient_->Config().stopEgressOnExit) {
		apiClient_->AbortPending();
		return;
	}

	if (state_ != EgressState::Live && state_ != EgressState::Starting) {
		apiClient_->AbortPending();
		return;
	}

	blog(LOG_INFO, "[egress-control] stop_egress_on_exit is enabled, stopping Egress before shutdown");

	apiClient_->AbortPending();

	/* OBS is already tearing down, so there is no interactive UI left to keep
	 * responsive. The wait is bounded by the configured request timeout to
	 * guarantee shutdown still completes. */
	QEventLoop loop;
	connect(apiClient_, &EgressApiClient::StopFinished, &loop, [&loop](const EgressApiResult &) {
		loop.quit();
	});

	QTimer::singleShot(apiClient_->Config().requestTimeoutMs, &loop, &QEventLoop::quit);

	apiClient_->RequestStop(serviceId_);
	loop.exec();

	apiClient_->AbortPending();
}

void EgressController::OnStartFinished(const EgressApiResult &result)
{
	/* A Stop issued while Start was in flight already moved the state on. */
	if (state_ != EgressState::Starting) {
		return;
	}

	if (result.success) {
		serviceId_ = result.serviceId;
		egressId_ = result.egressId;
		serviceMayExist_ = true;

		ClearMessage();
		SetState(EgressState::Live);

		blog(LOG_INFO, "[egress-control] Egress started successfully");
		return;
	}

	/* Keep any identifier the backend managed to return so Stop can target it. */
	if (!result.serviceId.isEmpty()) {
		serviceId_ = result.serviceId;
	}

	serviceMayExist_ = result.outcomeUnknown || !serviceId_.isEmpty();

	if (result.outcomeUnknown) {
		SetMessage("Error.MayStillBeActive");
	} else {
		SetApiErrorMessage(result.error, "Error.StartFailed");
	}

	SetState(EgressState::Error);
}

void EgressController::OnStopFinished(const EgressApiResult &result)
{
	if (state_ != EgressState::Stopping) {
		return;
	}

	/* Stop is idempotent: the backend reporting no active session is the state
	 * the user asked for, so it is not surfaced as a failure. */
	if (result.success || result.error == EgressApiError::NotFound) {
		ClearSession();

		if (result.error == EgressApiError::NotFound) {
			SetMessage("Error.NoActiveSession");
		} else {
			SetMessage("Message.Ready");
			blog(LOG_INFO, "[egress-control] Egress stopped successfully");
		}

		SetState(EgressState::Offline);
		return;
	}

	serviceMayExist_ = true;

	if (result.outcomeUnknown) {
		SetMessage("Error.MayStillBeActive");
	} else {
		SetApiErrorMessage(result.error, "Error.StopFailed");
	}

	SetState(EgressState::Error);
}

void EgressController::OnStatusFinished(const EgressApiResult &result)
{
	/* Startup probe only: it must never interrupt an operation the user
	 * started in the meantime. */
	if (state_ != EgressState::Offline) {
		return;
	}

	/* A failed probe is not worth an error state; the dock simply stays
	 * Offline until the user acts. */
	if (!result.success || !StatusMeansLive(result.status)) {
		return;
	}

	serviceId_ = result.serviceId;
	egressId_ = result.egressId;
	serviceMayExist_ = true;

	SetMessage("Message.ExistingSession");
	SetState(EgressState::Live);
}

void EgressController::SetState(EgressState state)
{
	state_ = state;

	emit Changed();
}

void EgressController::SetMessage(const char *localeKey)
{
	messageText_ = QString::fromUtf8(obs_module_text(localeKey));

	emit Changed();
}

void EgressController::ClearMessage()
{
	messageText_.clear();

	emit Changed();
}

void EgressController::SetApiErrorMessage(EgressApiError error, const char *fallbackLocaleKey)
{
	SetMessage(LocaleKeyForError(error, fallbackLocaleKey));
}

void EgressController::ClearSession()
{
	serviceId_.clear();
	egressId_.clear();
	serviceMayExist_ = false;
}
