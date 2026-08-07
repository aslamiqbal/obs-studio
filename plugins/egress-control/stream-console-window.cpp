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
#include <QEvent>
#include <QScrollArea>
#include <QWheelEvent>
#include <QDesktopServices>
#include <QHeaderView>
#include <QPair>
#include <QStringList>
#include <QTableWidget>
#include <QVBoxLayout>

#include <QJsonObject>
#include <QSignalBlocker>

#include "egress-config.hpp"
#include "egress-controller.hpp"
#include "egress-state.hpp"
#include "nvs-credential-store.hpp"
#include "nvs-identity.hpp"
#include "nvs-multi-rtmp.hpp"
#include "nvs-room-client.hpp"
#include "nvs-startup.hpp"
#include "program-preview-widget.hpp"

namespace {

/* Preset services (Twitch, YouTube, Facebook Live, ...) live in rtmp_common;
 * a hand-entered RTMP endpoint is a different service type entirely. */
constexpr const char *COMMON_SERVICE_ID = "rtmp_common";
constexpr const char *CUSTOM_SERVICE_ID = "rtmp_custom";

/* Stops a combo box from swallowing wheel events it does not own.
 *
 * These live inside a scroll area, and by default scrolling the page while the
 * pointer happens to be over a combo silently changes the selection instead of
 * scrolling — which here would mean retargeting the console at a different room
 * or service without the operator noticing. */
class WheelGuard : public QObject {
public:
	using QObject::QObject;

protected:
	bool eventFilter(QObject *watched, QEvent *event) override
	{
		if (event->type() == QEvent::Wheel) {
			QWidget *widget = qobject_cast<QWidget *>(watched);

			/* Deliberate interaction still works: click it first. */
			if (widget && !widget->hasFocus()) {
				event->ignore();
				return true;
			}
		}

		return QObject::eventFilter(watched, event);
	}
};

void GuardWheel(QComboBox *combo, QObject *owner)
{
	combo->setFocusPolicy(Qt::StrongFocus);
	combo->installEventFilter(new WheelGuard(owner));
}

} // namespace

StreamConsoleWindow::StreamConsoleWindow(EgressController *controller, NvsIdentity *identity, QWidget *parent)
	: QWidget(parent, Qt::Window),
	  controller_(controller),
	  identity_(identity)
{
	setObjectName("streamConsoleWindow");
	setWindowTitle(obs_module_text("StreamConsole"));
	resize(980, 720);

	/* Created before the layout: the group builders connect to these. */
	const QString apiBaseUrl = EgressConfig::Load().baseUrl;
	roomClient_ = new NvsRoomClient(apiBaseUrl, identity_, this);
	targetClient_ = new NvsStreamTargetClient(apiBaseUrl, identity_, this);
	multiRtmp_ = new NvsMultiRtmp(this);

	QVBoxLayout *mainLayout = new QVBoxLayout(this);

	preview_ = new ProgramPreviewWidget(this);
	/* Weighted above the scrolled config so the picture stays the focus. */
	mainLayout->addWidget(preview_, 3);

	QFrame *separator = new QFrame(this);
	separator->setFrameShape(QFrame::HLine);
	separator->setFrameShadow(QFrame::Sunken);
	mainLayout->addWidget(separator);

	/* Row 1: scene selection. */
	QHBoxLayout *streamRow = new QHBoxLayout();

	sceneSelector_ = new QComboBox(this);
	sceneSelector_->setMinimumWidth(160);
	GuardWheel(sceneSelector_, this);

	streamStatusLabel_ = new QLabel(this);

	QFont statusFont = streamStatusLabel_->font();
	statusFont.setBold(true);
	streamStatusLabel_->setFont(statusFont);

	streamRow->addWidget(new QLabel(obs_module_text("Scene"), this));
	streamRow->addWidget(sceneSelector_);
	streamRow->addSpacing(12);
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

	/* Configuration below the live controls: set once, rarely touched.
	 *
	 * Scrolled rather than stacked: the three groups together are taller than
	 * a laptop screen, and without this the account row at the bottom becomes
	 * unreachable. The preview and live controls stay fixed above it. */
	QWidget *configWidget = new QWidget(this);
	QVBoxLayout *configLayout = new QVBoxLayout(configWidget);
	configLayout->setContentsMargins(0, 0, 0, 0);

	configLayout->addWidget(BuildRoomGroup());

	configLayout->addWidget(BuildDestinationsGroup());
	configLayout->addStretch(1);

	QScrollArea *configScroll = new QScrollArea(this);
	configScroll->setWidget(configWidget);
	configScroll->setWidgetResizable(true);
	configScroll->setFrameShape(QFrame::NoFrame);
	configScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	mainLayout->addWidget(configScroll, 2);

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

	connect(sceneSelector_, &QComboBox::currentIndexChanged, this, &StreamConsoleWindow::OnSceneSelected);

	/* Start/Stop drive the real stream targets. The controller still owns the
	 * displayed state, which the dock and tray also read. */
	connect(egressStartButton_, &QPushButton::clicked, this, [this]() {
		egressStartButton_->setEnabled(false);
		OnStartDestinationsClicked();
		controller_->Start();
	});

	connect(egressStopButton_, &QPushButton::clicked, this, [this]() {
		egressStopButton_->setEnabled(false);
		OnStopDestinationsClicked();
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

QWidget *StreamConsoleWindow::BuildRoomGroup()
{
	QGroupBox *group = new QGroupBox(obs_module_text("Room"), this);
	QVBoxLayout *outer = new QVBoxLayout(group);

	QFormLayout *form = new QFormLayout();

	roomSelector_ = new QComboBox(this);
	roomSelector_->setMinimumWidth(200);
	GuardWheel(roomSelector_, this);
	refreshRoomsButton_ = new QPushButton(obs_module_text("Room.Refresh"), this);

	QWidget *roomRow = new QWidget(this);
	QHBoxLayout *roomLayout = new QHBoxLayout(roomRow);
	roomLayout->setContentsMargins(0, 0, 0, 0);
	roomLayout->addWidget(roomSelector_, 1);
	roomLayout->addWidget(refreshRoomsButton_);

	roomAddressEdit_ = new QLineEdit(this);
	roomAddressEdit_->setPlaceholderText("https://nadavox.com/rooms/<code>/participant?egress=true");
	fetchJoinUrlButton_ = new QPushButton(obs_module_text("Room.Fetch"), this);

	QWidget *addressRow = new QWidget(this);
	QHBoxLayout *addressLayout = new QHBoxLayout(addressRow);
	addressLayout->setContentsMargins(0, 0, 0, 0);
	addressLayout->addWidget(roomAddressEdit_, 1);
	addressLayout->addWidget(fetchJoinUrlButton_);

	roomTokenEdit_ = new QLineEdit(this);
	/* The join token grants view access to the room until it expires, so it is
	 * masked like every other credential on this form. */
	roomTokenEdit_->setEchoMode(QLineEdit::Password);
	roomTokenEdit_->setPlaceholderText(obs_module_text("Room.TokenPlaceholder"));

	form->addRow(obs_module_text("Room.Room"), roomRow);
	form->addRow(obs_module_text("Room.Address"), addressRow);
	form->addRow(obs_module_text("Room.Token"), roomTokenEdit_);

	outer->addLayout(form);

	applyRoomButton_ = new QPushButton(obs_module_text("Room.ApplyToBrowserSource"), this);

	QHBoxLayout *actionRow = new QHBoxLayout();
	actionRow->addStretch(1);
	actionRow->addWidget(applyRoomButton_);
	outer->addLayout(actionRow);

	roomNoticeLabel_ = new QLabel(this);
	roomNoticeLabel_->setWordWrap(true);
	outer->addWidget(roomNoticeLabel_);

	connect(refreshRoomsButton_, &QPushButton::clicked, this, &StreamConsoleWindow::OnRefreshRoomsClicked);
	connect(fetchJoinUrlButton_, &QPushButton::clicked, this, &StreamConsoleWindow::OnFetchJoinUrlClicked);
	connect(applyRoomButton_, &QPushButton::clicked, this, &StreamConsoleWindow::OnApplyRoomToBrowserSource);

	connect(roomSelector_, &QComboBox::currentIndexChanged, this, [this](int) {
		SaveRoomSettings();
		/* Destinations belong to a room, so switching rooms reloads them
		 * rather than leaving the previous room's targets on screen. */
		serverTargets_.clear();
		targetClient_->FetchTargets(SelectedRoomId());
	});
	connect(roomAddressEdit_, &QLineEdit::editingFinished, this, [this]() { SaveRoomSettings(); });
	connect(roomTokenEdit_, &QLineEdit::editingFinished, this, [this]() { SaveRoomSettings(); });

	connect(roomClient_, &NvsRoomClient::RoomsFetched, this, &StreamConsoleWindow::OnRoomsFetched);
	connect(roomClient_, &NvsRoomClient::JoinUrlFetched, this, &StreamConsoleWindow::OnJoinUrlFetched);
	connect(roomClient_, &NvsRoomClient::Failed, this,
		[this](const QString &message) { roomNoticeLabel_->setText(message); });

	LoadRoomSettings();

	return group;
}

QString StreamConsoleWindow::SelectedRoomId() const
{
	return roomSelector_->currentData().toString();
}

void StreamConsoleWindow::OnRefreshRoomsClicked()
{
	roomNoticeLabel_->setText(obs_module_text("Room.Loading"));
	roomClient_->FetchRooms();
}

void StreamConsoleWindow::OnRoomsFetched(const QList<NvsRoomInfo> &rooms)
{
	/* Remember the current pick so refreshing does not silently retarget the
	 * console at a different room. */
	const QString previous = SelectedRoomId();

	const QSignalBlocker blocker(roomSelector_);
	roomSelector_->clear();

	for (const NvsRoomInfo &room : rooms) {
		const QString label = room.code.isEmpty() ? room.name : room.name + "  (" + room.code + ")";
		roomSelector_->addItem(label, room.id);
	}

	const int index = roomSelector_->findData(previous);

	if (index >= 0) {
		roomSelector_->setCurrentIndex(index);
	}

	roomNoticeLabel_->setText(rooms.isEmpty() ? obs_module_text("Room.None") : QString());

	SaveRoomSettings();
}

void StreamConsoleWindow::OnFetchJoinUrlClicked()
{
	const QString roomId = SelectedRoomId();

	if (roomId.isEmpty()) {
		roomNoticeLabel_->setText(obs_module_text("Room.SelectFirst"));
		return;
	}

	roomNoticeLabel_->setText(obs_module_text("Room.Fetching"));
	roomClient_->FetchJoinUrl(roomId);
}

void StreamConsoleWindow::OnJoinUrlFetched(const QString &address, const QString &token)
{
	const QSignalBlocker blockAddress(roomAddressEdit_);
	const QSignalBlocker blockToken(roomTokenEdit_);

	roomAddressEdit_->setText(address);
	roomTokenEdit_->setText(token);

	SaveRoomSettings();

	roomNoticeLabel_->setText(obs_module_text("Room.Fetched"));
}

void StreamConsoleWindow::OnApplyRoomToBrowserSource()
{
	const QString url = NvsRoomClient::Compose(roomAddressEdit_->text(), roomTokenEdit_->text());

	if (url.isEmpty()) {
		roomNoticeLabel_->setText(obs_module_text("Room.AddressRequired"));
		return;
	}

	OBSSourceAutoRelease sceneSource = obs_frontend_get_current_scene();
	obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;

	if (!scene) {
		roomNoticeLabel_->setText(obs_module_text("Room.NoBrowserSource"));
		return;
	}

	/* Applied to the first browser source in the current scene. Carrying the
	 * item out of the enumeration would need a reference, so the update is
	 * done inside the callback and the result reported afterwards. */
	struct ApplyContext {
		QByteArray url;
		bool applied = false;
		QString sourceName;
	} context;

	context.url = url.toUtf8();

	auto applyToItem = [](obs_scene_t *, obs_sceneitem_t *item, void *param) -> bool {
		ApplyContext *ctx = static_cast<ApplyContext *>(param);
		obs_source_t *source = obs_sceneitem_get_source(item);
		const char *id = source ? obs_source_get_id(source) : nullptr;

		if (!id || strcmp(id, "browser_source") != 0) {
			return true;
		}

		OBSDataAutoRelease settings = obs_data_create();
		obs_data_set_string(settings, "url", ctx->url.constData());
		/* The room's audio is the broadcast audio: route it through OBS
		 * rather than CEF's direct playback, which NVS never captures.
		 * Monitor-and-output keeps the operator hearing the room; the
		 * global capture channels are cleared, so it cannot loop back. */
		obs_data_set_bool(settings, "reroute_audio", true);
		obs_source_update(source, settings);

		obs_source_set_monitoring_type(source, OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT);
		obs_source_set_muted(source, false);

		ctx->applied = true;
		ctx->sourceName = QString::fromUtf8(obs_source_get_name(source));

		/* First one wins; a scene with several browser sources would
		 * otherwise all get pointed at the same room. */
		return false;
	};

	obs_scene_enum_items(scene, applyToItem, &context);

	if (!context.applied) {
		roomNoticeLabel_->setText(obs_module_text("Room.NoBrowserSource"));
		return;
	}

	/* The URL embeds the join token, so only the source name is logged. */
	blog(LOG_INFO, "[nvs] applied the room address to browser source '%s'",
	     context.sourceName.toUtf8().constData());

	roomNoticeLabel_->setText(
		QString::fromUtf8(obs_module_text("Room.Applied")).arg(context.sourceName));
}

void StreamConsoleWindow::LoadRoomSettings()
{
	const QJsonObject stored = NvsCredentialStore::Load(NvsCredentialStore::RoomFile);

	const QSignalBlocker blockSelector(roomSelector_);
	const QSignalBlocker blockAddress(roomAddressEdit_);
	const QSignalBlocker blockToken(roomTokenEdit_);

	const QString roomId = stored.value("room_id").toString();
	const QString roomLabel = stored.value("room_label").toString();

	/* The list is only available once signed in, so the remembered room is
	 * seeded as a single entry and replaced by the real list on refresh. */
	if (!roomId.isEmpty()) {
		roomSelector_->addItem(roomLabel.isEmpty() ? roomId : roomLabel, roomId);
	}

	roomAddressEdit_->setText(stored.value("address").toString());
	roomTokenEdit_->setText(stored.value("token").toString());
}

void StreamConsoleWindow::SaveRoomSettings() const
{
	QJsonObject payload;
	payload.insert("room_id", roomSelector_->currentData().toString());
	payload.insert("room_label", roomSelector_->currentText());
	payload.insert("address", roomAddressEdit_->text().trimmed());
	payload.insert("token", roomTokenEdit_->text().trimmed());

	/* Sealed: the token is a live view credential for the room. */
	NvsCredentialStore::Save(payload, NvsCredentialStore::RoomFile);
}
namespace {

/* Destination table columns. */
enum DestinationColumn {
	ColumnEnabled = 0,
	ColumnService = 1,
	ColumnServer = 2,
	ColumnKey = 3,
	ColumnRemove = 4,
};

constexpr const char *CUSTOM_SERVICE_LABEL = "Custom...";

/* Every service rtmp_common knows about, plus Custom.
 *
 * The list is built by a modified-callback rather than by the properties
 * constructor, so it stays empty unless that callback is fired â€” the same
 * mechanism the OBS settings page uses. */
QStringList ServiceNames()
{
	QStringList names;

	OBSProperties props = obs_get_service_properties(COMMON_SERVICE_ID);

	if (props) {
		OBSDataAutoRelease settings = obs_data_create();
		obs_data_set_bool(settings, "show_all", false);

		if (obs_property_t *showAll = obs_properties_get(props, "show_all")) {
			obs_property_modified(showAll, settings);
		}

		if (obs_property_t *service = obs_properties_get(props, "service")) {
			const size_t count = obs_property_list_item_count(service);

			for (size_t i = 0; i < count; i++) {
				const char *name = obs_property_list_item_string(service, i);

				if (name && *name) {
					names << QString::fromUtf8(name);
				}
			}
		}
	}

	names << QLatin1String(CUSTOM_SERVICE_LABEL);

	return names;
}

/* Ingest servers for one service, as (label, url) pairs. */
QList<QPair<QString, QString>> ServersFor(const QString &serviceName)
{
	QList<QPair<QString, QString>> servers;

	OBSProperties props = obs_get_service_properties(COMMON_SERVICE_ID);

	if (!props) {
		return servers;
	}

	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "service", serviceName.toUtf8().constData());

	if (obs_property_t *service = obs_properties_get(props, "service")) {
		obs_property_modified(service, settings);
	}

	if (obs_property_t *server = obs_properties_get(props, "server")) {
		const size_t count = obs_property_list_item_count(server);

		for (size_t i = 0; i < count; i++) {
			const char *label = obs_property_list_item_name(server, i);
			const char *url = obs_property_list_item_string(server, i);

			if (url && *url) {
				servers.append({QString::fromUtf8(label ? label : url), QString::fromUtf8(url)});
			}
		}
	}

	return servers;
}

/* Where this service tells an operator to fetch their key. Empty for services
 * that publish none â€” YouTube among them, which is why the button is hidden
 * rather than pointed somewhere generic. */
QString StreamKeyLinkFor(const QString &serviceName)
{
	OBSProperties props = obs_get_service_properties(COMMON_SERVICE_ID);

	if (!props) {
		return {};
	}

	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "service", serviceName.toUtf8().constData());

	if (obs_property_t *service = obs_properties_get(props, "service")) {
		obs_property_modified(service, settings);
	}

	return QString::fromUtf8(obs_data_get_string(settings, "stream_key_link"));
}

} // namespace

QWidget *StreamConsoleWindow::BuildDestinationsGroup()
{
	QGroupBox *group = new QGroupBox(obs_module_text("Destinations"), this);
	QVBoxLayout *outer = new QVBoxLayout(group);

	/* A room may broadcast to several endpoints on the same platform, so this
	 * is a list and every row carries its own service, server and key. */
	destinationsTable_ = new QTableWidget(0, 5, this);
	destinationsTable_->setHorizontalHeaderLabels(
		{obs_module_text("Destinations.On"), obs_module_text("Destinations.Service"),
		 obs_module_text("Destinations.Server"), obs_module_text("Destinations.Token"), QString()});

	destinationsTable_->verticalHeader()->setVisible(false);
	destinationsTable_->setSelectionMode(QAbstractItemView::NoSelection);
	destinationsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
	destinationsTable_->horizontalHeader()->setSectionResizeMode(ColumnServer, QHeaderView::Stretch);
	destinationsTable_->horizontalHeader()->setSectionResizeMode(ColumnKey, QHeaderView::Stretch);
	destinationsTable_->setColumnWidth(ColumnEnabled, 40);
	destinationsTable_->setColumnWidth(ColumnService, 150);
	destinationsTable_->setColumnWidth(ColumnRemove, 34);
	destinationsTable_->setMinimumHeight(150);
	destinationsTable_->verticalHeader()->setDefaultSectionSize(32);

	outer->addWidget(destinationsTable_);

	QPushButton *addButton = new QPushButton(obs_module_text("Destinations.Add"), this);
	pushDestinationsButton_ = new QPushButton(obs_module_text("Destinations.Push"), this);

	QHBoxLayout *actionRow = new QHBoxLayout();
	actionRow->addWidget(addButton);
	actionRow->addStretch(1);
	actionRow->addWidget(pushDestinationsButton_);
	outer->addLayout(actionRow);

	destinationsNoticeLabel_ = new QLabel(this);
	destinationsNoticeLabel_->setWordWrap(true);
	outer->addWidget(destinationsNoticeLabel_);

	connect(addButton, &QPushButton::clicked, this, [this]() { AddDestinationRow(NvsStreamTarget()); });

	connect(pushDestinationsButton_, &QPushButton::clicked, this,
		&StreamConsoleWindow::OnPushDestinationsClicked);

	connect(targetClient_, &NvsStreamTargetClient::TargetsFetched, this,
		&StreamConsoleWindow::OnTargetsFetched);

	connect(targetClient_, &NvsStreamTargetClient::TargetSaved, this, [this](const QString &platform) {
		destinationsNoticeLabel_->setText(
			QString::fromUtf8(obs_module_text("Targets.Saved")).arg(platform));
		targetClient_->FetchTargets(SelectedRoomId());
	});

	connect(targetClient_, &NvsStreamTargetClient::TargetDeleted, this,
		[this](const QString &) { targetClient_->FetchTargets(SelectedRoomId()); });

	connect(targetClient_, &NvsStreamTargetClient::Failed, this,
		[this](const QString &message) { destinationsNoticeLabel_->setText(message); });

	return group;
}

void StreamConsoleWindow::AddDestinationRow(const NvsStreamTarget &target)
{
	const int row = destinationsTable_->rowCount();
	destinationsTable_->insertRow(row);

	QTableWidgetItem *enabled = new QTableWidgetItem();
	enabled->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
	enabled->setCheckState(target.enabled ? Qt::Checked : Qt::Unchecked);
	/* The server id rides on the row so a save knows create from update. */
	enabled->setData(Qt::UserRole, target.id);
	destinationsTable_->setItem(row, ColumnEnabled, enabled);

	QComboBox *service = new QComboBox(this);
	service->addItems(ServiceNames());
	GuardWheel(service, this);
	destinationsTable_->setCellWidget(row, ColumnService, service);

	/* Editable so "Custom..." can take a hand-typed endpoint in the same cell
	 * a preset would use a list for. */
	QComboBox *server = new QComboBox(this);
	server->setEditable(true);
	GuardWheel(server, this);
	destinationsTable_->setCellWidget(row, ColumnServer, server);

	QWidget *keyCell = new QWidget(this);
	QHBoxLayout *keyLayout = new QHBoxLayout(keyCell);
	keyLayout->setContentsMargins(0, 0, 0, 0);
	keyLayout->setSpacing(4);

	QLineEdit *key = new QLineEdit(this);
	key->setEchoMode(QLineEdit::Password);
	key->setPlaceholderText(target.id.isEmpty() ? obs_module_text("Destinations.TokenNew")
						    : obs_module_text("Destinations.TokenKeep"));

	QPushButton *show = new QPushButton(obs_module_text("StreamSettings.Show"), this);
	show->setCheckable(true);
	show->setFixedWidth(52);

	QPushButton *getKey = new QPushButton(obs_module_text("Destinations.GetStreamKey"), this);
	getKey->setFixedWidth(110);

	keyLayout->addWidget(key, 1);
	keyLayout->addWidget(show);
	keyLayout->addWidget(getKey);

	destinationsTable_->setCellWidget(row, ColumnKey, keyCell);

	QPushButton *remove = new QPushButton(QStringLiteral("\xE2\x9C\x95"), this);
	remove->setFlat(true);
	remove->setToolTip(obs_module_text("Destinations.Remove"));
	destinationsTable_->setCellWidget(row, ColumnRemove, remove);

	connect(show, &QPushButton::clicked, this, [key, show]() {
		const bool visible = show->isChecked();
		key->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
		show->setText(obs_module_text(visible ? "StreamSettings.Hide" : "StreamSettings.Show"));
	});

	/* Repopulating the servers and re-targeting the key link are the same
	 * event, so they are handled together. */
	auto onServiceChanged = [this, service, server, getKey]() {
		const QString name = service->currentText();
		const bool custom = name == QLatin1String(CUSTOM_SERVICE_LABEL);

		const QSignalBlocker blocker(server);
		const QString previous = server->currentText();

		server->clear();

		if (!custom) {
			for (const QPair<QString, QString> &entry : ServersFor(name)) {
				server->addItem(entry.first, entry.second);
			}
		} else {
			server->setEditText(previous);
		}

		/* Only some services publish a page to fetch a key from; YouTube
		 * does not, so the button is hidden rather than left dead. */
		const QString link = custom ? QString() : StreamKeyLinkFor(name);

		getKey->setVisible(!link.isEmpty());
		getKey->setProperty("nvsKeyLink", link);
	};

	connect(service, &QComboBox::currentTextChanged, this, [onServiceChanged]() { onServiceChanged(); });

	connect(getKey, &QPushButton::clicked, this, [getKey]() {
		const QString link = getKey->property("nvsKeyLink").toString();

		if (!link.isEmpty()) {
			QDesktopServices::openUrl(QUrl(link));
		}
	});

	connect(remove, &QPushButton::clicked, this, [this, remove]() {
		/* The row index shifts as rows are removed, so it is resolved from
		 * the button that was actually clicked. */
		for (int i = 0; i < destinationsTable_->rowCount(); i++) {
			if (destinationsTable_->cellWidget(i, ColumnRemove) == remove) {
				RemoveDestinationRow(i);
				return;
			}
		}
	});

	/* Restore the saved service, or default to the first entry. */
	const int serviceIndex = service->findText(target.platformType);
	service->setCurrentIndex(serviceIndex >= 0 ? serviceIndex : 0);

	onServiceChanged();

	if (!target.url.isEmpty()) {
		const int serverIndex = server->findData(target.url);

		if (serverIndex >= 0) {
			server->setCurrentIndex(serverIndex);
		} else {
			server->setEditText(target.url);
		}
	}
}

void StreamConsoleWindow::RemoveDestinationRow(int row)
{
	QTableWidgetItem *enabled = destinationsTable_->item(row, ColumnEnabled);
	const QString targetId = enabled ? enabled->data(Qt::UserRole).toString() : QString();

	/* Deleted on the next save rather than immediately, so removing a row is
	 * undoable by simply not saving. */
	if (!targetId.isEmpty()) {
		removedTargetIds_ << targetId;
	}

	destinationsTable_->removeRow(row);
}

void StreamConsoleWindow::OnTargetsFetched(const QList<NvsStreamTarget> &targets)
{
	serverTargets_ = targets;
	removedTargetIds_.clear();

	destinationsTable_->setRowCount(0);

	for (const NvsStreamTarget &target : targets) {
		AddDestinationRow(target);
	}

	destinationsNoticeLabel_->setText(
		targets.isEmpty() ? QString::fromUtf8(obs_module_text("Targets.None")) : QString());
}

/* Reads one row back out of the table. Returns false for a row that is not a
 * usable destination. */
static bool ReadDestinationRow(QTableWidget *table, int row, QString &serviceName, QString &serverUrl,
			       QString &key, QString &targetId, bool &enabled)
{
	QTableWidgetItem *enabledItem = table->item(row, ColumnEnabled);
	QComboBox *service = qobject_cast<QComboBox *>(table->cellWidget(row, ColumnService));
	QComboBox *server = qobject_cast<QComboBox *>(table->cellWidget(row, ColumnServer));
	QWidget *keyCell = table->cellWidget(row, ColumnKey);
	QLineEdit *keyEdit = keyCell ? keyCell->findChild<QLineEdit *>() : nullptr;

	if (!enabledItem || !service || !server || !keyEdit) {
		return false;
	}

	serviceName = service->currentText();
	/* A preset row carries the URL as item data; a custom row is free text. */
	serverUrl = server->currentData().isValid() ? server->currentData().toString()
						   : server->currentText().trimmed();
	key = keyEdit->text().trimmed();
	targetId = enabledItem->data(Qt::UserRole).toString();
	enabled = enabledItem->checkState() == Qt::Checked;

	return true;
}

void StreamConsoleWindow::OnPushDestinationsClicked()
{
	const QString roomId = SelectedRoomId();

	if (roomId.isEmpty()) {
		destinationsNoticeLabel_->setText(obs_module_text("Room.SelectFirst"));
		return;
	}

	for (const QString &removed : removedTargetIds_) {
		targetClient_->DeleteTarget(removed);
	}

	removedTargetIds_.clear();

	int saved = 0;

	for (int row = 0; row < destinationsTable_->rowCount(); row++) {
		QString serviceName;
		QString serverUrl;
		QString key;
		QString targetId;
		bool enabled = false;

		if (!ReadDestinationRow(destinationsTable_, row, serviceName, serverUrl, key, targetId, enabled)) {
			continue;
		}

		/* A brand new row with no server is an empty form, not a destination. */
		if (targetId.isEmpty() && serverUrl.isEmpty()) {
			continue;
		}

		NvsStreamTarget target;
		target.id = targetId;
		target.platformType = serviceName;
		target.name = serviceName;
		target.url = serverUrl;
		target.enabled = enabled;

		targetClient_->SaveTarget(roomId, target, key);
		saved++;
	}

	destinationsNoticeLabel_->setText(saved == 0 ? QString::fromUtf8(obs_module_text("Targets.NothingToPush"))
						     : QString::fromUtf8(obs_module_text("Targets.Pushing")));
}

QList<NvsRtmpDestination> StreamConsoleWindow::EnabledRtmpDestinations() const
{
	QList<NvsRtmpDestination> destinations;

	for (int row = 0; row < destinationsTable_->rowCount(); row++) {
		QString serviceName;
		QString serverUrl;
		QString key;
		QString targetId;
		bool enabled = false;

		if (!ReadDestinationRow(destinationsTable_, row, serviceName, serverUrl, key, targetId, enabled)) {
			continue;
		}

		if (!enabled || serverUrl.isEmpty()) {
			continue;
		}

		NvsRtmpDestination destination;
		/* Row number keeps two endpoints on the same service distinguishable
		 * in the log and in obs output names. */
		destination.name = QStringLiteral("%1 %2").arg(serviceName).arg(row + 1);
		destination.url = serverUrl;
		destination.key = key;

		destinations.append(destination);
	}

	return destinations;
}

void StreamConsoleWindow::OnStartDestinationsClicked()
{
	const QList<NvsRtmpDestination> destinations = EnabledRtmpDestinations();

	if (destinations.isEmpty()) {
		destinationsNoticeLabel_->setText(obs_module_text("Targets.NoneToStart"));
		return;
	}

	/* NVS encodes the room and pushes it itself, so going live is local rather
	 * than a call to the backend. The key typed into the row is used directly:
	 * a stored server-side key cannot be read back. */
	const int started = multiRtmp_->Start(destinations);

	if (started > 0) {
		destinationsNoticeLabel_->setText(
			QString::fromUtf8(obs_module_text("MultiRtmp.Live")).arg(started));
	}
}

void StreamConsoleWindow::OnStopDestinationsClicked()
{
	if (!multiRtmp_->IsActive()) {
		return;
	}

	multiRtmp_->Stop();
	destinationsNoticeLabel_->setText(obs_module_text("MultiRtmp.Stopped"));
}

/* NVS forwards the room's audio; it never captures the operator's machine.
 *
 * OBS ships with Desktop Audio and Mic/Aux enabled, which would put the
 * operator's speakers and microphone into the broadcast and double-mix the room
 * the moment they monitor it. The room's own audio arrives through the browser
 * source, so the global capture channels are cleared. */
void StreamConsoleWindow::EnforceAudioPolicy()
{
	int cleared = 0;

	/* Channels 1-2 are Desktop Audio, 3-6 are Mic/Aux. */
	for (uint32_t channel = 1; channel <= 6; channel++) {
		OBSSourceAutoRelease existing = obs_get_output_source(channel);

		if (!existing) {
			continue;
		}

		obs_set_output_source(channel, nullptr);
		cleared++;
	}

	if (cleared > 0) {
		blog(LOG_INFO,
		     "[nvs] disabled %d global audio capture device(s): NVS forwards room audio and captures none. "
		     "Re-enable in OBS audio settings if this machine really should be captured.",
		     cleared);
	}
}

void StreamConsoleWindow::InitStreamSettings()
{
	EnforceAudioPolicy();
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
	/* NVS pushes through its own multi-destination engine, so OBS's single
	 * output is not driven from here. This still reports it, because a stream
	 * started from the main window would push the same room a second time —
	 * the operator should be able to see that. */
	const bool active = obs_frontend_streaming_active();

	streamStatusLabel_->setText(obs_module_text(active ? "Stream.MainWindowLive" : "Stream.Offline"));
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
