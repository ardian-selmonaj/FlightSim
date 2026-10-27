/* Copyright Matthew Reid
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#pragma once

#include "Window.h"
#include "SkyboltVis/Rect.h"

#include <optional>

namespace skybolt {
namespace vis {

struct StandaloneWindowConfig
{
	RectI rect;
};

class StandaloneWindow : public Window
{
public:
	StandaloneWindow(const StandaloneWindowConfig& config);
	StandaloneWindow(const RectI& rect);
	~StandaloneWindow() override = default;

	int getWidth() const override;
	int getHeight() const override;

	std::string getHandle() const;

	//! Vertical synchronization: the frames are shown at the refreshes of the monitor, and rendering waits for them. On by default.
	//! A change takes effect when the next frame is rendered.
	void setVsyncEnabled(bool enabled);
	bool isVsyncEnabled() const { return mVsyncEnabled; }

	//! Shows the window without its border over the whole monitor, or again at its previous position and size
	void setFullscreen(bool fullscreen);
	bool isFullscreen() const { return mWindowedState.has_value(); }

private:
	osgViewer::GraphicsWindow& getGraphicsWindow() const;

private:
	bool mVsyncEnabled = true;

	//! Window before full screen: on Windows the rectangle of its frame in screen coordinates and its styles,
	//! otherwise the window rectangle of osgViewer
	struct WindowedState
	{
		RectI rect;
		long style = 0;
		long exStyle = 0;
	};
	std::optional<WindowedState> mWindowedState;
};

} // namespace vis
} // namespace skybolt
