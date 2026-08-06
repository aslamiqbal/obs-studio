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
#include <util/config-file.h>

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

#include "egress-controller.hpp"
#include "egress-state.hpp"
#include "nvs-identity.hpp"
#include "nvs-startup.hpp"
#include "program-preview-widget.hpp"

namespace {

/* Preset services (Twitch, YouTube, Facebook Live, ...) live in rtmp_common;
 * a hand-entered RTMP endpoint is a different service type entirely. */
constexpr const char *COMMON_SERVICE_ID = "rtmp_common";
constexpr const char *CUSTOM_SERVICE_ID = "rtmp_custom";

} // namespace

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

	/* Row 1: live controls — what an operator touches mid-broadcast. */
	QHBoxLayout *streamRow = new QHBoxLayout();

	sceneSelector_ = new QComboBox(this);
	sceneSelector_->setMinimumWidth(160);

	streamButton_ = new QPushButton(this);
	streamStatusLabel_ = new QLabel(this);

	QFont statusFont = streamStatusLabel_->font();
	statusFont.setBold(true);
	streamStatusLabel_->setFont(statusFont);

	streamRow->addWidget(new QLabel(obs_module_text("Scene"), this));
	streamRow->addWidget(sceneSelector_);
	streamRow->addSpacing(12);
	streamRow->addWidget(streamButton_);
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

	/* Configuration below the live controls: set once, rarely touched. */
	mainLayout->addWidget(BuildStreamGroup());

	QGroupBox *destinationsGroup = new QGroupBox(obs_module_text("Destinations"), this);
	QVBoxLayout *destinationsLayout = new QVBoxLayout(destinationsGroup);

	QHBoxLayout *youtubeRow = new QHBoxLayout();
	youtubeCheck_ = new QCheckBox("Youtube", this);
	youtubeCheck_->setMinimumWidth(100);
	youtubeLiveTokenEdit_ = new QLineEdit(this);
	youtubeLiveTokenEdit_->setPlaceholderText("youtubeLiveToken");
	youtubeRow->addWidget(youtubeCheck_);
	youtubeRow->addWidget(youtubeLiveTokenEdit_, 1);
	destinationsLayout->addLayout(youtubeRow);

	QHBoxLayout *facebookRow = new QHBoxLayout();
	facebookCheck_ = new QCheckBox("Facebook", this);
	facebookCheck_->setMinimumWidth(100);
	facebookLiveTokenEdit_ = new QLineEdit(this);
	facebookLiveTokenEdit_->setPlaceholderText("facebookLiveToken");
	facebookRow->addWidget(facebookCheck_);
	facebookRow->addWidget(facebookLiveTokenEdit_, 1);
	destinationsLayout->addLayout(facebookRow);

	mainLayout->addWidget(destinationsGroup);

	/* Bottom row: account and desktop integration. */
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
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		/* Every module is registered by now, so the service lists can be
		 * built and the current destination read back. */
		InitStreamSettings();
		break;
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

QWidget *StreamConsoleWindow::BuildStreamGroup()
{
	QGroupBox *group = new QGroupBox(obs_module_text("StreamSettings"), this);
	QVBoxLayout *outer = new QVBoxLayout(group);

	QFormLayout *form = new QFormLayout();

	serviceCombo_ = new QComboBox(this);
	serverCombo_ = new QComboBox(this);

	customServerEdit_ = new QLineEdit(this);
	customServerEdit_->setPlaceholderText("rtmp://");

	/* One row holds either the preset server list or a free-text URL; only the
	 * one that applies to the selected service is visible. */
	QWidget *serverRow = new QWidget(this);
	QHBoxLayout *serverLayout = new QHBoxLayout(serverRow);
	serverLayout->setContentsMargins(0, 0, 0, 0);
	serverLayout->addWidget(serverCombo_, 1);
	serverLayout->addWidget(customServerEdit_, 1);

	streamKeyEdit_ = new QLineEdit(this);
	streamKeyEdit_->setEchoMode(QLineEdit::Password);

	showKeyButton_ = new QPushButton(obs_module_text("StreamSettings.Show"), this);
	showKeyButton_->setCheckable(true);

	QWidget *keyRow = new QWidget(this);
	QHBoxLayout *keyLayout = new QHBoxLayout(keyRow);
	keyLayout->setContentsMargins(0, 0, 0, 0);
	keyLayout->addWidget(streamKeyEdit_, 1);
	keyLayout->addWidget(showKeyButton_);

	form->addRow(obs_module_text("StreamSettings.Service"), serviceCombo_);
	form->addRow(obs_module_text("StreamSettings.Server"), serverRow);
	form->addRow(obs_module_text("StreamSettings.StreamKey"), keyRow);

	outer->addLayout(form);

	ignoreRecommendedCheck_ = new QCheckBox(obs_module_text("StreamSettings.IgnoreRecommended"), this);
	applyStreamButton_ = new QPushButton(obs_module_text("StreamSettings.Apply"), this);

	QHBoxLayout *actionRow = new QHBoxLayout();
	actionRow->addWidget(ignoreRecommendedCheck_);
	actionRow->addStretch(1);
	actionRow->addWidget(applyStreamButton_);
	outer->addLayout(actionRow);

	recommendationsLabel_ = new QLabel(this);
	recommendationsLabel_->setWordWrap(true);
	recommendationsLabel_->setStyleSheet("opacity: 0.7;");
	outer->addWidget(recommendationsLabel_);

	streamSettingsNoticeLabel_ = new QLabel(this);
	streamSettingsNoticeLabel_->setWordWrap(true);
	outer->addWidget(streamSettingsNoticeLabel_);

	connect(serviceCombo_, &QComboBox::currentIndexChanged, this, &StreamConsoleWindow::OnServiceSelected);
	connect(showKeyButton_, &QPushButton::clicked, this, &StreamConsoleWindow::OnToggleKeyVisibility);
	connect(applyStreamButton_, &QPushButton::clicked, this, &StreamConsoleWindow::OnApplyStreamSettings);

	/* The lists are NOT filled here. This runs during obs_module_load(), and
	 * modules load alphabetically — egress-control before rtmp-services — so
	 * "rtmp_common" is not registered yet and every list would come back
	 * empty. InitStreamSettings() does it once loading has finished. */

	return group;
}

void StreamConsoleWindow::InitStreamSettings()
{
	PopulateServices();
	LoadCurrentService();
}

bool StreamConsoleWindow::IsCustomServiceSelected() const
{
	return serviceCombo_->currentData().toString() == QLatin1String(CUSTOM_SERVICE_ID);
}

void StreamConsoleWindow::PopulateServices()
{
	updatingServiceLists_ = true;

	serviceCombo_->clear();

	/* rtmp_common builds its service list inside the "show_all" modified
	 * callback, not in its properties constructor, so the list stays empty
	 * until that callback is fired. This mirrors what the OBS settings page
	 * does — anything else yields an empty dropdown. */
	OBSProperties props = obs_get_service_properties(COMMON_SERVICE_ID);

	if (props) {
		OBSDataAutoRelease settings = obs_data_create();
		obs_data_set_bool(settings, "show_all", false);

		obs_property_t *showAll = obs_properties_get(props, "show_all");

		if (showAll) {
			obs_property_modified(showAll, settings);
		}

		obs_property_t *serviceProp = obs_properties_get(props, "service");

		if (serviceProp) {
			const size_t count = obs_property_list_item_count(serviceProp);

			for (size_t i = 0; i < count; i++) {
				const char *name = obs_property_list_item_string(serviceProp, i);

				if (name && *name) {
					serviceCombo_->addItem(QString::fromUtf8(name),
							       QLatin1String(COMMON_SERVICE_ID));
				}
			}
		}
	}

	/* Custom RTMP is a different service type, not an entry in that list. */
	serviceCombo_->addItem(obs_module_text("StreamSettings.Custom"), QLatin1String(CUSTOM_SERVICE_ID));

	updatingServiceLists_ = false;
}

void StreamConsoleWindow::PopulateServersFor(const QString &serviceName)
{
	updatingServiceLists_ = true;

	serverCombo_->clear();

	/* Servers are filled by the "service" modified callback, keyed on the
	 * selected service name. */
	OBSProperties props = obs_get_service_properties(COMMON_SERVICE_ID);

	if (props) {
		OBSDataAutoRelease settings = obs_data_create();
		obs_data_set_string(settings, "service", serviceName.toUtf8().constData());

		obs_property_t *serviceProp = obs_properties_get(props, "service");

		if (serviceProp) {
			obs_property_modified(serviceProp, settings);
		}

		obs_property_t *serverProp = obs_properties_get(props, "server");

		if (serverProp) {
			const size_t count = obs_property_list_item_count(serverProp);

			for (size_t i = 0; i < count; i++) {
				const char *name = obs_property_list_item_name(serverProp, i);
				const char *url = obs_property_list_item_string(serverProp, i);

				if (url && *url) {
					serverCombo_->addItem(QString::fromUtf8(name ? name : url),
							      QString::fromUtf8(url));
				}
			}
		}
	}

	updatingServiceLists_ = false;
}

void StreamConsoleWindow::LoadCurrentService()
{
	OBSService service = obs_frontend_get_streaming_service();

	if (!service) {
		return;
	}

	const char *type = obs_service_get_type(service);
	OBSDataAutoRelease settings = obs_service_get_settings(service);

	updatingServiceLists_ = true;

	if (type && strcmp(type, CUSTOM_SERVICE_ID) == 0) {
		const int index = serviceCombo_->findData(QLatin1String(CUSTOM_SERVICE_ID));
		serviceCombo_->setCurrentIndex(index >= 0 ? index : serviceCombo_->count() - 1);
		customServerEdit_->setText(QString::fromUtf8(obs_data_get_string(settings, "server")));
	} else {
		const QString serviceName = QString::fromUtf8(obs_data_get_string(settings, "service"));
		const int index = serviceCombo_->findText(serviceName);

		if (index >= 0) {
			serviceCombo_->setCurrentIndex(index);
		}

		PopulateServersFor(serviceName);

		const QString server = QString::fromUtf8(obs_data_get_string(settings, "server"));
		const int serverIndex = serverCombo_->findData(server);

		if (serverIndex >= 0) {
			serverCombo_->setCurrentIndex(serverIndex);
		}
	}

	streamKeyEdit_->setText(QString::fromUtf8(obs_data_get_string(settings, "key")));

	config_t *profile = obs_frontend_get_profile_config();

	if (profile) {
		ignoreRecommendedCheck_->setChecked(config_get_bool(profile, "Stream1", "IgnoreRecommended"));
	}

	updatingServiceLists_ = false;

	ApplyServerRowMode();
	RefreshRecommendations();
}

/* A preset service picks its server from a list; a custom endpoint is typed in.
 * Only the control that applies is shown, so the row never offers both. */
void StreamConsoleWindow::ApplyServerRowMode()
{
	const bool custom = IsCustomServiceSelected();

	serverCombo_->setVisible(!custom);
	customServerEdit_->setVisible(custom);
}

void StreamConsoleWindow::OnServiceSelected(int index)
{
	if (updatingServiceLists_ || index < 0) {
		return;
	}

	ApplyServerRowMode();

	if (!IsCustomServiceSelected()) {
		PopulateServersFor(serviceCombo_->currentText());
	}

	RefreshRecommendations();
}

void StreamConsoleWindow::OnToggleKeyVisibility()
{
	const bool visible = showKeyButton_->isChecked();

	streamKeyEdit_->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
	showKeyButton_->setText(obs_module_text(visible ? "StreamSettings.Hide" : "StreamSettings.Show"));
}

void StreamConsoleWindow::RefreshRecommendations()
{
	if (!recommendationsLabel_) {
		return;
	}

	/* Built from a throwaway service matching the current selection, so the
	 * limits shown are the ones that would actually apply. */
	OBSDataAutoRelease settings = obs_data_create();
	const bool custom = IsCustomServiceSelected();

	if (!custom) {
		obs_data_set_string(settings, "service", serviceCombo_->currentText().toUtf8().constData());
	}

	OBSServiceAutoRelease probe = obs_service_create_private(custom ? CUSTOM_SERVICE_ID : COMMON_SERVICE_ID,
								"nvs_probe", settings);

	if (!probe) {
		recommendationsLabel_->clear();
		return;
	}

	int videoBitrate = 0;
	int audioBitrate = 0;
	int fps = 0;

	obs_service_get_max_bitrate(probe, &videoBitrate, &audioBitrate);
	obs_service_get_max_fps(probe, &fps);

	QStringList parts;

	if (videoBitrate > 0) {
		parts << QString::fromUtf8(obs_module_text("StreamSettings.MaxVideo")).arg(videoBitrate);
	}
	if (audioBitrate > 0) {
		parts << QString::fromUtf8(obs_module_text("StreamSettings.MaxAudio")).arg(audioBitrate);
	}
	if (fps > 0) {
		parts << QString::fromUtf8(obs_module_text("StreamSettings.MaxFps")).arg(fps);
	}

	/* A service with no published limits (custom RTMP) simply has nothing to
	 * say here, which is different from the limits being zero. */
	recommendationsLabel_->setText(parts.isEmpty() ? QString::fromUtf8(obs_module_text("StreamSettings.NoLimits"))
						       : parts.join(QStringLiteral("  \xC2\xB7  ")));
}

void StreamConsoleWindow::OnApplyStreamSettings()
{
	/* A running output already holds its destination; changing it now would be
	 * silently ignored rather than applied. */
	if (obs_frontend_streaming_active()) {
		streamSettingsNoticeLabel_->setText(obs_module_text("StreamSettings.CannotEditWhileLive"));
		return;
	}

	const bool custom = IsCustomServiceSelected();

	OBSDataAutoRelease settings = obs_data_create();

	if (custom) {
		obs_data_set_string(settings, "server",
				    customServerEdit_->text().trimmed().toUtf8().constData());
	} else {
		obs_data_set_string(settings, "service", serviceCombo_->currentText().toUtf8().constData());
		obs_data_set_string(settings, "server",
				    serverCombo_->currentData().toString().toUtf8().constData());
	}

	obs_data_set_string(settings, "key", streamKeyEdit_->text().toUtf8().constData());

	OBSServiceAutoRelease service = obs_service_create(custom ? CUSTOM_SERVICE_ID : COMMON_SERVICE_ID,
							  "default_service", settings, nullptr);

	if (!service) {
		streamSettingsNoticeLabel_->setText(obs_module_text("StreamSettings.SaveFailed"));
		return;
	}

	obs_frontend_set_streaming_service(service);
	obs_frontend_save_streaming_service();

	config_t *profile = obs_frontend_get_profile_config();

	if (profile) {
		config_set_bool(profile, "Stream1", "IgnoreRecommended", ignoreRecommendedCheck_->isChecked());
		config_save(profile);
	}

	/* Service name and server are safe to log; the stream key never is. */
	blog(LOG_INFO, "[nvs] stream destination updated (%s)",
	     custom ? "custom" : serviceCombo_->currentText().toUtf8().constData());

	streamSettingsNoticeLabel_->setText(obs_module_text("StreamSettings.Saved"));
	RefreshRecommendations();
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

	/* The destination cannot change under a running output, so the whole
	 * editor is locked rather than letting an edit look like it took effect. */
	if (applyStreamButton_) {
		applyStreamButton_->setEnabled(!active);
		serviceCombo_->setEnabled(!active);
		serverCombo_->setEnabled(!active);
		customServerEdit_->setEnabled(!active);
		streamKeyEdit_->setEnabled(!active);
		ignoreRecommendedCheck_->setEnabled(!active);

		if (active) {
			streamSettingsNoticeLabel_->setText(obs_module_text("StreamSettings.CannotEditWhileLive"));
		} else {
			streamSettingsNoticeLabel_->clear();
		}
	}
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
