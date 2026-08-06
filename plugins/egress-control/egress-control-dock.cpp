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

#include "egress-control-dock.hpp"

#include <obs-module.h>

#include <QEventLoop>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

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

EgressControlDock::EgressControlDock(QWidget *parent) : QWidget(parent)
{
	setObjectName("egressControlDockContents");

	apiClient_ = new EgressApiClient(EgressConfig::Load(), this);

	QVBoxLayout *mainLayout = new QVBoxLayout(this);

	QHBoxLayout *statusLayout = new QHBoxLayout();

	QLabel *statusCaptionLabel = new QLabel(obs_module_text("Status"), this);
	statusValueLabel_ = new QLabel(this);

	QFont statusFont = statusValueLabel_->font();
	statusFont.setBold(true);
	statusValueLabel_->setFont(statusFont);

	statusLayout->addWidget(statusCaptionLabel);
	statusLayout->addWidget(statusValueLabel_, 1);

	mainLayout->addLayout(statusLayout);

	startButton_ = new QPushButton(obs_module_text("StartService"), this);
	stopButton_ = new QPushButton(obs_module_text("StopService"), this);

	mainLayout->addWidget(startButton_);
	mainLayout->addWidget(stopButton_);

	messageLabel_ = new QLabel(this);
	messageLabel_->setWordWrap(true);
	messageLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	mainLayout->addWidget(messageLabel_);

	mainLayout->addStretch(1);

	connect(startButton_, &QPushButton::clicked, this, &EgressControlDock::OnStartClicked);
	connect(stopButton_, &QPushButton::clicked, this, &EgressControlDock::OnStopClicked);

	connect(apiClient_, &EgressApiClient::StartFinished, this, &EgressControlDock::OnStartFinished);
	connect(apiClient_, &EgressApiClient::StopFinished, this, &EgressControlDock::OnStopFinished);
	connect(apiClient_, &EgressApiClient::StatusFinished, this, &EgressControlDock::OnStatusFinished);

	SetState(EgressState::Offline);

	if (!apiClient_->Config().IsConfigured()) {
		ShowMessage("Error.NotConfigured");
	} else {
		ShowMessage("Message.Ready");
	}
}

void EgressControlDock::QueryInitialStatus()
{
	if (!apiClient_->Config().IsConfigured() || !apiClient_->Config().queryStatusOnStartup) {
		return;
	}

	apiClient_->RequestStatus();
}

void EgressControlDock::HandleExit()
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

void EgressControlDock::OnStartClicked()
{
	if (state_ == EgressState::Starting || state_ == EgressState::Stopping) {
		return;
	}

	/* Disabled up front so a second click cannot queue a duplicate Start. */
	startButton_->setEnabled(false);

	serviceMayExist_ = false;
	ClearMessage();
	SetState(EgressState::Starting);

	apiClient_->RequestStart();
}

void EgressControlDock::OnStopClicked()
{
	if (state_ == EgressState::Stopping) {
		return;
	}

	stopButton_->setEnabled(false);

	/* Cancelling an in-progress Start: drop the pending request so its late
	 * response cannot move the dock back to Live. */
	if (apiClient_->HasPendingRequest()) {
		apiClient_->AbortPending();
	}

	ClearMessage();
	SetState(EgressState::Stopping);

	apiClient_->RequestStop(serviceId_);
}

void EgressControlDock::OnStartFinished(const EgressApiResult &result)
{
	/* A Stop issued while Start was in flight already moved the dock on. */
	if (state_ != EgressState::Starting) {
		return;
	}

	if (result.success) {
		serviceId_ = result.serviceId;
		egressId_ = result.egressId;
		serviceMayExist_ = true;

		SetState(EgressState::Live);
		ClearMessage();

		blog(LOG_INFO, "[egress-control] Egress started successfully");
		return;
	}

	/* Keep any identifier the backend managed to return so Stop can target it. */
	if (!result.serviceId.isEmpty()) {
		serviceId_ = result.serviceId;
	}

	serviceMayExist_ = result.outcomeUnknown || !serviceId_.isEmpty();

	SetState(EgressState::Error);

	if (result.outcomeUnknown) {
		ShowMessage("Error.MayStillBeActive");
	} else {
		ShowApiError(result.error, "Error.StartFailed");
	}
}

void EgressControlDock::OnStopFinished(const EgressApiResult &result)
{
	if (state_ != EgressState::Stopping) {
		return;
	}

	/* Stop is idempotent: the backend reporting no active session is the state
	 * the user asked for, so it is not surfaced as a failure. */
	if (result.success || result.error == EgressApiError::NotFound) {
		ClearSession();
		SetState(EgressState::Offline);

		if (result.error == EgressApiError::NotFound) {
			ShowMessage("Error.NoActiveSession");
		} else {
			ShowMessage("Message.Ready");
			blog(LOG_INFO, "[egress-control] Egress stopped successfully");
		}

		return;
	}

	serviceMayExist_ = true;
	SetState(EgressState::Error);

	if (result.outcomeUnknown) {
		ShowMessage("Error.MayStillBeActive");
	} else {
		ShowApiError(result.error, "Error.StopFailed");
	}
}

void EgressControlDock::OnStatusFinished(const EgressApiResult &result)
{
	/* Startup probe only: it must never interrupt an operation the user
	 * started in the meantime. */
	if (state_ != EgressState::Offline) {
		return;
	}

	if (!result.success) {
		/* A failed probe is not worth an error state; the dock simply stays
		 * Offline until the user acts. */
		return;
	}

	if (!StatusMeansLive(result.status)) {
		return;
	}

	serviceId_ = result.serviceId;
	egressId_ = result.egressId;
	serviceMayExist_ = true;

	SetState(EgressState::Live);
	ShowMessage("Message.ExistingSession");
}

void EgressControlDock::SetState(EgressState state)
{
	state_ = state;

	statusValueLabel_->setText(obs_module_text(EgressStateLocaleKey(state)));

	UpdateButtons();
}

void EgressControlDock::UpdateButtons()
{
	switch (state_) {
	case EgressState::Offline:
		startButton_->setEnabled(true);
		stopButton_->setEnabled(false);
		break;
	case EgressState::Starting:
		startButton_->setEnabled(false);
		/* Enabled so an in-progress start can be cancelled. */
		stopButton_->setEnabled(true);
		break;
	case EgressState::Live:
		startButton_->setEnabled(false);
		stopButton_->setEnabled(true);
		break;
	case EgressState::Stopping:
		startButton_->setEnabled(false);
		stopButton_->setEnabled(false);
		break;
	case EgressState::Error:
		startButton_->setEnabled(true);
		/* Only offer Stop when a session may still be running server-side. */
		stopButton_->setEnabled(serviceMayExist_);
		break;
	}
}

void EgressControlDock::ShowMessage(const char *localeKey)
{
	messageLabel_->setText(obs_module_text(localeKey));
}

void EgressControlDock::ClearMessage()
{
	messageLabel_->clear();
}

void EgressControlDock::ShowApiError(EgressApiError error, const char *fallbackLocaleKey)
{
	ShowMessage(LocaleKeyForError(error, fallbackLocaleKey));
}

void EgressControlDock::ClearSession()
{
	serviceId_.clear();
	egressId_.clear();
	serviceMayExist_ = false;
}
