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

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "egress-controller.hpp"
#include "egress-state.hpp"

EgressControlDock::EgressControlDock(EgressController *controller, QWidget *parent)
	: QWidget(parent),
	  controller_(controller)
{
	setObjectName("egressControlDockContents");

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

	connect(startButton_, &QPushButton::clicked, this, [this]() {
		/* Disabled up front so a second click cannot queue a duplicate. */
		startButton_->setEnabled(false);
		controller_->Start();
	});

	connect(stopButton_, &QPushButton::clicked, this, [this]() {
		stopButton_->setEnabled(false);
		controller_->Stop();
	});

	connect(controller_, &EgressController::Changed, this, &EgressControlDock::Refresh);

	Refresh();
}

void EgressControlDock::Refresh()
{
	statusValueLabel_->setText(obs_module_text(EgressStateLocaleKey(controller_->State())));
	messageLabel_->setText(controller_->MessageText());

	startButton_->setEnabled(controller_->StartAvailable());
	stopButton_->setEnabled(controller_->StopAvailable());
}
