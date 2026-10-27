/* Copyright Matthew Reid
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "StandaloneWindow.h"
#include <osg/DisplaySettings>
#include <osg/GraphicsThread>
#include <osgViewer/View>

#if defined(WIN32)
#include <osgViewer/api/Win32/GraphicsHandleWin32>
#else
#include <osgViewer/api/X11/GraphicsWindowX11>
#endif

#include <assert.h>

using namespace skybolt::vis;

static StandaloneWindowConfig createDefaultConfigWithRect(const RectI& rect)
{
	StandaloneWindowConfig c;
	c.rect = rect;
	return c;
}

StandaloneWindow::StandaloneWindow(const RectI& rect) :
	StandaloneWindow(createDefaultConfigWithRect(rect))
{
}

static osg::ref_ptr<osgViewer::View> createStandaloneView(const RectI& rect)
{
	// TODO Use the vsync value in skybolt::DisplaySettings to enable/disable vsync by setting the value in graphics traits before creating the context.

	osg::ref_ptr<osgViewer::View> view = new osgViewer::View;
	view->setUpViewInWindow(rect.x, rect.y, rect.width, rect.height);

	if (!view->getCamera()->getGraphicsContext() && osg::DisplaySettings::instance()->getNumMultiSamples() > 0)
	{
		// Some X servers (e.g. WSLg on WSL2) have no multisampled visuals. The scene is rendered to multisampled
		// render targets anyway, so the window itself does not need multisampling.
		osg::ref_ptr<osg::DisplaySettings> settings = new osg::DisplaySettings(*osg::DisplaySettings::instance());
		settings->setNumMultiSamples(0);
		view->setDisplaySettings(settings);
		view->setUpViewInWindow(rect.x, rect.y, rect.width, rect.height);
	}

	osg::GraphicsContext* context = view->getCamera()->getGraphicsContext();
	if (!context)
	{
		throw std::runtime_error("Could not initialize graphics context");
	}

	configureGraphicsState(*context);
	return view;
}

StandaloneWindow::StandaloneWindow(const StandaloneWindowConfig& config) :
	Window(createStandaloneView(config.rect))
{
	const auto& rect = config.rect;

	getGraphicsWindow().useCursor(false);

	// FIXME: The Viewer's default camera clears the frame before our Camera is rendered from, which clears the frame a second time.
	// The first frame clearing is unnecessary and should be eliminated.
}

int StandaloneWindow::getWidth() const
{
	int x, y, width, height;
	getGraphicsWindow().getWindowRectangle(x, y, width, height);
	return width;
}

int StandaloneWindow::getHeight() const
{
	int x, y, width, height;
	getGraphicsWindow().getWindowRectangle(x, y, width, height);
	return height;
}

std::string StandaloneWindow::getHandle() const
{
	osg::GraphicsContext* context = mView->getCamera()->getGraphicsContext();
#if defined(WIN32)
	size_t ptr = (size_t)dynamic_cast<osgViewer::GraphicsHandleWin32*>(context)->getHWND();
#else
	size_t ptr = (size_t)dynamic_cast<osgViewer::GraphicsWindowX11*>(context)->getWindow();
#endif
	return std::to_string(ptr);
}

osgViewer::GraphicsWindow& StandaloneWindow::getGraphicsWindow() const
{
	osgViewer::GraphicsWindow* window = dynamic_cast<osgViewer::GraphicsWindow*>(mView->getCamera()->getGraphicsContext());
	assert(window);
	return *window;
}

namespace {

//! Sets the swap interval of the OpenGL context of a window: 1 waits for the refreshes of the monitor (vsync), 0 does not.
//! Runs as operation of the context while rendering, when the context is current.
class SwapIntervalOperation : public osg::GraphicsOperation
{
public:
	explicit SwapIntervalOperation(int interval) :
		osg::GraphicsOperation("SwapInterval", false),
		mInterval(interval)
	{
	}

	void operator()(osg::GraphicsContext* context) override
	{
#if defined(WIN32)
		if (auto window = dynamic_cast<osgViewer::GraphicsWindow*>(context); window)
		{
			window->setSyncToVBlank(mInterval != 0);
		}
#elif !defined(OSG_USE_EGL)
		// osgViewer does not implement setSyncToVBlank() for X11
		auto window = dynamic_cast<osgViewer::GraphicsWindowX11*>(context);
		if (!window)
		{
			return;
		}
		Display* display = window->getDisplayToUse();
		const char* extensions = glXQueryExtensionsString(display, window->getTraits()->screenNum);
		auto hasExtension = [extensions] (const std::string& name) {
			return extensions && (" " + std::string(extensions) + " ").find(" " + name + " ") != std::string::npos;
		};
		if (hasExtension("GLX_EXT_swap_control"))
		{
			using SwapIntervalExt = void (*)(Display*, GLXDrawable, int);
			auto swapInterval = reinterpret_cast<SwapIntervalExt>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glXSwapIntervalEXT")));
			swapInterval(display, window->getWindow(), mInterval);
		}
		else if (hasExtension("GLX_MESA_swap_control"))
		{
			using SwapIntervalMesa = int (*)(unsigned int);
			auto swapInterval = reinterpret_cast<SwapIntervalMesa>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glXSwapIntervalMESA")));
			swapInterval(unsigned(mInterval));
		}
#endif
	}

private:
	int mInterval;
};

} // namespace

void StandaloneWindow::setVsyncEnabled(bool enabled)
{
	mVsyncEnabled = enabled;
	getGraphicsWindow().add(new SwapIntervalOperation(enabled ? 1 : 0));
}

void StandaloneWindow::setFullscreen(bool fullscreen)
{
	if (fullscreen == isFullscreen())
	{
		return;
	}
	osgViewer::GraphicsWindow& window = getGraphicsWindow();

#if defined(WIN32)
	// The window positions of osgViewer are relative to the screen of its traits, so the window is changed directly.
	// osgViewer takes the new size from the window messages.
	HWND hwnd = dynamic_cast<osgViewer::GraphicsHandleWin32&>(window).getHWND();
	if (fullscreen)
	{
		RECT frame;
		MONITORINFO monitor = {sizeof(MONITORINFO)};
		if (!GetWindowRect(hwnd, &frame) || !GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor))
		{
			return;
		}
		long style = GetWindowLong(hwnd, GWL_STYLE);
		long exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
		mWindowedState = WindowedState{RectI(frame.left, frame.top, frame.right - frame.left, frame.bottom - frame.top), style, exStyle};
		const RECT& screen = monitor.rcMonitor;
		SetWindowLong(hwnd, GWL_STYLE, style & ~(WS_CAPTION | WS_THICKFRAME));
		SetWindowLong(hwnd, GWL_EXSTYLE, exStyle & ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));
		SetWindowPos(hwnd, HWND_TOP, screen.left, screen.top, screen.right - screen.left, screen.bottom - screen.top,
			SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
	}
	else
	{
		const RectI& rect = mWindowedState->rect;
		SetWindowLong(hwnd, GWL_STYLE, mWindowedState->style);
		SetWindowLong(hwnd, GWL_EXSTYLE, mWindowedState->exStyle);
		SetWindowPos(hwnd, nullptr, rect.x, rect.y, rect.width, rect.height, SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
		mWindowedState.reset();
	}
#else
	if (fullscreen)
	{
		osg::GraphicsContext::WindowingSystemInterface* wsi = osg::GraphicsContext::getWindowingSystemInterface();
		if (!wsi)
		{
			return;
		}
		unsigned int screenWidth, screenHeight;
		wsi->getScreenResolution(*window.getTraits(), screenWidth, screenHeight);
		int x, y, width, height;
		window.getWindowRectangle(x, y, width, height);
		mWindowedState = WindowedState{RectI(x, y, width, height)};
		window.setWindowDecoration(false);
		window.setWindowRectangle(0, 0, int(screenWidth), int(screenHeight));
	}
	else
	{
		const RectI& rect = mWindowedState->rect;
		window.setWindowDecoration(true);
		window.setWindowRectangle(rect.x, rect.y, rect.width, rect.height);
		mWindowedState.reset();
	}
#endif
	window.grabFocusIfPointerInWindow();
}