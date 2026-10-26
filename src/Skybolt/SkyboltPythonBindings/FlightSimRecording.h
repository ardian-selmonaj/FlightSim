/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#pragma once

#include <SkyboltEngine/VisHud.h>
#include <SkyboltVis/RenderContext.h>
#include <SkyboltVis/RenderOperation/RenderOperation.h>
#include <SkyboltVis/RenderOperation/RenderOperationOrder.h>
#include <SkyboltVis/Window/Window.h>

#include <osg/BufferObject>
#include <osg/Camera>
#include <osg/GL>
#include <osg/GLExtensions>
#include <osg/GraphicsContext>
#include <osg/Image>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <signal.h>
#include <sys/wait.h>
#endif

namespace skybolt {

namespace detail {

//! String of the operating system for paths and command lines: UTF-16 on Windows, UTF-8 otherwise
using NativeString = std::filesystem::path::string_type;

//! OpenGL sync object, which OSG declares in the namespace osg unless the OpenGL headers declare it
using GLsync = decltype(std::declval<osg::GLExtensions>().glFenceSync(0, 0));

inline NativeString toNative(const std::string& ascii)
{
	return NativeString(ascii.begin(), ascii.end());
}

//! @returns the path as UTF-8, e.g. for messages
inline std::string toUtf8(const std::filesystem::path& path)
{
	std::u8string s = path.u8string();
	return std::string(s.begin(), s.end());
}

//! @returns the absolute path as UTF-8, or the path if it has none
inline std::string toAbsoluteUtf8(const std::filesystem::path& path)
{
	std::error_code error;
	std::filesystem::path absolute = std::filesystem::absolute(path, error);
	return toUtf8(error ? path : absolute);
}

//! @returns the argument quoted for the shell that runs the commands of popen() and system(): cmd.exe on Windows, sh otherwise
inline NativeString quoteArgument(const NativeString& argument)
{
	using Char = NativeString::value_type;
#ifdef _WIN32
	// Paths cannot contain quotes, but the backslashes before the closing quote must be doubled
	NativeString quoted = argument;
	for (size_t i = argument.size(); i > 0 && argument[i - 1] == Char('\\'); --i)
	{
		quoted += Char('\\');
	}
	return Char('"') + quoted + Char('"');
#else
	NativeString quoted(1, Char('\''));
	for (Char c : argument)
	{
		quoted += (c == Char('\'')) ? NativeString("'\\''") : NativeString(1, c);
	}
	return quoted + Char('\'');
#endif
}

//! @returns true if the command, whose arguments are quoted with quoteArgument(), exits with code 0. Its output is discarded.
inline bool runsSuccessfully(const NativeString& command)
{
#ifdef _WIN32
	// cmd.exe removes the first and the last quote of the command line, so that the quotes of the arguments are kept
	return _wsystem((L"\"" + command + L" >NUL 2>&1\"").c_str()) == 0;
#else
	int status = std::system((command + " >/dev/null 2>&1").c_str());
	return status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}

//! Process started by the shell, whose standard input is written through a pipe
class PipeProcess
{
public:
	//! @param command line, whose arguments are quoted with quoteArgument()
	explicit PipeProcess(const NativeString& command)
	{
#ifdef _WIN32
		mPipe = _wpopen((L"\"" + command + L"\"").c_str(), L"wb"); // the quotes are removed by cmd.exe, see runsSuccessfully()
#else
		mPipe = popen(command.c_str(), "w");
#endif
		if (!mPipe)
		{
			throw std::runtime_error("FlightSim: could not start the process " + toUtf8(std::filesystem::path(command)));
		}
		std::setvbuf(mPipe, nullptr, _IONBF, 0); // the data is written in large pieces anyway
	}

	~PipeProcess()
	{
		close();
	}

	PipeProcess(const PipeProcess&) = delete;
	PipeProcess& operator=(const PipeProcess&) = delete;

	//! @returns false if not all data was written, e.g. because the process exited
	bool write(const void* data, size_t size)
	{
		return mPipe && std::fwrite(data, 1, size, mPipe) == size;
	}

	//! Closes the pipe and waits until the process has exited
	//! @returns the exit code of the process
	int close()
	{
		if (mPipe)
		{
#ifdef _WIN32
			mExitCode = _pclose(mPipe);
#else
			int status = pclose(mPipe);
			mExitCode = (status != -1 && WIFEXITED(status)) ? WEXITSTATUS(status) : -1;
#endif
			mPipe = nullptr;
		}
		return mExitCode;
	}

private:
	std::FILE* mPipe = nullptr;
	int mExitCode = -1;
};

} // namespace detail

//! Writes frames into an H.264 MP4 video file with ffmpeg, which reads them from a pipe. A thread writes the frames into the pipe,
//! so that the encoding does not hold up the caller, unless too many frames are waiting.
class FfmpegVideoWriter
{
public:
	using Frame = std::shared_ptr<const std::vector<std::uint8_t>>; //!< BGRA pixels, rows from bottom to top

	//! @param width and height in pixels, which must be even for the chroma subsampling of the video
	FfmpegVideoWriter(const std::filesystem::path& ffmpeg, const std::filesystem::path& file, int width, int height, double fps) :
		mFrameSize(size_t(width) * size_t(height) * 4),
		mProcess(createCommand(ffmpeg, file, width, height, fps))
	{
		mThread = std::thread([this] { writeQueuedFrames(); });
	}

	~FfmpegVideoWriter()
	{
		finish();
	}

	FfmpegVideoWriter(const FfmpegVideoWriter&) = delete;
	FfmpegVideoWriter& operator=(const FfmpegVideoWriter&) = delete;

	//! Queues the frame to be written count times, waits while too many frames are queued
	//! @returns false if writing failed, e.g. because ffmpeg exited
	bool write(const Frame& frame, int count = 1)
	{
		std::unique_lock<std::mutex> lock(mMutex);
		for (int i = 0; i < count && !mFailed; ++i)
		{
			mCondition.wait(lock, [this] { return mQueue.size() < msMaxQueuedFrames || mFailed; });
			if (!mFailed)
			{
				mQueue.push_back(frame);
				mCondition.notify_all();
			}
		}
		return !mFailed;
	}

	//! Writes the queued frames and waits until ffmpeg has finished the file
	//! @returns true if the file was written successfully
	bool finish()
	{
		if (mThread.joinable())
		{
			{
				std::lock_guard<std::mutex> lock(mMutex);
				mClosed = true;
			}
			mCondition.notify_all();
			mThread.join();
			int exitCode = mProcess.close();
			mSucceeded = !mFailed && exitCode == 0;
		}
		return mSucceeded;
	}

private:
	static detail::NativeString createCommand(const std::filesystem::path& ffmpeg, const std::filesystem::path& file, int width, int height, double fps)
	{
		std::ostringstream fpsText;
		fpsText << std::setprecision(12) << fps;

		return detail::quoteArgument(ffmpeg.native()) + detail::toNative(
			" -hide_banner -loglevel error -y"
			" -f rawvideo -pix_fmt bgra -video_size " + std::to_string(width) + "x" + std::to_string(height) +
			" -framerate " + fpsText.str() + " -i -"
			// Flipped, because OpenGL reads the rows from bottom to top. The colors are converted and tagged as BT.709, the color space
			// of HD video. The tags are set on the frames, from which ffmpeg 7 takes them.
			" -vf vflip,scale=out_color_matrix=bt709:out_range=tv,format=yuv420p,setparams=colorspace=bt709:color_primaries=bt709:color_trc=bt709:range=tv"
			" -c:v libx264 -preset veryfast -crf 18"
			" -movflags +faststart ") + detail::quoteArgument(file.native());
	}

	void writeQueuedFrames()
	{
#ifndef _WIN32
		// Writing into the pipe after ffmpeg exited fails instead of ending the process with SIGPIPE
		sigset_t signals;
		sigemptyset(&signals);
		sigaddset(&signals, SIGPIPE);
		pthread_sigmask(SIG_BLOCK, &signals, nullptr);
#endif
		std::unique_lock<std::mutex> lock(mMutex);
		while (true)
		{
			mCondition.wait(lock, [this] { return !mQueue.empty() || mClosed; });
			if (mQueue.empty())
			{
				return; // closed
			}

			// The frame stays in the queue while it is written, so that the queue limits all frames in memory
			Frame frame = mQueue.front();
			lock.unlock();
			bool written = frame && frame->size() == mFrameSize && mProcess.write(frame->data(), frame->size());
			lock.lock();

			mQueue.pop_front();
			if (!written)
			{
				mFailed = true;
				mQueue.clear();
			}
			mCondition.notify_all();
			if (mFailed)
			{
				return;
			}
		}
	}

private:
	static constexpr size_t msMaxQueuedFrames = 16;

	const size_t mFrameSize; //!< bytes
	detail::PipeProcess mProcess;
	std::thread mThread;
	std::mutex mMutex;
	std::condition_variable mCondition;
	std::deque<Frame> mQueue;
	bool mClosed = false;
	bool mFailed = false;
	bool mSucceeded = false;
};

//! Reads frames of the window when requested, at the position of the operation in the window's render operations, i.e. after the
//! operations with a lower priority and before those with a higher priority.
//! The GPU copies each frame into a pixel buffer object, which is read when the GPU has finished, usually a few frames later.
//! Reading synchronously would wait until the GPU has rendered all frames queued by the driver, which took over 100 ms per frame
//! with an integrated GPU.
class FrameCapture : public vis::RenderOperation
{
public:
	struct Frame
	{
		FfmpegVideoWriter::Frame pixels;
		int count; //!< of the request
	};

	FrameCapture() :
		mReader(new FrameReader)
	{
		// Post-render camera without children, which only reads the frame buffer when it is drawn
		osg::ref_ptr<osg::Camera> camera = new osg::Camera;
		camera->setRenderOrder(osg::Camera::POST_RENDER);
		camera->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
		camera->setClearMask(0);
		camera->setAllowEventFocus(false);
		camera->setFinalDrawCallback(mReader.get());
		addChild(camera);
	}

	//! Reads the width x height pixels at the bottom left of the next rendered frame
	//! @param count is passed on to the Frame
	void request(int width, int height, int count)
	{
		mReader->request = Request{osg::Vec2i(width, height), count};
	}

	void cancelRequest()
	{
		mReader->request.reset();
	}

	//! @returns the frames read since the last call, in the order of their requests
	std::vector<Frame> take()
	{
		return std::exchange(mReader->frames, {});
	}

	//! Waits until the GPU has copied the requested frames, reads them (see take()) and deletes the pixel buffer objects.
	//! Makes the OpenGL context current, so it must not be called while rendering.
	void finish(osg::GraphicsContext& context)
	{
		mReader->request.reset();
		if (mReader->buffers.empty())
		{
			return;
		}
		const bool current = context.isCurrent(); // if the viewer keeps it current between frames
		if (context.valid() && (current || context.makeCurrent()))
		{
			mReader->finish(*context.getState()->get<osg::GLExtensions>());
			if (!current)
			{
				context.releaseContext();
			}
		}
		// Otherwise the window was closed with its OpenGL objects
		mReader->pending.clear();
		mReader->buffers.clear();
		mReader->freeBuffers.clear();
	}

private:
	struct Request
	{
		osg::Vec2i size;
		int count;
	};

	//! The viewer of VisRoot is single-threaded, so the callback runs in the thread of the other functions
	struct FrameReader : public osg::Camera::DrawCallback
	{
		//! Copy of a frame into a pixel buffer object by the GPU
		struct Copy
		{
			GLuint buffer;
			size_t size; //!< bytes
			detail::GLsync fence;
			int count;
		};

		void operator()(osg::RenderInfo& renderInfo) const override
		{
			if (!request && pending.empty())
			{
				return;
			}
			osg::State& state = *renderInfo.getState();
			const osg::GLExtensions& extensions = *state.get<osg::GLExtensions>();
			readCopied(extensions, false);
			if (request)
			{
				const osg::GraphicsContext* context = state.getGraphicsContext();
				const osg::GraphicsContext::Traits* traits = context ? context->getTraits() : nullptr;
				glReadBuffer((traits && !traits->doubleBuffer) ? GL_FRONT : GL_BACK);
				glPixelStorei(GL_PACK_ALIGNMENT, 1);
				startCopy(extensions);
				request.reset();
			}
		}

		//! Starts copying the requested pixels into a pixel buffer object, or reads them right away without the OpenGL functions for it
		void startCopy(const osg::GLExtensions& extensions) const
		{
			const int width = request->size.x();
			const int height = request->size.y();
			const size_t size = size_t(width) * size_t(height) * 4;
			if (!extensions.isPBOSupported || !extensions.glFenceSync)
			{
				auto pixels = std::make_shared<std::vector<std::uint8_t>>(size);
				glReadPixels(0, 0, width, height, GL_BGRA, GL_UNSIGNED_BYTE, pixels->data());
				frames.push_back({std::move(pixels), request->count});
				return;
			}

			if (freeBuffers.empty() && pending.size() >= msMaxPendingCopies)
			{
				readCopied(extensions, true); // frees the buffer of the oldest copy
			}
			GLuint buffer = 0;
			if (!freeBuffers.empty())
			{
				buffer = freeBuffers.back();
				freeBuffers.pop_back();
			}
			else
			{
				extensions.glGenBuffers(1, &buffer);
			}

			extensions.glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, buffer);
			if (buffers[buffer] != size)
			{
				extensions.glBufferData(GL_PIXEL_PACK_BUFFER_ARB, std::ptrdiff_t(size), nullptr, GL_STREAM_READ);
				buffers[buffer] = size;
			}
			glReadPixels(0, 0, width, height, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
			extensions.glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, 0);
			pending.push_back({buffer, size, extensions.glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0), request->count});
		}

		//! Reads the pixels of the copies that the GPU has finished, in the order of the requests
		//! @param waitForFirst waits up to msMaxWait for the first copy
		void readCopied(const osg::GLExtensions& extensions, bool waitForFirst) const
		{
			for (bool wait = waitForFirst; !pending.empty(); wait = false)
			{
				const Copy& copy = pending.front();
				GLenum status = extensions.glClientWaitSync(copy.fence, wait ? GL_SYNC_FLUSH_COMMANDS_BIT : 0, wait ? msMaxWait : 0);
				if (status == GL_TIMEOUT_EXPIRED)
				{
					return;
				}
				extensions.glDeleteSync(copy.fence);

				auto pixels = std::make_shared<std::vector<std::uint8_t>>(copy.size);
				extensions.glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, copy.buffer);
				extensions.glGetBufferSubData(GL_PIXEL_PACK_BUFFER_ARB, 0, std::ptrdiff_t(copy.size), pixels->data());
				extensions.glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, 0);
				frames.push_back({std::move(pixels), copy.count});
				freeBuffers.push_back(copy.buffer);
				pending.pop_front();
			}
		}

		//! Reads all copies, waiting for them, and deletes the pixel buffer objects
		void finish(const osg::GLExtensions& extensions) const
		{
			while (!pending.empty())
			{
				size_t count = pending.size();
				readCopied(extensions, true);
				if (pending.size() == count) // the GPU did not finish within msMaxWait
				{
					for (const Copy& copy : pending)
					{
						extensions.glDeleteSync(copy.fence);
					}
					pending.clear();
				}
			}
			for (const auto& [buffer, size] : buffers)
			{
				extensions.glDeleteBuffers(1, &buffer);
			}
		}

		static constexpr size_t msMaxPendingCopies = 8;
		static constexpr std::uint64_t msMaxWait = 1000000000; //!< ns

		mutable std::optional<Request> request;
		mutable std::deque<Copy> pending;
		mutable std::map<GLuint, size_t> buffers; //!< pixel buffer objects with their sizes in bytes
		mutable std::vector<GLuint> freeBuffers;
		mutable std::vector<Frame> frames; //!< read, not taken yet
	};

	osg::ref_ptr<FrameReader> mReader;
};

//! Red dot in the top left corner of the window, shown while recording
class RecordingIndicator : public vis::RenderOperation
{
public:
	RecordingIndicator() :
		mHud(new VisHud)
	{
		mHud->setColor(osg::Vec4f(1.0f, 0.0f, 0.0f, 1.0f));
	}

	void setVisible(bool visible)
	{
		if (visible && !isVisible())
		{
			addChild(mHud);
		}
		else if (!visible && isVisible())
		{
			removeChild(mHud);
		}
	}

	bool isVisible() const { return mHud->getNumParents() > 0; }

	void updatePreRender(const vis::RenderContext& context) override
	{
		int height = context.targetDimensions.y();
		if (height <= 0 || context.targetDimensions.x() <= 0)
		{
			return;
		}
		float aspectRatio = vis::calcAspectRatio(context);
		if (aspectRatio == mAspectRatio && height == mHeight)
		{
			return; // already drawn for this size
		}
		mAspectRatio = aspectRatio;
		mHeight = height;

		// Disk of one box per row of pixels, in the coordinates of the HUD, which are -1 to 1 from the bottom to the top
		mHud->setAspectRatio(aspectRatio);
		mHud->clear();
		const float pixel = 2.0f / float(height);
		const glm::vec2 center(-aspectRatio + msMargin, 1.0f - msMargin);
		const int rows = std::max(1, int(std::round(2.0f * msRadius / pixel)));
		for (int i = 0; i < rows; ++i)
		{
			float y = (float(i) + 0.5f) * pixel - 0.5f * float(rows) * pixel;
			float halfWidth = std::sqrt(std::max(0.0f, msRadius * msRadius - y * y));
			mHud->drawSolidBox(center + glm::vec2(0.0f, y), 2.0f * halfWidth, pixel);
		}
	}

private:
	static constexpr float msRadius = 0.03f; //!< in half heights of the window
	static constexpr float msMargin = 0.065f; //!< distance of the center from the left and the top edge

	osg::ref_ptr<VisHud> mHud;
	float mAspectRatio = 0;
	int mHeight = 0;
};

//! Records the view of a window into MP4 video files with ffmpeg. The operations of the window with the priority
//! vis::RenderOperationOrder::Hud, e.g. the help message and the red dot of RecordingIndicator shown while recording, are not recorded.
//! The frames are taken at a constant rate of simulation time, each from the simulation step closest to its time, so that the video
//! plays at the speed of the simulation, however fast it is rendered. Frames of steps that were not rendered repeat the next
//! rendered frame, gaps of more than a second without rendered frames are left out.
class VideoRecorder
{
public:
	explicit VideoRecorder(vis::Window& window) :
		mWindow(window),
		mCapture(new FrameCapture),
		mIndicator(new RecordingIndicator)
	{
		// After the view and before the operations of the HUD, which are therefore not recorded
		mWindow.getRenderOperationSequence().addOperation(mCapture, int(vis::RenderOperationOrder::Hud) - 1);
		mWindow.getRenderOperationSequence().addOperation(mIndicator, int(vis::RenderOperationOrder::Hud));
	}

	~VideoRecorder()
	{
		stop();
		mWindow.getRenderOperationSequence().removeOperation(mCapture);
		mWindow.getRenderOperationSequence().removeOperation(mIndicator);
	}

	VideoRecorder(const VideoRecorder&) = delete;
	VideoRecorder& operator=(const VideoRecorder&) = delete;

	//! @returns a new file in the directory, named after the current date and time, e.g. flightsim_2026-10-26_14-03-22.mp4
	static std::filesystem::path createFilePath(const std::filesystem::path& directory)
	{
		std::time_t now = std::time(nullptr);
		std::ostringstream name;
		name << "flightsim_" << std::put_time(std::localtime(&now), "%Y-%m-%d_%H-%M-%S");
		std::filesystem::path file = directory / (name.str() + ".mp4");
		for (int i = 2; std::filesystem::exists(file); ++i)
		{
			file = directory / (name.str() + "_" + std::to_string(i) + ".mp4");
		}
		return file;
	}

	//! @returns the ffmpeg of the environment variable FLIGHTSIM_FFMPEG, which flightsim_env.py sets to the one of the Python package
	//! imageio-ffmpeg, otherwise ffmpeg of PATH
	static std::filesystem::path getFfmpegExecutable()
	{
#ifdef _WIN32
		const wchar_t* ffmpeg = _wgetenv(L"FLIGHTSIM_FFMPEG");
#else
		const char* ffmpeg = std::getenv("FLIGHTSIM_FFMPEG");
#endif
		return (ffmpeg && *ffmpeg) ? std::filesystem::path(ffmpeg) : std::filesystem::path("ffmpeg");
	}

	bool isRecording() const { return mWriter != nullptr; }

	//! Starts recording into a new video file, whose folder is created if needed
	//! @param time is the simulation time in s of the first frame
	//! @param stepSize is the simulation step in s
	void start(const std::filesystem::path& file, double fps, double time, double stepSize)
	{
		if (mWriter)
		{
			throw std::runtime_error("FlightSim: already recording into " + detail::toAbsoluteUtf8(mFile));
		}
		if (!(fps > 0))
		{
			throw std::invalid_argument("FlightSim: the frame rate of the recording must be positive");
		}

		// The size of the video is even for the chroma subsampling of H.264
		int width = mWindow.getWidth() & ~1;
		int height = mWindow.getHeight() & ~1;
		if (width <= 0 || height <= 0)
		{
			throw std::runtime_error("FlightSim: the window is too small to record");
		}

		std::filesystem::path ffmpeg = getFfmpegExecutable();
		if (!detail::runsSuccessfully(detail::quoteArgument(ffmpeg.native()) + detail::toNative(" -version")))
		{
			throw std::runtime_error("FlightSim: could not run ffmpeg (" + detail::toUtf8(ffmpeg) + ") to record a video. Install the Python package "
				"imageio-ffmpeg (flightsim_env.py sets the environment variable FLIGHTSIM_FFMPEG to its ffmpeg) or ffmpeg on PATH.");
		}

		if (file.has_parent_path())
		{
			std::filesystem::create_directories(file.parent_path());
		}
		mWriter = std::make_unique<FfmpegVideoWriter>(ffmpeg, file, width, height, fps);
		mFile = file;
		mWidth = width;
		mHeight = height;
		mFps = fps;
		mStartTime = time;
		mHalfStepSize = 0.5 * stepSize;
		mNextFrame = 0;
		mWrittenFrames = 0;
		mIndicator->setVisible(true);
		std::cout << "FlightSim: recording " << detail::toAbsoluteUtf8(mFile) << std::endl;
	}

	//! Stops recording and finishes the video file. Not called while rendering, see FrameCapture::finish().
	void stop()
	{
		if (!mWriter)
		{
			return;
		}
		mIndicator->setVisible(false);
		if (osg::GraphicsContext* context = mWindow.getView()->getCamera()->getGraphicsContext(); context)
		{
			mCapture->finish(*context);
		}
		writeFrames(mCapture->take());
		bool succeeded = mWriter->finish();
		mWriter.reset();

		if (succeeded)
		{
			std::ostringstream message;
			message << "FlightSim: recorded " << std::fixed << std::setprecision(1) << double(mWrittenFrames) / mFps << " s into " << detail::toAbsoluteUtf8(mFile);
			std::cout << message.str() << std::endl;
		}
		else
		{
			std::cerr << "FlightSim: recording into " << detail::toAbsoluteUtf8(mFile) << " failed or was interrupted (e.g. ffmpeg by Ctrl+C), "
				"see the messages of ffmpeg" << std::endl;
		}
	}

	//! @returns true while recording if a frame of the video is due at the simulation time
	bool isFrameDue(double time) const
	{
		return mWriter && getLastDueFrame(time) >= mNextFrame;
	}

	//! Requests the frame of the video if one is due at the simulation time. Called before rendering.
	void beforeRender(double time)
	{
		if (!mWriter)
		{
			return;
		}
		if ((mWindow.getWidth() & ~1) != mWidth || (mWindow.getHeight() & ~1) != mHeight)
		{
			std::cerr << "FlightSim: the window size changed, so the recording is stopped" << std::endl;
			stop();
			return;
		}
		if (std::int64_t lastFrame = getLastDueFrame(time); lastFrame >= mNextFrame)
		{
			// The frame stands for all frames of the video up to its simulation step
			std::int64_t count = lastFrame - mNextFrame + 1;
			mNextFrame = lastFrame + 1;
			if (count > std::int64_t(std::ceil(mFps)))
			{
				count = 1; // leave out the gap
			}
			mCapture->request(mWidth, mHeight, int(count));
		}
	}

	//! Writes the frames read so far. Called after rendering.
	void afterRender()
	{
		mCapture->cancelRequest(); // if the frame was not drawn
		if (mWriter && !writeFrames(mCapture->take()))
		{
			stop(); // reports the error
		}
	}

private:
	//! @returns false if writing failed
	bool writeFrames(const std::vector<FrameCapture::Frame>& frames)
	{
		for (const FrameCapture::Frame& frame : frames)
		{
			if (!mWriter->write(frame.pixels, frame.count))
			{
				return false;
			}
			mWrittenFrames += frame.count;
		}
		return true;
	}

	//! @returns the index of the last frame of the video due at the simulation time. A frame is taken from the first step at most
	//! half a step before the time of the frame, i.e. from the step closest to it.
	std::int64_t getLastDueFrame(double time) const
	{
		return std::int64_t(std::floor((time - mStartTime + mHalfStepSize) * mFps));
	}

private:
	vis::Window& mWindow;
	osg::ref_ptr<FrameCapture> mCapture;
	osg::ref_ptr<RecordingIndicator> mIndicator;

	std::unique_ptr<FfmpegVideoWriter> mWriter; //!< null if not recording
	std::filesystem::path mFile;
	int mWidth = 0;
	int mHeight = 0;
	double mFps = 30;
	double mStartTime = 0; //!< simulation time of the first frame
	double mHalfStepSize = 0;
	std::int64_t mNextFrame = 0; //!< index of the next frame of the video
	std::int64_t mWrittenFrames = 0;
};

} // namespace skybolt
