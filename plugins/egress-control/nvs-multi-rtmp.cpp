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

#include "nvs-multi-rtmp.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <obs.hpp>
#include <util/config-file.h>

namespace {

constexpr const char *FALLBACK_OUTPUT_ID = "rtmp_output";
constexpr const char *SERVICE_ID = "rtmp_custom";

/* Reconnect behaviour copied from the frontend's own stream output: a dropped
 * RTMP connection retries rather than ending the broadcast. */
constexpr int RECONNECT_RETRIES = 20;
constexpr int RECONNECT_DELAY_SEC = 10;

/* The output type is the service's business, not ours.
 *
 * Facebook is RTMPS and some services register their own output, so hardcoding
 * "rtmp_output" is what makes a destination fail to connect for reasons that
 * look like a bad key. This mirrors BasicOutputHandler's selection. */
const char *OutputTypeFor(obs_service_t *service)
{
	const char *preferred = obs_service_get_preferred_output_type(service);

	if (preferred && (obs_get_output_flags(preferred) & OBS_OUTPUT_SERVICE) != 0) {
		return preferred;
	}

	const char *protocol = obs_service_get_protocol(service);

	if (protocol && !obs_is_output_protocol_registered(protocol)) {
		blog(LOG_WARNING, "[nvs] protocol '%s' is not registered", protocol);
	}

	return FALLBACK_OUTPUT_ID;
}

/* Used when the profile carries no encoder settings of its own. */
constexpr int DEFAULT_VIDEO_BITRATE = 2500;
constexpr int DEFAULT_AUDIO_BITRATE = 160;

int ProfileInt(const char *section, const char *key, int fallback)
{
	config_t *profile = obs_frontend_get_profile_config();

	if (!profile) {
		return fallback;
	}

	const int64_t value = config_get_int(profile, section, key);

	return value > 0 ? int(value) : fallback;
}

} // namespace

NvsMultiRtmp::NvsMultiRtmp(QObject *parent) : QObject(parent) {}

NvsMultiRtmp::~NvsMultiRtmp()
{
	Stop();
	ReleaseEncoders();
}

bool NvsMultiRtmp::EnsureEncoders(const QList<obs_service_t *> &services)
{
	if (videoEncoder_ && audioEncoder_) {
		return true;
	}

	ReleaseEncoders();

	/* Settings shaped like the frontend's simple output, which is the
	 * combination already proven to reach Facebook from the main window. */
	OBSDataAutoRelease videoSettings = obs_data_create();
	obs_data_set_string(videoSettings, "rate_control", "CBR");
	obs_data_set_int(videoSettings, "bitrate", ProfileInt("SimpleOutput", "VBitrate", DEFAULT_VIDEO_BITRATE));
	obs_data_set_string(videoSettings, "preset", "veryfast");
	obs_data_set_string(videoSettings, "profile", "main");
	/* Regular keyframes: both YouTube and Facebook expect roughly 2s. */
	obs_data_set_int(videoSettings, "keyint_sec", 2);

	OBSDataAutoRelease audioSettings = obs_data_create();
	obs_data_set_string(audioSettings, "rate_control", "CBR");
	obs_data_set_int(audioSettings, "bitrate", ProfileInt("SimpleOutput", "ABitrate", DEFAULT_AUDIO_BITRATE));

	/* Each service clamps the encoder to what it will accept — Facebook's
	 * 4000 kbps ceiling, for example. One encoder feeds every destination, so
	 * the MOST restrictive service has to win; applying them in turn leaves
	 * the lowest limit in place. */
	for (obs_service_t *service : services) {
		obs_service_apply_encoder_settings(service, videoSettings, audioSettings);
	}

	blog(LOG_INFO, "[nvs] shared encoder: %d kbps video, %d kbps audio",
	     int(obs_data_get_int(videoSettings, "bitrate")), int(obs_data_get_int(audioSettings, "bitrate")));

	videoEncoder_ = obs_video_encoder_create("obs_x264", "nvs_video_encoder", videoSettings, nullptr);
	audioEncoder_ = obs_audio_encoder_create("ffmpeg_aac", "nvs_audio_encoder", audioSettings, 0, nullptr);

	if (!videoEncoder_ || !audioEncoder_) {
		blog(LOG_WARNING, "[nvs] could not create the shared encoders");
		ReleaseEncoders();
		return false;
	}

	obs_encoder_set_video(videoEncoder_, obs_get_video());
	obs_encoder_set_audio(audioEncoder_, obs_get_audio());

	return true;
}

void NvsMultiRtmp::ReleaseEncoders()
{
	if (videoEncoder_) {
		obs_encoder_release(videoEncoder_);
		videoEncoder_ = nullptr;
	}

	if (audioEncoder_) {
		obs_encoder_release(audioEncoder_);
		audioEncoder_ = nullptr;
	}
}

int NvsMultiRtmp::Start(const QList<NvsRtmpDestination> &destinations)
{
	if (IsActive()) {
		/* Restarting would drop live viewers on destinations that are fine. */
		emit Failed(QString::fromUtf8(obs_module_text("MultiRtmp.AlreadyLive")));
		return 0;
	}

	if (destinations.isEmpty()) {
		emit Failed(QString::fromUtf8(obs_module_text("MultiRtmp.NoDestinations")));
		return 0;
	}

	/* Services first: the encoder settings depend on what they allow. */
	QList<obs_service_t *> services;
	QList<QString> names;

	for (const NvsRtmpDestination &destination : destinations) {
		if (destination.url.trimmed().isEmpty()) {
			continue;
		}

		OBSDataAutoRelease serviceSettings = obs_data_create();
		obs_data_set_string(serviceSettings, "server", destination.url.trimmed().toUtf8().constData());
		obs_data_set_string(serviceSettings, "key", destination.key.toUtf8().constData());

		const QByteArray name = destination.name.toUtf8();
		obs_service_t *service =
			obs_service_create(SERVICE_ID, name.constData(), serviceSettings, nullptr);

		if (service) {
			services.append(service);
			names.append(destination.name);
		}
	}

	if (services.isEmpty()) {
		emit Failed(QString::fromUtf8(obs_module_text("MultiRtmp.NoDestinations")));
		return 0;
	}

	if (!EnsureEncoders(services)) {
		for (obs_service_t *service : services) {
			obs_service_release(service);
		}

		emit Failed(QString::fromUtf8(obs_module_text("MultiRtmp.EncoderFailed")));
		return 0;
	}

	int started = 0;

	for (int i = 0; i < services.size(); i++) {
		obs_service_t *service = services[i];
		const QByteArray name = names[i].toUtf8();

		obs_output_t *output =
			obs_output_create(OutputTypeFor(service), name.constData(), nullptr, nullptr);

		if (!output) {
			obs_service_release(service);
			continue;
		}

		/* Both outputs reference the same encoders; libobs reference-counts
		 * them, so the frame is encoded once regardless of destination count. */
		obs_output_set_video_encoder(output, videoEncoder_);
		obs_output_set_audio_encoder(output, audioEncoder_, 0);
		obs_output_set_service(output, service);

		/* A dropped connection retries instead of ending the broadcast. */
		obs_output_set_reconnect_settings(output, RECONNECT_RETRIES, RECONNECT_DELAY_SEC);

		if (!obs_output_start(output)) {
			/* The reason is on the output, and it is safe to log: it
			 * describes the failure, not the credential. */
			blog(LOG_WARNING, "[nvs] destination '%s' failed to start: %s", name.constData(),
			     obs_output_get_last_error(output));

			obs_output_release(output);
			obs_service_release(service);
			continue;
		}

		outputs_.append({output, service, names[i]});
		started++;
	}

	if (started == 0) {
		ReleaseEncoders();
		emit Failed(QString::fromUtf8(obs_module_text("MultiRtmp.NoneStarted")));
		return 0;
	}

	blog(LOG_INFO, "[nvs] pushing to %d destination(s) from one shared encoder", started);

	emit Changed();

	return started;
}

void NvsMultiRtmp::Stop()
{
	if (outputs_.isEmpty()) {
		return;
	}

	for (const ActiveOutput &active : outputs_) {
		obs_output_stop(active.output);
		obs_output_release(active.output);
		obs_service_release(active.service);
	}

	blog(LOG_INFO, "[nvs] stopped %lld destination(s)", static_cast<long long>(outputs_.size()));

	outputs_.clear();

	/* Encoders are kept until the object dies: a stop/start cycle is common
	 * and rebuilding x264 each time is wasteful. */
	emit Changed();
}
