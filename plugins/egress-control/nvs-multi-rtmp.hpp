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

#include <obs.h>

#include <QList>
#include <QObject>
#include <QString>

/* One RTMP endpoint to push to. */
struct NvsRtmpDestination {
	QString name;
	QString url;
	QString key;
};

/* Pushes the program feed to several RTMP endpoints at once.
 *
 * OBS itself has exactly one streaming output, so "go live to three places"
 * is not something the frontend can do. This creates one rtmp_output per
 * destination and points them all at a SINGLE shared encoder pair: the frame is
 * encoded once and the compressed packets are fanned out. Encoding per
 * destination would multiply CPU cost by the number of endpoints for no gain,
 * since they all carry the same picture.
 *
 * Audio comes from the OBS mix, which for NVS is the room's audio and nothing
 * else — see the audio policy in the console.
 */
class NvsMultiRtmp : public QObject {
	Q_OBJECT

public:
	explicit NvsMultiRtmp(QObject *parent = nullptr);
	~NvsMultiRtmp() override;

	/* Starts every destination. Returns the number that began starting;
	 * failures are reported through Failed(). */
	int Start(const QList<NvsRtmpDestination> &destinations);

	void Stop();

	bool IsActive() const { return !outputs_.isEmpty(); }
	int ActiveCount() const { return int(outputs_.size()); }

signals:
	void Failed(const QString &message);
	void Changed();

private:
	/* Services are passed in so each can clamp the shared encoder to what it
	 * accepts; the most restrictive wins. */
	bool EnsureEncoders(const QList<obs_service_t *> &services);
	void ReleaseEncoders();

	/* Shared by every output: encode once, send many. */
	obs_encoder_t *videoEncoder_ = nullptr;
	obs_encoder_t *audioEncoder_ = nullptr;

	struct ActiveOutput {
		obs_output_t *output = nullptr;
		obs_service_t *service = nullptr;
		QString name;
	};

	QList<ActiveOutput> outputs_;
};
