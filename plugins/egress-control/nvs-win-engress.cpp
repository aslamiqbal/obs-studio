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

#include "nvs-win-engress.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <obs.hpp>

#include <QCloseEvent>
#include <QDesktopServices>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QUrl>
#include <QVBoxLayout>

#include "program-preview-widget.hpp"

namespace {

/* Where an operator fetches a Facebook stream key. */
constexpr const char *FACEBOOK_LIVE_PRODUCER_URL = "https://www.facebook.com/live/producer?ref=OBS";

} // namespace

NvsWinEngress::NvsWinEngress(QWidget *parent) : QWidget(parent, Qt::Window)
{
	setObjectName("nvswinengress");
	setWindowTitle(obs_module_text("Engress.Title"));
	resize(860, 520);

	QVBoxLayout *mainLayout = new QVBoxLayout(this);

	/* Top: the room preview on the left, room fields on the right. */
	QHBoxLayout *topRow = new QHBoxLayout();

	preview_ = new ProgramPreviewWidget(this);
	preview_->setMinimumSize(360, 220);
	topRow->addWidget(preview_, 3);

	QVBoxLayout *roomColumn = new QVBoxLayout();

	QFormLayout *roomForm = new QFormLayout();

	roomCodeEdit_ = new QLineEdit(this);

	roomTokenEdit_ = new QLineEdit(this);
	/* A room token grants access to the room; masked like every other
	 * credential in NVS. */
	roomTokenEdit_->setEchoMode(QLineEdit::Password);

	roomForm->addRow(obs_module_text("Engress.RoomCode"), roomCodeEdit_);
	roomForm->addRow(obs_module_text("Engress.RoomToken"), roomTokenEdit_);

	roomColumn->addLayout(roomForm);

	/* One platform at a time in this window, so radios rather than checks. */
	QHBoxLayout *platformRow = new QHBoxLayout();

	facebookRadio_ = new QRadioButton(QStringLiteral("Facebook"), this);
	youtubeRadio_ = new QRadioButton(QStringLiteral("Youtube"), this);
	facebookRadio_->setChecked(true);

	platformRow->addWidget(facebookRadio_);
	platformRow->addWidget(youtubeRadio_);
	platformRow->addStretch(1);

	roomColumn->addLayout(platformRow);

	tryOpenButton_ = new QPushButton(obs_module_text("Engress.TryOpen"), this);
	roomColumn->addWidget(tryOpenButton_, 0, Qt::AlignLeft);

	/* Directly under the button that opens the page it is copied from. */
	QHBoxLayout *streamKeyRow = new QHBoxLayout();

	streamKeyEdit_ = new QLineEdit(this);
	streamKeyEdit_->setEchoMode(QLineEdit::Password);
	streamKeyEdit_->setPlaceholderText(obs_module_text("Engress.StreamKey"));

	showStreamKeyButton_ = new QPushButton(obs_module_text("StreamSettings.Show"), this);
	showStreamKeyButton_->setCheckable(true);
	showStreamKeyButton_->setFixedWidth(56);

	streamKeyRow->addWidget(streamKeyEdit_, 1);
	streamKeyRow->addWidget(showStreamKeyButton_);

	roomColumn->addLayout(streamKeyRow);

	roomColumn->addStretch(1);

	topRow->addLayout(roomColumn, 2);

	mainLayout->addLayout(topRow, 1);

	/* Bottom: the live destination. */
	QFormLayout *liveForm = new QFormLayout();

	liveUrlEdit_ = new QLineEdit(this);
	liveUrlEdit_->setPlaceholderText(
		QStringLiteral("rtmps://a.rtmps.youtube.com/live2  OR  rtmps://b.rtmps.youtube.com/live2?backup=1"));

	/* The key lives beside Open FB Live, next to the page it is copied from,
	 * so there is no second token field down here. */
	liveForm->addRow(obs_module_text("Engress.LiveUrl"), liveUrlEdit_);

	mainLayout->addLayout(liveForm);

	QHBoxLayout *actionRow = new QHBoxLayout();

	/* Far from Start Egress on purpose: this ends the application, and the two
	 * should not sit under the same thumb during a broadcast. */
	exitButton_ = new QPushButton(obs_module_text("Engress.Exit"), this);
	actionRow->addWidget(exitButton_);

	actionRow->addStretch(1);

	startEgressButton_ = new QPushButton(obs_module_text("Engress.Start"), this);
	actionRow->addWidget(startEgressButton_);

	mainLayout->addLayout(actionRow);

	roomNoticeLabel_ = new QLabel(this);
	roomNoticeLabel_->setWordWrap(true);
	mainLayout->addWidget(roomNoticeLabel_);

	/* Editing either field rebuilds the link and points the browser source at
	 * it, so the Room window shows the room without a separate Apply step.
	 * editingFinished rather than textChanged: one update per edit, not one
	 * per keystroke reloading the page. */
	connect(roomCodeEdit_, &QLineEdit::editingFinished, this, &NvsWinEngress::ApplyRoomUrl);
	connect(roomTokenEdit_, &QLineEdit::editingFinished, this, &NvsWinEngress::ApplyRoomUrl);

	/* Opens Facebook's Live Producer, where the operator copies the stream
	 * key that goes in the field below. */
	connect(tryOpenButton_, &QPushButton::clicked, this, []() {
		QDesktopServices::openUrl(QUrl(QLatin1String(FACEBOOK_LIVE_PRODUCER_URL)));
	});

	/* Quits NVS rather than just hiding this window — the window's own close
	 * button already does the hiding.
	 *
	 * Closing the main window is the frontend's normal shutdown path, so the
	 * scene collection is saved and outputs are stopped cleanly. Calling
	 * qApp->quit() would skip all of that. */
	connect(exitButton_, &QPushButton::clicked, this, []() {
		if (QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window())) {
			mainWindow->close();
		}
	});

	connect(showStreamKeyButton_, &QPushButton::clicked, this, [this]() {
		const bool visible = showStreamKeyButton_->isChecked();

		streamKeyEdit_->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
		showStreamKeyButton_->setText(
			obs_module_text(visible ? "StreamSettings.Hide" : "StreamSettings.Show"));
	});

	/* The platform radios and Start Egress are still skeleton. */
}

QString NvsWinEngress::RoomUrl() const
{
	const QString code = roomCodeEdit_->text().trimmed();
	const QString token = roomTokenEdit_->text().trimmed();

	if (code.isEmpty() || token.isEmpty()) {
		return {};
	}

	/* Fixed template. The credential goes in the fragment, which browsers do
	 * not send to the server, so it stays out of access logs even though the
	 * whole string is a URL. */
	return QStringLiteral("https://nadavox.com/rooms/%1/participant?egress=true&autojoin=1&name=OBS#token=%2")
		.arg(QString::fromUtf8(QUrl::toPercentEncoding(code)))
		.arg(QString::fromUtf8(QUrl::toPercentEncoding(token)));
}

void NvsWinEngress::ApplyRoomUrl()
{
	const QString url = RoomUrl();

	if (url.isEmpty()) {
		roomNoticeLabel_->setText(obs_module_text("Engress.RoomFieldsRequired"));
		return;
	}

	OBSSourceAutoRelease sceneSource = obs_frontend_get_current_scene();
	obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;

	if (!scene) {
		roomNoticeLabel_->setText(obs_module_text("Engress.NoBrowserSource"));
		return;
	}

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
		obs_source_update(source, settings);

		ctx->applied = true;
		ctx->sourceName = QString::fromUtf8(obs_source_get_name(source));

		/* First browser source wins; a scene with several would otherwise
		 * all be pointed at the same room. */
		return false;
	};

	obs_scene_enum_items(scene, applyToItem, &context);

	if (!context.applied) {
		roomNoticeLabel_->setText(obs_module_text("Engress.NoBrowserSource"));
		return;
	}

	/* The URL embeds the room token, so only the source name is logged. */
	blog(LOG_INFO, "[nvs] engress: room link applied to browser source '%s'",
	     context.sourceName.toUtf8().constData());

	roomNoticeLabel_->setText(
		QString::fromUtf8(obs_module_text("Engress.RoomApplied")).arg(context.sourceName));
}

NvsWinEngress::~NvsWinEngress()
{
	ReleasePreview();
}

void NvsWinEngress::ReleasePreview()
{
	if (preview_) {
		preview_->DestroyDisplay();
	}
}

void NvsWinEngress::closeEvent(QCloseEvent *event)
{
	hide();
	event->ignore();
}
