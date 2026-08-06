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

#include "stream-settings-dialog.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <obs.hpp>

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

constexpr const char *CUSTOM_SERVICE_ID = "rtmp_custom";

} // namespace

StreamSettingsDialog::StreamSettingsDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(obs_module_text("StreamSettings"));
	setModal(true);

	QVBoxLayout *mainLayout = new QVBoxLayout(this);

	QFormLayout *formLayout = new QFormLayout();

	serverEdit_ = new QLineEdit(this);
	serverEdit_->setPlaceholderText("rtmp://");

	streamKeyEdit_ = new QLineEdit(this);
	/* Masked so the key is not shown on a screen that may be visible to an
	 * audience or a capture. */
	streamKeyEdit_->setEchoMode(QLineEdit::Password);

	formLayout->addRow(obs_module_text("StreamSettings.Server"), serverEdit_);
	formLayout->addRow(obs_module_text("StreamSettings.StreamKey"), streamKeyEdit_);

	mainLayout->addLayout(formLayout);

	noticeLabel_ = new QLabel(this);
	noticeLabel_->setWordWrap(true);
	mainLayout->addWidget(noticeLabel_);

	QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
	saveButton_ = buttonBox->button(QDialogButtonBox::Save);

	mainLayout->addWidget(buttonBox);

	connect(buttonBox, &QDialogButtonBox::accepted, this, &StreamSettingsDialog::Save);
	connect(buttonBox, &QDialogButtonBox::rejected, this, &StreamSettingsDialog::reject);

	LoadCurrentService();
}

void StreamSettingsDialog::LoadCurrentService()
{
	obs_service_t *service = obs_frontend_get_streaming_service();

	if (!service) {
		return;
	}

	const char *serviceId = obs_service_get_type(service);

	if (serviceId && strcmp(serviceId, CUSTOM_SERVICE_ID) != 0) {
		/* A preset service is configured. Editing it here would silently
		 * replace it, so the fields are left read-only instead. */
		noticeLabel_->setText(obs_module_text("StreamSettings.PresetServiceInUse"));
		serverEdit_->setReadOnly(true);
		streamKeyEdit_->setReadOnly(true);
		saveButton_->setEnabled(false);
		return;
	}

	OBSDataAutoRelease settings = obs_service_get_settings(service);

	if (!settings) {
		return;
	}

	serverEdit_->setText(QString::fromUtf8(obs_data_get_string(settings, "server")));
	streamKeyEdit_->setText(QString::fromUtf8(obs_data_get_string(settings, "key")));
}

void StreamSettingsDialog::Save()
{
	/* Swapping the destination mid-broadcast would be ignored by the running
	 * output and is refused rather than silently dropped. */
	if (obs_frontend_streaming_active()) {
		noticeLabel_->setText(obs_module_text("StreamSettings.CannotEditWhileLive"));
		return;
	}

	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "server", serverEdit_->text().trimmed().toUtf8().constData());
	obs_data_set_string(settings, "key", streamKeyEdit_->text().toUtf8().constData());

	OBSServiceAutoRelease service =
		obs_service_create(CUSTOM_SERVICE_ID, "default_service", settings, nullptr);

	if (!service) {
		noticeLabel_->setText(obs_module_text("StreamSettings.SaveFailed"));
		return;
	}

	obs_frontend_set_streaming_service(service);
	obs_frontend_save_streaming_service();

	/* The stream key is deliberately absent from this log line. */
	blog(LOG_INFO, "[egress-control] stream destination updated");

	accept();
}
