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

#include <QWidget>

/* Renders the OBS program output into a plugin-owned window.
 *
 * This is the same mechanism the OBS preview uses: a native child window is
 * handed to obs_display_create(), and a draw callback on the graphics thread
 * renders the main texture scaled to fit. Deliberately minimal, and read-only:
 * there is no item selection, no transform handles, no interaction.
 */
class ProgramPreviewWidget : public QWidget {
	Q_OBJECT

public:
	explicit ProgramPreviewWidget(QWidget *parent = nullptr);
	~ProgramPreviewWidget() override;

	/* Releases the display before the graphics subsystem goes away. Safe to
	 * call more than once. */
	void DestroyDisplay();

protected:
	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void moveEvent(QMoveEvent *event) override;
	QPaintEngine *paintEngine() const override;

private:
	void CreateDisplay();
	void UpdateDisplaySize();

	/* Runs on the graphics thread: must not touch Qt state. */
	static void RenderPreview(void *data, uint32_t cx, uint32_t cy);

	obs_display_t *display_ = nullptr;
	bool destroying_ = false;
};
