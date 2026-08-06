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

#include "program-preview-widget.hpp"

#include <obs-module.h>

#include <QResizeEvent>
#include <QWindow>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#if !defined(_WIN32) && !defined(__APPLE__)
#include <obs-nix-platform.h>
#endif

namespace {

constexpr uint32_t PREVIEW_BACKGROUND_COLOR = 0xFF1A1A1A;

/* Physical pixel size of a widget, which is what the display expects. */
QSize PixelSize(const QWidget *widget)
{
	return widget->size() * widget->devicePixelRatioF();
}

/* Largest centered rectangle with the source aspect ratio that fits the
 * window. Mirrors the frontend's GetScaleAndCenterPos(). */
void ScaleAndCenter(int baseCX, int baseCY, int windowCX, int windowCY, int &x, int &y, float &scale)
{
	const double windowAspect = double(windowCX) / double(windowCY);
	const double baseAspect = double(baseCX) / double(baseCY);

	int newCX = 0;
	int newCY = 0;

	if (windowAspect > baseAspect) {
		scale = float(windowCY) / float(baseCY);
		newCX = int(double(windowCY) * baseAspect);
		newCY = windowCY;
	} else {
		scale = float(windowCX) / float(baseCX);
		newCX = windowCX;
		newCY = int(double(windowCX) / baseAspect);
	}

	x = windowCX / 2 - newCX / 2;
	y = windowCY / 2 - newCY / 2;
}

bool NativeWindowHandle(QWindow *window, gs_window &target)
{
#ifdef _WIN32
	target.hwnd = (HWND)window->winId();
	return true;
#elif defined(__APPLE__)
	target.view = (id)window->winId();
	return true;
#else
	if (obs_get_nix_platform() == OBS_NIX_PLATFORM_X11_EGL) {
		target.id = window->winId();
		target.display = obs_get_nix_platform_display();
		return true;
	}

	/* Wayland needs the frontend's surface plumbing, which is not public API.
	 * The console still works; only the preview stays blank. */
	return false;
#endif
}

} // namespace

ProgramPreviewWidget::ProgramPreviewWidget(QWidget *parent) : QWidget(parent)
{
	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_StaticContents);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAttribute(Qt::WA_DontCreateNativeAncestors);
	setAttribute(Qt::WA_NativeWindow);

	setMinimumSize(320, 180);
}

ProgramPreviewWidget::~ProgramPreviewWidget()
{
	DestroyDisplay();
}

void ProgramPreviewWidget::DestroyDisplay()
{
	destroying_ = true;

	if (!display_) {
		return;
	}

	obs_display_remove_draw_callback(display_, ProgramPreviewWidget::RenderPreview, this);
	obs_display_destroy(display_);
	display_ = nullptr;
}

void ProgramPreviewWidget::CreateDisplay()
{
	if (display_ || destroying_) {
		return;
	}

	QWindow *window = windowHandle();

	if (!window || !window->isExposed()) {
		return;
	}

	const QSize size = PixelSize(this);

	gs_init_data info = {};
	info.cx = size.width();
	info.cy = size.height();
	info.format = GS_BGRA;
	info.zsformat = GS_ZS_NONE;

	if (!NativeWindowHandle(window, info.window)) {
		blog(LOG_WARNING, "[egress-control] preview is unavailable on this display platform");
		destroying_ = true;
		return;
	}

	display_ = obs_display_create(&info, PREVIEW_BACKGROUND_COLOR);

	if (!display_) {
		blog(LOG_WARNING, "[egress-control] failed to create the preview display");
		return;
	}

	obs_display_add_draw_callback(display_, ProgramPreviewWidget::RenderPreview, this);
}

void ProgramPreviewWidget::UpdateDisplaySize()
{
	if (!display_) {
		return;
	}

	const QSize size = PixelSize(this);
	obs_display_resize(display_, size.width(), size.height());
}

void ProgramPreviewWidget::paintEvent(QPaintEvent *event)
{
	CreateDisplay();

	QWidget::paintEvent(event);
}

void ProgramPreviewWidget::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);

	CreateDisplay();

	if (isVisible()) {
		UpdateDisplaySize();
	}
}

void ProgramPreviewWidget::moveEvent(QMoveEvent *event)
{
	QWidget::moveEvent(event);

	if (display_) {
		obs_display_update_color_space(display_);
	}
}

QPaintEngine *ProgramPreviewWidget::paintEngine() const
{
	/* Qt must not paint into this widget: OBS owns the surface. */
	return nullptr;
}

void ProgramPreviewWidget::RenderPreview(void *data, uint32_t cx, uint32_t cy)
{
	UNUSED_PARAMETER(data);

	obs_video_info videoInfo = {};

	if (!obs_get_video_info(&videoInfo)) {
		return;
	}

	const uint32_t baseCX = videoInfo.base_width > 0 ? videoInfo.base_width : 1;
	const uint32_t baseCY = videoInfo.base_height > 0 ? videoInfo.base_height : 1;

	int x = 0;
	int y = 0;
	float scale = 1.0f;

	ScaleAndCenter(baseCX, baseCY, int(cx), int(cy), x, y, scale);

	const int scaledCX = int(scale * float(baseCX));
	const int scaledCY = int(scale * float(baseCY));

	gs_projection_push();
	gs_viewport_push();
	gs_set_viewport(x, y, scaledCX, scaledCY);
	gs_ortho(0.0f, float(baseCX), 0.0f, float(baseCY), -100.0f, 100.0f);

	obs_render_main_texture();

	gs_viewport_pop();
	gs_projection_pop();
}
