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

#include "stream-console-window.hpp"

#include <obs-module.h>
#include <obs.hpp>

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "egress-controller.hpp"
#include "egress-state.hpp"
#include "nvs-identity.hpp"
#include "nvs-startup.hpp"
#include "program-preview-widget.hpp"
#include "stream-settings-dialog.hpp"

StreamConsoleWindow::StreamConsoleWindow(EgressController *controller, NvsIdentity *identity, QWidget *parent)
	: QWidget(parent, Qt::Window),
	  controller_(controller),
	  identity_(identity)
{
	setObjectName("streamConsoleWindow");
	setWindowTitle(obs_module_text("StreamConsole"));
	resize(960, 640);

	QVBoxLayout *mainLayout = new QVBoxLayout(this);

	preview_ = new ProgramPreviewWidget(this);
	mainLayout->addWidget(preview_, 1);

	QFrame *separator = new QFrame(this);
	separator->setFrameShape(QFrame::HLine);
	separator->setFrameShadow(QFrame::Sunken);
	mainLayout->addWidget(separator);

	/* Row 1: scene selection and streaming. */
	QHBoxLayout *streamRow = new QHBoxLayout();

	sceneSelector_ = new QComboBox(this);
	sceneSelector_->setMinimumWidth(160);

	streamButton_ = new QPushButton(this);
	streamSettingsButton_ = new QPushButton(obs_module_text("StreamSettings"), this);
	streamStatusLabel_ = new QLabel(this);

	QFont statusFont = streamStatusLabel_->font();
	statusFont.setBold(true);
	streamStatusLabel_->setFont(statusFont);

	streamRow->addWidget(new QLabel(obs_module_text("Scene"), this));
	streamRow->addWidget(sceneSelector_);
	streamRow->addSpacing(12);
	streamRow->addWidget(streamButton_);
	streamRow->addWidget(streamSettingsButton_);
	streamRow->addSpacing(12);
	streamRow->addWidget(streamStatusLabel_);
	streamRow->addStretch(1);

	mainLayout->addLayout(streamRow);

	/* Row 2: Egress service, sharing state with the Egress Control dock. */
	QHBoxLayout *egressRow = new QHBoxLayout();

	egressStartButton_ = new QPushButton(obs_module_text("StartService"), this);
	egressStopButton_ = new QPushButton(obs_module_text("StopService"), this);
	egressStatusLabel_ = new QLabel(this);

	egressRow->addWidget(new QLabel(obs_module_text("EgressControl"), this));
	egressRow->addWidget(egressStartButton_);
	egressRow->addWidget(egressStopButton_);
	egressRow->addSpacing(12);
	egressRow->addWidget(egressStatusLabel_, 1);

	mainLayout->addLayout(egressRow);

	/* Row 3: Youtube destination */
	QHBoxLayout *youtubeRow = new QHBoxLayout();
	youtubeCheck_ = new QCheckBox("Youtube", this);
	youtubeCheck_->setMinimumWidth(100);
	youtubeLiveTokenEdit_ = new QLineEdit(this);
	youtubeLiveTokenEdit_->setPlaceholderText("youtubeLiveToken");
	youtubeRow->addWidget(youtubeCheck_);
	youtubeRow->addWidget(youtubeLiveTokenEdit_, 1);
	mainLayout->addLayout(youtubeRow);

	/* Row 4: Facebook destination */
	QHBoxLayout *facebookRow = new QHBoxLayout();
	facebookCheck_ = new QCheckBox("Facebook", this);
	facebookCheck_->setMinimumWidth(100);
	facebookLiveTokenEdit_ = new QLineEdit(this);
	facebookLiveTokenEdit_->setPlaceholderText("facebookLiveToken");
	facebookRow->addWidget(facebookCheck_);
	facebookRow->addWidget(facebookLiveTokenEdit_, 1);
	mainLayout->addLayout(facebookRow);

	/* Row 5: account and desktop integration. */
	QHBoxLayout *accountRow = new QHBoxLayout();

	signInButton_ = new QPushButton(this);
	accountLabel_ = new QLabel(this);
	startWithWindowsCheck_ = new QCheckBox(obs_module_text("StartWithWindows"), this);

	if (!NvsStartup::IsSupported()) {
		startWithWindowsCheck_->setEnabled(false);
		startWithWindowsCheck_->setToolTip(obs_module_text("StartWithWindows.Unsupported"));
	} else {
		startWithWindowsCheck_->setChecked(NvsStartup::IsEnabled());
	}

	accountRow->addWidget(signInButton_);
	accountRow->addWidget(accountLabel_, 1);
	accountRow->addWidget(startWithWindowsCheck_);

	mainLayout->addLayout(accountRow);

	connect(signInButton_, &QPushButton::clicked, this, &StreamConsoleWindow::OnSignInClicked);
	connect(startWithWindowsCheck_, &QCheckBox::toggled, this, &StreamConsoleWindow::OnStartupToggled);

	connect(identity_, &NvsIdentity::Changed, this, &StreamConsoleWindow::RefreshIdentity);
	connect(identity_, &NvsIdentity::SignInFailed, this, [this](const QString &message) {
		QMessageBox::warning(this, obs_module_text("SignIn"), message);
	});

	connect(streamButton_, &QPushButton::clicked, this, &StreamConsoleWindow::OnStreamButtonClicked);
	connect(streamSettingsButton_, &QPushButton::clicked, this, &StreamConsoleWindow::OnStreamSettingsClicked);
	connect(sceneSelector_, &QComboBox::currentIndexChanged, this, &StreamConsoleWindow::OnSceneSelected);

	connect(egressStartButton_, &QPushButton::clicked, this, [this]() {
		egressStartButton_->setEnabled(false);
		controller_->Start();
	});

	connect(egressStopButton_, &QPushButton::clicked, this, [this]() {
		egressStopButton_->setEnabled(false);
		controller_->Stop();
	});

	connect(controller_, &EgressController::Changed, this, &StreamConsoleWindow::RefreshEgress);

	RefreshSceneList();
	RefreshStreamingState();
	RefreshEgress();
	RefreshIdentity();
}

StreamConsoleWindow::~StreamConsoleWindow()
{
	ReleasePreview();
}

void StreamConsoleWindow::ReleasePreview()
{
	if (preview_) {
		preview_->DestroyDisplay();
	}
}

void StreamConsoleWindow::closeEvent(QCloseEvent *event)
{
	/* Closing the console only hides it: OBS keeps running and the window can
	 * be reopened from the Tools menu. */
	hide();
	event->ignore();
}

void StreamConsoleWindow::HandleFrontendEvent(enum obs_frontend_event event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		RefreshSceneList();
		break;
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
		RefreshCurrentScene();
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STARTING:
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
	case OBS_FRONTEND_EVENT_STREAMING_STOPPING:
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		RefreshStreamingState();
		break;
	default:
		break;
	}
}

void StreamConsoleWindow::OnStreamButtonClicked()
{
	/* Disabled until the matching frontend event arrives, so a double click
	 * cannot issue a second start or stop. */
	streamButton_->setEnabled(false);

	if (obs_frontend_streaming_active()) {
		blog(LOG_INFO, "[egress-control] stopping OBS streaming from the console");
		obs_frontend_streaming_stop();
	} else {
		blog(LOG_INFO, "[egress-control] starting OBS streaming from the console");
		obs_frontend_streaming_start();
	}
}

void StreamConsoleWindow::OnStreamSettingsClicked()
{
	StreamSettingsDialog dialog(this);
	dialog.exec();
}

void StreamConsoleWindow::OnSceneSelected(int index)
{
	if (updatingSceneList_ || index < 0) {
		return;
	}

	const QString sceneName = sceneSelector_->itemText(index);

	OBSSourceAutoRelease scene = obs_get_source_by_name(sceneName.toUtf8().constData());

	if (!scene) {
		return;
	}

	obs_frontend_set_current_scene(scene);
}

void StreamConsoleWindow::RefreshSceneList()
{
	updatingSceneList_ = true;

	sceneSelector_->clear();

	obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);

	for (size_t i = 0; i < scenes.sources.num; i++) {
		obs_source_t *scene = scenes.sources.array[i];
		const char *name = obs_source_get_name(scene);

		if (name) {
			sceneSelector_->addItem(QString::fromUtf8(name));
		}
	}

	obs_frontend_source_list_free(&scenes);

	updatingSceneList_ = false;

	RefreshCurrentScene();
}

void StreamConsoleWindow::RefreshCurrentScene()
{
	OBSSourceAutoRelease currentScene = obs_frontend_get_current_scene();

	if (!currentScene) {
		return;
	}

	const char *name = obs_source_get_name(currentScene);

	if (!name) {
		return;
	}

	const int index = sceneSelector_->findText(QString::fromUtf8(name));

	if (index < 0 || index == sceneSelector_->currentIndex()) {
		return;
	}

	updatingSceneList_ = true;
	sceneSelector_->setCurrentIndex(index);
	updatingSceneList_ = false;
}

void StreamConsoleWindow::RefreshStreamingState()
{
	const bool active = obs_frontend_streaming_active();

	streamButton_->setText(obs_module_text(active ? "StopStreaming" : "StartStreaming"));
	streamButton_->setEnabled(true);

	streamStatusLabel_->setText(obs_module_text(active ? "Stream.Live" : "Stream.Offline"));

	/* Changing the destination while live has no effect on the running output. */
	streamSettingsButton_->setEnabled(!active);
}

void StreamConsoleWindow::OnSignInClicked()
{
	if (identity_->IsSigningIn()) {
		identity_->CancelSignIn();
		return;
	}

	if (identity_->IsSignedIn()) {
		identity_->SignOut();
		return;
	}

	identity_->SignIn();
}

void StreamConsoleWindow::OnStartupToggled(bool checked)
{
	if (NvsStartup::SetEnabled(checked)) {
		return;
	}

	/* The registry write failed, so put the checkbox back where it was rather
	 * than leaving it claiming something untrue. */
	QSignalBlocker blocker(startWithWindowsCheck_);
	startWithWindowsCheck_->setChecked(NvsStartup::IsEnabled());

	QMessageBox::warning(this, obs_module_text("StartWithWindows"),
			     obs_module_text("StartWithWindows.Failed"));
}

void StreamConsoleWindow::RefreshIdentity()
{
	if (identity_->IsSigningIn()) {
		signInButton_->setText(obs_module_text("SignIn.Cancel"));
		accountLabel_->setText(obs_module_text("SignIn.WaitingForBrowser"));
		return;
	}

	if (identity_->IsSignedIn()) {
		signInButton_->setText(obs_module_text("SignOut"));
		accountLabel_->setText(identity_->DisplayName());
		return;
	}

	signInButton_->setText(obs_module_text("SignIn"));
	accountLabel_->setText(obs_module_text("SignIn.NotSignedIn"));
}

void StreamConsoleWindow::RefreshEgress()
{
	egressStartButton_->setEnabled(controller_->StartAvailable());
	egressStopButton_->setEnabled(controller_->StopAvailable());

	const QString state = QString::fromUtf8(obs_module_text(EgressStateLocaleKey(controller_->State())));
	const QString message = controller_->MessageText();

	egressStatusLabel_->setText(message.isEmpty() ? state : state + " \xE2\x80\x94 " + message);
}
