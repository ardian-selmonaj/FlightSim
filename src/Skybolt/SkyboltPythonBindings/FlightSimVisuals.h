/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#pragma once

#include <SkyboltCommon/Math/FirstOrderLag.h>
#include <SkyboltCommon/Math/MathUtility.h>
#include <SkyboltEngine/VisHud.h>
#include <SkyboltEngine/SimVisBinding/GeocentricToNedConverter.h>
#include <SkyboltEngine/SimVisBinding/SimVisBinding.h>
#include <SkyboltSim/CameraController/CameraController.h>
#include <SkyboltSim/Components/CameraComponent.h>
#include <SkyboltSim/Components/Node.h>
#include <SkyboltSim/Spatial/Geocentric.h>
#include <SkyboltVis/DefaultRootNode.h>
#include <SkyboltVis/OsgGeometryHelpers.h>
#include <SkyboltVis/OsgStateSetHelpers.h>
#include <SkyboltVis/RenderContext.h>
#include <SkyboltVis/Scene.h>
#include <SkyboltVis/RenderOperation/RenderOperation.h>

#include <osg/BlendFunc>
#include <osg/Camera>
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/Program>
#include <osg/Uniform>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace skybolt {

namespace detail {

//! Tone mapping curve of Assets/Core/Shaders/ToneMapping.h (toneMap_Uchimura with the default parameters)
inline double toneMapUchimura(double x)
{
	const double P = 1.0, a = 1.0, m = 0.22, l = 0.4, c = 1.33, b = 0.0;
	double l0 = ((P - m) * l) / a;
	double S0 = m + l0;
	double S1 = m + a * l0;
	double C2 = (a * P) / (P - S1);
	double CP = -C2 / P;

	double t = math::clamp(x / m, 0.0, 1.0);
	double w0 = 1.0 - t * t * (3.0 - 2.0 * t); // 1 - smoothstep(0, m, x)
	double w2 = (x >= m + l0) ? 1.0 : 0.0;
	double w1 = 1.0 - w0 - w2;

	double T = m * std::pow(x / m, c) + b;
	double S = P - (P - S1) * std::exp(CP * (x - S0));
	double L = m + a * (x - m);
	return T * w0 + L * w1 + S * w2;
}

//! @returns the color to render in the scene, so that it is displayed with the given linear color after the tone mapping of
//! Assets/Core/Shaders/ToneMapping.h (brightness multiplier 4, white point 1). Unlit colors would otherwise be displayed too bright.
inline osg::Vec4f toSceneColor(const osg::Vec4f& displayColor)
{
	const double brightnessMultiplier = 4.0;
	osg::Vec4f result = displayColor;
	for (int i = 0; i < 3; ++i)
	{
		// Invert toneMap() by bisection, the curve is monotonic
		double target = math::clamp(double(displayColor[i]), 0.0, 1.0) * toneMapUchimura(1.0);
		double low = 0.0, high = 1.0;
		for (int j = 0; j < 40; ++j)
		{
			double mid = 0.5 * (low + high);
			(toneMapUchimura(mid) < target ? low : high) = mid;
		}
		result[i] = float(0.5 * (low + high) / brightnessMultiplier);
	}
	return result;
}

} // namespace detail

//! Camera showing all aircraft of a group, from slightly above and from behind a focus aircraft, which is in the
//! foreground with the others behind it. The distance is chosen so that the whole group is in view.
//! The mouse and A/D rotate around the group, the mouse up/down changes the elevation, the mouse wheel and W/S zoom.
class OverviewCameraController : public sim::CameraController
{
public:
	struct Aircraft
	{
		sim::Vector3 position;
		sim::Vector3 velocity;
	};

	using AircraftProvider = std::function<std::vector<Aircraft>()>; //!< aircraft to show
	using FocusProvider = std::function<std::optional<Aircraft>()>; //!< aircraft in the foreground, if any
	using AspectRatioProvider = std::function<double()>; //!< width / height of the view

	OverviewCameraController(sim::Entity* camera, AircraftProvider aircraft, FocusProvider focus, AspectRatioProvider aspectRatio) :
		sim::CameraController(camera),
		mAircraft(std::move(aircraft)),
		mFocus(std::move(focus)),
		mAspectRatio(std::move(aspectRatio))
	{
	}

	void setActive(bool active) override
	{
		sim::CameraController::setActive(active);
		mFiltered.reset(); // jump to the new view instead of moving there
	}

	void setInput(const Input& input) override { mInput = input; }

	void update(sim::SecondsD dt) override
	{
		sim::CameraState& state = mCameraComponent->getState();
		state.fovY = msFovY;
		state.nearClipDistance = 0.5;

		mYawOffset += (mInput.yawRate + mInput.rightSpeed * msKeyRotationRate) * dt;
		mElevation = math::clamp(mElevation - mInput.tiltRate * dt, msMinElevation, msMaxElevation);
		mZoom = math::clamp(mZoom + (mInput.zoomRate + mInput.forwardSpeed * msKeyZoomRate) * dt, -msMaxZoom, msMaxZoom);

		std::vector<Aircraft> aircraft = mAircraft();
		if (aircraft.empty())
		{
			return; // keep the current view
		}

		// Sphere around all aircraft
		sim::Vector3 center(0, 0, 0);
		for (const Aircraft& a : aircraft)
		{
			center += a.position;
		}
		center /= double(aircraft.size());

		double radius = 0;
		for (const Aircraft& a : aircraft)
		{
			radius = std::max(radius, glm::length(a.position - center));
		}
		radius += msAircraftRadius;

		// Viewing direction as azimuth in north-east-down: from the focus aircraft towards the center,
		// or the flight direction of the focus aircraft if it is (nearly) at the center
		std::optional<double> azimuth;
		if (std::optional<Aircraft> focus = mFocus(); focus)
		{
			sim::Matrix3 nedFromWorld = glm::transpose(sim::geocentricToLtpOrientation(center));
			sim::Vector3 toCenter = nedFromWorld * (center - focus->position);
			sim::Vector3 velocity = nedFromWorld * focus->velocity;
			if (std::hypot(toCenter.x, toCenter.y) > std::max(0.25 * radius, 100.0))
			{
				azimuth = std::atan2(toCenter.y, toCenter.x);
			}
			else if (std::hypot(velocity.x, velocity.y) > 1.0)
			{
				azimuth = std::atan2(velocity.y, velocity.x);
			}
		}

		// Distance at which the sphere fits into the view
		double fovX = 2.0 * std::atan(std::tan(msFovY * 0.5) * mAspectRatio());
		double halfAngle = 0.5 * std::min(double(msFovY), fovX);
		double distance = std::max(msMinDistance, radius / std::sin(halfAngle) * msMargin) * std::exp(-mZoom);

		if (!mFiltered)
		{
			mFiltered = Filtered{center, distance, azimuth.value_or(0.0)};
		}
		else
		{
			double f = calcFirstOrderLagInterpolationFactor(dt, msPositionTimeConstant);
			mFiltered->center = glm::mix(mFiltered->center, center, f);
			mFiltered->distance = glm::mix(mFiltered->distance, distance, f);
			if (azimuth)
			{
				mFiltered->azimuth += std::remainder(*azimuth - mFiltered->azimuth, 2.0 * math::piD())
					* calcFirstOrderLagInterpolationFactor(dt, msDirectionTimeConstant);
			}
		}

		double cameraAzimuth = mFiltered->azimuth + mYawOffset;
		sim::Vector3 forwardNed(
			std::cos(mElevation) * std::cos(cameraAzimuth),
			std::cos(mElevation) * std::sin(cameraAzimuth),
			std::sin(mElevation));
		sim::Vector3 position = mFiltered->center - sim::geocentricToLtpOrientation(mFiltered->center) * forwardNed * mFiltered->distance;

		// Look at the center, with a level horizon at the camera position
		sim::Matrix3 ltpOrientation = sim::geocentricToLtpOrientation(position);
		sim::Vector3 forward = glm::transpose(ltpOrientation) * glm::normalize(mFiltered->center - position);
		double yaw = std::atan2(forward.y, forward.x);
		double pitch = -std::asin(math::clamp(forward.z, -1.0, 1.0));

		mNodeComponent->setPosition(position);
		mNodeComponent->setOrientation(sim::Quaternion(ltpOrientation) * glm::angleAxis(yaw, sim::Vector3(0, 0, 1)) * glm::angleAxis(pitch, sim::Vector3(0, 1, 0)));
	}

private:
	static constexpr float msFovY = 0.6f;
	static constexpr double msAircraftRadius = 20; //!< m, added to the radius of the group
	static constexpr double msMinDistance = 60; //!< m
	static constexpr double msMargin = 1.15;
	static constexpr double msMinElevation = 0.03; //!< rad
	static constexpr double msMaxElevation = 1.55; //!< rad
	static constexpr double msMaxZoom = 3; //!< distance factor exp(-zoom)
	static constexpr double msKeyRotationRate = 1.0; //!< rad/s
	static constexpr double msKeyZoomRate = 1.0; //!< 1/s
	static constexpr double msPositionTimeConstant = 0.5; //!< s
	static constexpr double msDirectionTimeConstant = 1.5; //!< s

	AircraftProvider mAircraft;
	FocusProvider mFocus;
	AspectRatioProvider mAspectRatio;
	Input mInput = Input::zero();

	double mYawOffset = 0;
	double mElevation = 0.21; //!< rad below the horizon, 12 degrees shows the horizon as well
	double mZoom = 0;

	struct Filtered
	{
		sim::Vector3 center;
		double distance;
		double azimuth;
	};
	std::optional<Filtered> mFiltered;
};

namespace detail {

//! Node with the drawables of AircraftTrails and CannonTracers: unlit, transparent and visible from both sides
class TransparentLinesNode : public vis::DefaultRootNode
{
public:
	TransparentLinesNode(const osg::ref_ptr<osg::Program>& program) :
		mGeode(new osg::Geode)
	{
		osg::StateSet* stateSet = mGeode->getOrCreateStateSet();
		stateSet->setAttribute(program);
		stateSet->setMode(GL_CULL_FACE, osg::StateAttribute::OFF);
		vis::makeStateSetTransparent(*stateSet, vis::TransparencyMode::Classic);
		mTransform->addChild(mGeode);
	}

	~TransparentLinesNode() override
	{
		mTransform->removeChild(mGeode);
	}

	osg::Geode* getGeode() const { return mGeode; }

private:
	osg::ref_ptr<osg::Geode> mGeode;
};

//! Point of a line in the coordinates of the visuals
struct LinePoint
{
	osg::Vec3d position;
	osg::Vec4f color;
};

//! Camera in the coordinates of the visuals
struct VisCamera
{
	osg::Vec3d position;
	osg::Vec3d forward; //!< unit vector in the viewing direction
};

//! @returns the pieces of the line in front of the camera, cut at the near distance.
//! Vertices behind the camera must not be drawn, because the logarithmic depth of the shaders (DepthPrecision.h)
//! is undefined there, which drops whole triangles and made lines passing the camera flicker.
inline std::vector<std::vector<LinePoint>> clipToFront(const std::vector<LinePoint>& line, const VisCamera& camera, double nearDistance)
{
	auto depthOf = [&] (const LinePoint& point) { return (point.position - camera.position) * camera.forward; };

	std::vector<std::vector<LinePoint>> pieces;
	std::vector<LinePoint> piece;
	for (size_t i = 0; i < line.size(); ++i)
	{
		double depth = depthOf(line[i]);
		if (depth >= nearDistance)
		{
			piece.push_back(line[i]);
		}

		// Cut the segment to the next point where it crosses the near distance
		if (i + 1 < line.size())
		{
			double nextDepth = depthOf(line[i + 1]);
			if ((depth >= nearDistance) != (nextDepth >= nearDistance))
			{
				double t = (nearDistance - depth) / (nextDepth - depth);
				piece.push_back({line[i].position + (line[i + 1].position - line[i].position) * t,
					line[i].color + (line[i + 1].color - line[i].color) * float(t)});
				if (nextDepth < nearDistance)
				{
					pieces.push_back(std::move(piece));
					piece.clear();
				}
			}
		}
	}
	if (piece.size() >= 2)
	{
		pieces.push_back(std::move(piece));
	}
	return pieces;
}

//! Appends a piece of a line as a ribbon facing the camera, with the center opaque and transparent edges, which looks smooth.
//! Each point adds the vertices left, center and right, each segment four triangles.
//! @param halfWidthPerDistance half width of the ribbon in m per m distance from the camera, for a constant width on the screen
inline void appendRibbon(const std::vector<LinePoint>& piece, const VisCamera& camera, double halfWidthPerDistance,
	osg::Vec3Array& vertices, osg::Vec4Array& colors, osg::DrawElementsUInt& triangles)
{
	osg::Vec3d side(0, 0, 0);
	for (size_t i = 0; i < piece.size(); ++i)
	{
		// Extrude perpendicular to the line and to the view direction
		const osg::Vec3d& p = piece[i].position;
		osg::Vec3d toPoint = p - camera.position;
		osg::Vec3d tangent = piece[std::min(i + 1, piece.size() - 1)].position - piece[i > 0 ? i - 1 : 0].position;
		osg::Vec3d newSide = tangent ^ toPoint;
		if (newSide.normalize() > 0)
		{
			// Keep the orientation of the previous side, so that the ribbon does not twist
			side = (newSide * side < 0) ? -newSide : newSide;
		}
		osg::Vec3d offset = side * (halfWidthPerDistance * toPoint.length());
		const osg::Vec4f& color = piece[i].color;
		const osg::Vec4f edgeColor(color.r(), color.g(), color.b(), 0.0f);

		GLuint base = GLuint(vertices.size());
		vertices.push_back(osg::Vec3(p + offset));
		vertices.push_back(osg::Vec3(p));
		vertices.push_back(osg::Vec3(p - offset));
		colors.push_back(edgeColor);
		colors.push_back(color);
		colors.push_back(edgeColor);

		if (i > 0)
		{
			GLuint previous = base - 3;
			for (GLuint index : {previous, previous + 1, base, base, previous + 1, base + 1,
				previous + 1, previous + 2, base + 1, base + 1, previous + 2, base + 2})
			{
				triangles.push_back(index);
			}
		}
	}
}

//! Geometry of ribbons, rebuilt each time the visuals are updated.
//! The arrays are refilled instead of replaced: OSG recognizes changed arrays by their address and modified count, so new arrays
//! that were allocated at the address of the previously drawn ones were drawn with the offsets of the previous ones in the vertex
//! buffer, which made the colors of the trails flicker.
class RibbonGeometry
{
public:
	RibbonGeometry() :
		mGeometry(new osg::Geometry),
		mVertices(new osg::Vec3Array),
		mColors(new osg::Vec4Array),
		mTriangles(new osg::DrawElementsUInt(GL_TRIANGLES))
	{
		vis::configureDrawable(*mGeometry);
		mGeometry->setDataVariance(osg::Object::DYNAMIC); // the vertex array object sets the arrays again for each draw
		mGeometry->setVertexArray(mVertices);
		mGeometry->setColorArray(mColors, osg::Array::BIND_PER_VERTEX);
		mGeometry->addPrimitiveSet(mTriangles);
	}

	osg::Geometry* getGeometry() const { return mGeometry; }

	void setLines(const std::vector<std::vector<LinePoint>>& lines, const VisCamera& camera, double halfWidthPerDistance)
	{
		mVertices->clear();
		mColors->clear();
		mTriangles->clear();
		for (const std::vector<LinePoint>& line : lines)
		{
			for (const std::vector<LinePoint>& piece : clipToFront(line, camera, msNearDistance))
			{
				appendRibbon(piece, camera, halfWidthPerDistance, *mVertices, *mColors, *mTriangles);
			}
		}

		// Uploads the arrays and sets their new offsets in the vertex buffer
		mVertices->dirty();
		mColors->dirty();
		mTriangles->dirty();
		mGeometry->dirtyBound();
	}

private:
	static constexpr double msNearDistance = 1.0; //!< m in front of the camera, where lines are cut

	osg::ref_ptr<osg::Geometry> mGeometry;
	osg::ref_ptr<osg::Vec3Array> mVertices;
	osg::ref_ptr<osg::Vec4Array> mColors;
	osg::ref_ptr<osg::DrawElementsUInt> mTriangles;
};

} // namespace detail

//! Position and viewing direction of the camera in geocentric coordinates
struct CameraPose
{
	sim::Vector3 position;
	sim::Vector3 forward;
};

using CameraPoseProvider = std::function<CameraPose()>;

inline detail::VisCamera toVisCamera(const GeocentricToNedConverter& converter, const CameraPose& camera)
{
	osg::Vec3d position = converter.convertPosition(camera.position);
	osg::Vec3d forward = converter.convertPosition(camera.position + camera.forward) - position;
	forward.normalize();
	return {position, forward};
}

//! Shows the paths flown by aircraft as lines of constant screen width, which fade out along the path and end after a maximum length.
//! Each time the visuals are updated, the trails are extended to the current positions of the aircraft.
class AircraftTrails : public SimVisBinding
{
public:
	struct Aircraft
	{
		int id;
		sim::Vector3 position;
		osg::Vec4f color; //!< linear color as displayed, like the colors of VisHud
	};

	using AircraftProvider = std::function<std::vector<Aircraft>()>; //!< aircraft whose trails are extended
	using PixelSizeProvider = std::function<double()>; //!< size in m of a pixel at a distance of 1 m from the camera

	AircraftTrails(vis::Scene* scene, const osg::ref_ptr<osg::Program>& program, AircraftProvider aircraft, CameraPoseProvider camera, PixelSizeProvider pixelSize) :
		mScene(scene),
		mNode(std::make_shared<detail::TransparentLinesNode>(program)),
		mAircraft(std::move(aircraft)),
		mCamera(std::move(camera)),
		mPixelSize(std::move(pixelSize))
	{
		mNode->getGeode()->addDrawable(mRibbons.getGeometry());
		mScene->addObject(mNode);
	}

	~AircraftTrails() override
	{
		mScene->removeObject(mNode);
	}

	void setVisible(bool visible) { mNode->setVisible(visible); }
	bool isVisible() const { return mNode->isVisible(); }

	//! @param length in m along the path
	void setMaxLength(double length) { mMaxLength = std::max(0.0, length); }
	double getMaxLength() const { return mMaxLength; }

	void clear(int id) { mTrails.erase(id); }
	void clearAll() { mTrails.clear(); }

	void syncVis(const GeocentricToNedConverter& converter) override
	{
		for (const Aircraft& aircraft : mAircraft())
		{
			auto it = mTrails.find(aircraft.id);
			if (it == mTrails.end())
			{
				it = mTrails.emplace(aircraft.id, createTrail(aircraft.position)).first;
			}
			it->second.color = detail::toSceneColor(aircraft.color);
			extend(it->second, aircraft.position);
		}

		std::vector<std::vector<detail::LinePoint>> lines;
		if (mMaxLength > 0)
		{
			for (const auto& [id, trail] : mTrails)
			{
				lines.push_back(toLine(trail, converter));
			}
		}
		mRibbons.setLines(lines, toVisCamera(converter, mCamera()), msHalfWidthPixels * mPixelSize());
	}

private:
	static constexpr double msPointSpacing = 25; //!< m between the points of a trail
	static constexpr double msHalfWidthPixels = 3.0; //!< the edges are transparent, so the line looks about half as wide
	static constexpr float msMaxAlpha = 0.9f;

	struct Trail
	{
		std::deque<sim::Vector3> points; //!< sampled positions, oldest first
		std::deque<double> distances; //!< distance flown at each sampled position
		sim::Vector3 head; //!< current position
		double headDistance = 0; //!< distance flown at the current position
		osg::Vec4f color;
	};

	static Trail createTrail(const sim::Vector3& position)
	{
		Trail trail;
		trail.points.push_back(position);
		trail.distances.push_back(0);
		trail.head = position;
		return trail;
	}

	void extend(Trail& trail, const sim::Vector3& position)
	{
		double step = glm::length(position - trail.points.back());
		trail.head = position;
		trail.headDistance = trail.distances.back() + step;
		if (step >= msPointSpacing)
		{
			trail.points.push_back(position);
			trail.distances.push_back(trail.headDistance);
		}

		// Drop the points beyond the maximum length, but keep the one that ends the faded part
		while (trail.points.size() > 1 && trail.headDistance - trail.distances[1] >= mMaxLength)
		{
			trail.points.pop_front();
			trail.distances.pop_front();
		}
	}

	//! @param age distance flown since the position
	detail::LinePoint toLinePoint(const Trail& trail, const GeocentricToNedConverter& converter, const sim::Vector3& position, double age) const
	{
		// Fade out with the distance flown since the position, faster at first so that the fading is visible on shorter trails
		double remaining = std::max(0.0, 1.0 - age / mMaxLength);
		return {converter.convertPosition(position),
			osg::Vec4f(trail.color.r(), trail.color.g(), trail.color.b(), trail.color.a() * msMaxAlpha * float(remaining * remaining))};
	}

	//! @returns the points of the trail, newest first
	std::vector<detail::LinePoint> toLine(const Trail& trail, const GeocentricToNedConverter& converter) const
	{
		std::vector<detail::LinePoint> line;
		line.reserve(trail.points.size() + 1);
		line.push_back(toLinePoint(trail, converter, trail.head, 0));
		for (size_t i = trail.points.size(); i-- > 0;)
		{
			if (glm::length(trail.points[i] - trail.head) > 0.01)
			{
				line.push_back(toLinePoint(trail, converter, trail.points[i], trail.headDistance - trail.distances[i]));
			}
		}
		return line;
	}

private:
	vis::Scene* mScene;
	std::shared_ptr<detail::TransparentLinesNode> mNode;
	detail::RibbonGeometry mRibbons;
	AircraftProvider mAircraft;
	CameraPoseProvider mCamera;
	PixelSizeProvider mPixelSize;
	double mMaxLength = 30000; //!< m
	std::map<int, Trail> mTrails;
};

//! Shows cannon shots as short dark lines, which fly in a straight line from where they are fired until their lifetime has passed
class CannonTracers : public SimVisBinding
{
public:
	using TimeProvider = std::function<double()>; //!< simulation time in s
	using PixelSizeProvider = AircraftTrails::PixelSizeProvider;

	CannonTracers(vis::Scene* scene, const osg::ref_ptr<osg::Program>& program, TimeProvider time, CameraPoseProvider camera, PixelSizeProvider pixelSize) :
		mScene(scene),
		mNode(std::make_shared<detail::TransparentLinesNode>(program)),
		mTime(std::move(time)),
		mCamera(std::move(camera)),
		mPixelSize(std::move(pixelSize))
	{
		mNode->getGeode()->addDrawable(mRibbons.getGeometry());
		mScene->addObject(mNode);
	}

	~CannonTracers() override
	{
		mScene->removeObject(mNode);
	}

	//! Adds a tracer at the given position and velocity at the current simulation time
	void add(const sim::Vector3& position, const sim::Vector3& velocity, double lifetime)
	{
		double time = mTime();
		mTracers.push_back({position, velocity, time, time + lifetime});
	}

	void clear() { mTracers.clear(); }

	void syncVis(const GeocentricToNedConverter& converter) override
	{
		double time = mTime();
		mTracers.erase(std::remove_if(mTracers.begin(), mTracers.end(), [time] (const Tracer& tracer) { return time >= tracer.endTime; }), mTracers.end());

		const osg::Vec4f color = detail::toSceneColor(msColor);
		std::vector<std::vector<detail::LinePoint>> lines;
		lines.reserve(mTracers.size());
		for (const Tracer& tracer : mTracers)
		{
			double age = std::max(0.0, time - tracer.startTime);
			double speed = glm::length(tracer.velocity);
			if (speed <= 0)
			{
				continue;
			}
			// From the head to the tail, which is shorter right after firing
			sim::Vector3 head = tracer.position + tracer.velocity * age;
			sim::Vector3 tail = head - tracer.velocity * (std::min(msLength, speed * age) / speed);
			lines.push_back({{converter.convertPosition(head), color}, {converter.convertPosition(tail), color}});
		}
		mRibbons.setLines(lines, toVisCamera(converter, mCamera()), msHalfWidthPixels * mPixelSize());
	}

private:
	static constexpr double msLength = 20; //!< m
	static constexpr double msHalfWidthPixels = 4.0; //!< the edges are transparent, so the line looks about 4 pixels wide
	static inline const osg::Vec4f msColor = osg::Vec4f(0.02f, 0.02f, 0.02f, 0.85f); //!< dark gray, as displayed

	struct Tracer
	{
		sim::Vector3 position; //!< at the start time
		sim::Vector3 velocity;
		double startTime;
		double endTime;
	};

	vis::Scene* mScene;
	std::shared_ptr<detail::TransparentLinesNode> mNode;
	detail::RibbonGeometry mRibbons;
	TimeProvider mTime;
	CameraPoseProvider mCamera;
	PixelSizeProvider mPixelSize;
	std::vector<Tracer> mTracers;
};

//! Shows the engine nozzles of an aircraft, e.g. of the camera target, as discs whose color goes linearly from black at idle to
//! dark red at full throttle. The nozzles come from the entity template of the aircraft (see FlightSim::add_aircraft()).
class NozzleGlow : public SimVisBinding
{
public:
	//! Exit of a nozzle in the body axes of the aircraft (x forward, y right, z down)
	struct Nozzle
	{
		sim::Vector3 positionRelBody; //!< center in m
		double radius; //!< m
	};

	struct Aircraft
	{
		sim::Vector3 position; //!< geocentric
		sim::Quaternion orientation; //!< geocentric
		std::vector<Nozzle> nozzles;
		double throttle; //!< 0 (idle) to 1 (full)
	};

	using AircraftProvider = std::function<std::optional<Aircraft>()>;

	NozzleGlow(vis::Scene* scene, const osg::ref_ptr<osg::Program>& program, AircraftProvider aircraft) :
		mScene(scene),
		mNode(std::make_shared<detail::TransparentLinesNode>(program)),
		mAircraft(std::move(aircraft)),
		mGeometry(new osg::Geometry),
		mVertices(new osg::Vec3Array),
		mColors(new osg::Vec4Array),
		mTriangles(new osg::DrawElementsUInt(GL_TRIANGLES))
	{
		// The arrays are refilled each time, see RibbonGeometry
		vis::configureDrawable(*mGeometry);
		mGeometry->setDataVariance(osg::Object::DYNAMIC);
		mGeometry->setVertexArray(mVertices);
		mGeometry->setColorArray(mColors, osg::Array::BIND_PER_VERTEX);
		mGeometry->addPrimitiveSet(mTriangles);
		mNode->getGeode()->addDrawable(mGeometry);
		mScene->addObject(mNode);
	}

	~NozzleGlow() override
	{
		mScene->removeObject(mNode);
	}

	void syncVis(const GeocentricToNedConverter& converter) override
	{
		mVertices->clear();
		mColors->clear();
		mTriangles->clear();
		if (std::optional<Aircraft> aircraft = mAircraft(); aircraft)
		{
			float throttle = float(math::clamp(aircraft->throttle, 0.0, 1.0));
			const osg::Vec4f color = detail::toSceneColor(msIdleColor * (1.0f - throttle) + msFullThrottleColor * throttle);
			auto addVertex = [&] (const sim::Vector3& positionRelBody) {
				mVertices->push_back(osg::Vec3(converter.convertPosition(aircraft->position + aircraft->orientation * positionRelBody)));
				mColors->push_back(color);
			};

			// A fan of triangles around the center of each nozzle, in the plane perpendicular to the body x axis
			for (const Nozzle& nozzle : aircraft->nozzles)
			{
				GLuint center = GLuint(mVertices->size());
				addVertex(nozzle.positionRelBody);
				for (int i = 0; i < msSegments; ++i)
				{
					double angle = 2.0 * math::piD() * double(i) / double(msSegments);
					addVertex(nozzle.positionRelBody + sim::Vector3(0, nozzle.radius * std::cos(angle), nozzle.radius * std::sin(angle)));
					for (GLuint index : {center, center + 1 + GLuint(i), center + 1 + GLuint((i + 1) % msSegments)})
					{
						mTriangles->push_back(index);
					}
				}
			}
		}
		mVertices->dirty();
		mColors->dirty();
		mTriangles->dirty();
		mGeometry->dirtyBound();
	}

private:
	static constexpr int msSegments = 32;
	static inline const osg::Vec4f msIdleColor = osg::Vec4f(0.0f, 0.0f, 0.0f, 1.0f); //!< as displayed
	static inline const osg::Vec4f msFullThrottleColor = osg::Vec4f(0.045f, 0.0012f, 0.0f, 1.0f); //!< dark red as displayed, in linear RGB

	vis::Scene* mScene;
	std::shared_ptr<detail::TransparentLinesNode> mNode;
	AircraftProvider mAircraft;
	osg::ref_ptr<osg::Geometry> mGeometry;
	osg::ref_ptr<osg::Vec3Array> mVertices;
	osg::ref_ptr<osg::Vec4Array> mColors;
	osg::ref_ptr<osg::DrawElementsUInt> mTriangles;
};

//! Map of the aircraft around a center aircraft, e.g. the camera target, in the bottom right corner of the window. Each aircraft is an
//! arrow in its color pointing in its heading, with its label and its distance (in 3D) to the center aircraft, and with its flown path
//! of the last msPathDuration seconds as dots. Aircraft beyond the range are shown by a triangle at the edge in their direction, with
//! their label and distance. The map is north-up or heading-up (the heading of the center aircraft points up), with a ring at the
//! range, a dashed ring at half of it and ticks every 30 degrees, marked N at north. The range is shown in the bottom left corner, the
//! label of the center aircraft in the bottom right corner. The texts of the aircraft in range are beside their
//! arrows, on the side where they cover the least of the other symbols and texts. A camera, e.g. the free camera, can be shown as a
//! light gray camera with dashed lines along its field of view, so that it is easier to turn it towards the aircraft.
//! It is drawn over the view like the labels, so videos show it.
class TacticalMinimap
{
public:
	struct Aircraft
	{
		int id; //!< keeps the place of the text of the aircraft from frame to frame
		sim::Vector3 position; //!< geocentric
		sim::Quaternion orientation; //!< geocentric
		osg::Vec4f color; //!< linear color as displayed, like the colors of VisHud
		std::string label; //!< may be empty
	};

	//! Camera shown on the map, e.g. the free camera
	struct Camera
	{
		sim::Vector3 position; //!< geocentric
		sim::Vector3 forward; //!< geocentric unit vector in the viewing direction
		double fieldOfView; //!< horizontal, in rad
	};

	//! @param parentCamera draws the map over the view, e.g. the camera of the final render target
	explicit TacticalMinimap(const osg::ref_ptr<osg::Camera>& parentCamera) :
		mParentCamera(parentCamera),
		mRoot(new osg::Group),
		mBackground(new VisHud),
		mGrid(new VisHud),
		mAircraftGroup(new osg::Group),
		mCenterHud(new VisHud),
		mCameraHud(new VisHud)
	{
		// The background is transparent
		mRoot->getOrCreateStateSet()->setAttributeAndModes(new osg::BlendFunc(osg::BlendFunc::SRC_ALPHA, osg::BlendFunc::ONE_MINUS_SRC_ALPHA));

		// The HUDs are drawn in this order, the center aircraft on top of the others and the camera on top of all
		mBackground->setColor(msBackgroundColor);
		mGrid->setColor(msGridColor);
		mCameraHud->setColor(msCameraColor);
		mRoot->addChild(mBackground);
		mRoot->addChild(mGrid);
		mRoot->addChild(mAircraftGroup);
		mRoot->addChild(mCenterHud);
		mRoot->addChild(mCameraHud);
		mParentCamera->addChild(mRoot);
	}

	~TacticalMinimap()
	{
		mParentCamera->removeChild(mRoot);
	}

	void setVisible(bool visible) { mRoot->setNodeMask(visible ? ~0u : 0u); }
	bool isVisible() const { return mRoot->getNodeMask() != 0; }

	//! @param range distance in m from the center of the map to the ring at its edge
	void setRange(double range) { mRange = range; }
	double getRange() const { return mRange; }

	//! @param headingUp true: the heading of the center aircraft points up, false: north points up
	void setHeadingUp(bool headingUp) { mHeadingUp = headingUp; }
	bool isHeadingUp() const { return mHeadingUp; }

	//! Extends the flown paths of the aircraft and draws the map if it is visible
	//! @param aspectRatio width / height of the view
	//! @param time simulation time in s
	//! @param center aircraft in the center of the map, none to draw no map
	//! @param others the other aircraft
	//! @param camera shown as a camera with dashed lines along its field of view, if any
	void update(float aspectRatio, double time, const std::optional<Aircraft>& center, const std::vector<Aircraft>& others,
		const std::optional<Camera>& camera)
	{
		updatePaths(time, center, others);
		if (!isVisible())
		{
			return;
		}
		mAspectRatio = aspectRatio;
		for (VisHud* hud : {mBackground.get(), mGrid.get(), mCenterHud.get(), mCameraHud.get()})
		{
			hud->clear();
			hud->setAspectRatio(aspectRatio);
		}
		for (ColorHud& colorHud : mAircraftHuds)
		{
			colorHud.hud->clear();
			colorHud.hud->setAspectRatio(aspectRatio);
			colorHud.used = false;
		}

		if (center)
		{
			drawMap(*center, others, camera);
		}

		// Remove the HUDs of colors that are no longer drawn
		for (auto it = mAircraftHuds.begin(); it != mAircraftHuds.end();)
		{
			if (it->used)
			{
				++it;
			}
			else
			{
				mAircraftGroup->removeChild(it->hud);
				it = mAircraftHuds.erase(it);
			}
		}
	}

	//! Removes the flown paths, e.g. after the aircraft were moved to new states
	void clearPaths() { mPaths.clear(); }
	void clearPath(int id) { mPaths.erase(id); }

private:
	//! Positions of an aircraft in the last msPathDuration seconds, oldest first
	struct PathPoint
	{
		double time; //!< s
		sim::Vector3 position; //!< geocentric
	};
	using Path = std::deque<PathPoint>;

	//! Extends the paths of the visible aircraft, those of the others are removed
	void updatePaths(double time, const std::optional<Aircraft>& center, const std::vector<Aircraft>& others)
	{
		std::map<int, Path> paths;
		auto extend = [&] (const Aircraft& aircraft) {
			Path& path = paths[aircraft.id];
			if (auto it = mPaths.find(aircraft.id); it != mPaths.end())
			{
				path = std::move(it->second);
			}

			// A new path after a jump, e.g. when the aircraft was moved to a new state, or if the time went back
			if (!path.empty())
			{
				const PathPoint& last = path.back();
				if (time < last.time || glm::length(aircraft.position - last.position) > msMaxPathSpeed * std::max(time - last.time, msPathSampleInterval))
				{
					path.clear();
				}
			}
			if (path.empty() || time - path.back().time >= msPathSampleInterval)
			{
				path.push_back({time, aircraft.position});
			}
			while (time - path.front().time > msPathDuration)
			{
				path.pop_front();
			}
		};
		if (center)
		{
			extend(*center);
		}
		for (const Aircraft& aircraft : others)
		{
			extend(aircraft);
		}
		mPaths = std::move(paths);
	}

	//! Draws dots at regular distances along a line on the map, starting at msPathDotStart from its first point, inside the ring
	void drawPathDots(VisHud& hud, const std::vector<glm::vec2>& line, const glm::vec2& mapCenter)
	{
		float next = msPathDotStart; // distance along the line of the next dot
		float travelled = 0.0f;
		for (size_t i = 1; i < line.size(); ++i)
		{
			glm::vec2 segment = line[i] - line[i - 1];
			float length = glm::length(segment);
			while (next <= travelled + length)
			{
				glm::vec2 dot = line[i - 1] + segment * ((next - travelled) / length);
				if (glm::length(dot - mapCenter) <= msRadius)
				{
					hud.drawSolidBox(dot, msPathDotSize, msPathDotSize);
				}
				next += msPathDotSpacing;
			}
			travelled += length;
		}
	}

	//! Rectangle on the map
	struct Box
	{
		glm::vec2 min;
		glm::vec2 max;
	};

	//! Side of an arrow where its text is drawn
	enum class Side
	{
		Right,
		Left,
		Below,
		Above
	};

	//! Text of an aircraft in range, drawn beside its arrow
	struct ArrowText
	{
		int id; //!< of the aircraft
		VisHud* hud;
		glm::vec2 arrowPosition;
		std::string label;
		std::string distance; //!< to the center aircraft
	};

	void drawMap(const Aircraft& center, const std::vector<Aircraft>& others, const std::optional<Camera>& camera)
	{
		// The map shows north and east at the center aircraft. Headings and bearings are clockwise from north, directions on the map
		// clockwise from up.
		const sim::Matrix3 nedFromWorld = glm::transpose(sim::geocentricToLtpOrientation(center.position));
		auto heading = [&] (const Aircraft& aircraft) {
			sim::Vector3 forward = nedFromWorld * (aircraft.orientation * sim::Vector3(1, 0, 0));
			return std::atan2(forward.y, forward.x);
		};
		const double up = mHeadingUp ? heading(center) : 0.0; // heading pointing up on the map
		const glm::vec2 mapCenter(mAspectRatio - msMargin - msPanelHalfSize, -1.0f + msMargin + msPanelHalfSize);

		// Areas that the texts of the aircraft in range should not cover
		std::vector<Box> occupied;
		drawGrid(mapCenter, up, occupied);

		mCenterHud->setColor(center.color);
		drawArrow(*mCenterHud, mapCenter, heading(center) - up, msCenterSymbolSize);
		occupied.push_back(calcArrowBox(mapCenter, msCenterSymbolSize));
		if (!center.label.empty())
		{
			glm::vec2 position = mapCenter + glm::vec2(msPanelHalfSize - msCornerInset, -msPanelHalfSize + msCornerInset + msTextCenterHeight);
			drawTextBlock(getAircraftHud(toTextColor(center.color)), position, "", center.label, HudDrawer::Alignment::Right);
			occupied.push_back(calcTextBlockBox(position, "", center.label, HudDrawer::Alignment::Right));
		}

		// Position on the map, beyond the ring for positions beyond the range
		auto toMap = [&] (const sim::Vector3& position) {
			sim::Vector3 offset = nedFromWorld * (position - center.position);
			double distance = std::hypot(offset.x, offset.y);
			return mapCenter + toMapDirection(std::atan2(offset.y, offset.x) - up) * float(msRadius * distance / mRange);
		};

		std::vector<ArrowText> arrowTexts;
		for (const Aircraft& aircraft : others)
		{
			sim::Vector3 offset = nedFromWorld * (aircraft.position - center.position);
			double distance = std::hypot(offset.x, offset.y); // on the map
			std::string distanceText = formatDistance(glm::length(offset)); // in 3D
			glm::vec2 direction = toMapDirection(std::atan2(offset.y, offset.x) - up);
			VisHud& hud = getAircraftHud(aircraft.color);
			VisHud& textHud = getAircraftHud(toTextColor(aircraft.color));

			// Flown path as dots behind the aircraft (the HUD draws them below the arrows)
			if (auto path = mPaths.find(aircraft.id); path != mPaths.end())
			{
				std::vector<glm::vec2> line = {toMap(aircraft.position)};
				for (auto point = path->second.rbegin(); point != path->second.rend(); ++point)
				{
					line.push_back(toMap(point->position));
				}
				drawPathDots(hud, line, mapCenter);
			}

			if (distance <= mRange)
			{
				glm::vec2 position = toMap(aircraft.position);
				drawArrow(hud, position, heading(aircraft) - up, msSymbolSize);
				occupied.push_back(calcArrowBox(position, msSymbolSize));
				arrowTexts.push_back({aircraft.id, &textHud, position, aircraft.label, distanceText});
			}
			else
			{
				// Triangle on the ring pointing outwards, with the label and the distance inside the ring
				glm::vec2 right(direction.y, -direction.x);
				glm::vec2 tip = mapCenter + direction * msRadius;
				glm::vec2 base = tip - direction * msEdgeMarkerLength;
				hud.drawSolidTriangle(tip, base - right * msEdgeMarkerHalfWidth, base + right * msEdgeMarkerHalfWidth);
				occupied.push_back(calcArrowBox(0.5f * (tip + base), msEdgeMarkerLength));

				HudDrawer::Alignment alignment = (direction.x > 0.35f) ? HudDrawer::Alignment::Right
					: (direction.x < -0.35f) ? HudDrawer::Alignment::Left : HudDrawer::Alignment::Center;
				glm::vec2 textPosition = mapCenter + direction * (msRadius - msEdgeTextInset);
				drawTextBlock(textHud, textPosition, aircraft.label, distanceText, alignment);
				occupied.push_back(calcTextBlockBox(textPosition, aircraft.label, distanceText, alignment));
			}
		}

		if (camera)
		{
			sim::Vector3 forward = nedFromWorld * camera->forward;
			double direction = std::atan2(forward.y, forward.x) - up;
			glm::vec2 position = toMap(camera->position);

			// Field of view as dashed lines from the camera to the ring
			for (double side : {-0.5, 0.5})
			{
				glm::vec2 end = position + toMapDirection(direction + side * camera->fieldOfView) * (glm::length(position - mapCenter) + 2.0f * msRadius);
				if (auto segment = clipToRing(position, end, mapCenter); segment)
				{
					mCameraHud->drawLineDashed(segment->first, segment->second, {msFieldOfViewDashLength, msFieldOfViewGapLength});
				}
			}

			// At the ring if the camera is beyond the range
			glm::vec2 offset = position - mapCenter;
			if (glm::length(offset) > msRadius)
			{
				position = mapCenter + offset * (msRadius / glm::length(offset));
			}
			drawCamera(*mCameraHud, position, direction, msCameraSymbolSize);
			occupied.push_back(calcArrowBox(position, msCameraSymbolSize));
		}

		// Label and distance beside each arrow, one after the other on the side where they cover the least of the occupied
		// areas and of the outside of the map. The previous side is kept while it is as good as the others, so the texts do not
		// jump between the sides. Otherwise the texts are preferably away from the center aircraft.
		const Box panel{mapCenter - glm::vec2(msPanelHalfSize), mapCenter + glm::vec2(msPanelHalfSize)};
		std::map<int, Side> sides;
		for (const ArrowText& text : arrowTexts)
		{
			bool left = text.arrowPosition.x < mapCenter.x;
			std::vector<Side> candidates = {left ? Side::Left : Side::Right, left ? Side::Right : Side::Left, Side::Below, Side::Above};
			if (auto previous = mArrowTextSides.find(text.id); previous != mArrowTextSides.end())
			{
				candidates.insert(candidates.begin(), previous->second);
			}

			Side bestSide = candidates.front();
			float bestCoverage = std::numeric_limits<float>::max();
			for (Side side : candidates)
			{
				auto [position, alignment] = calcArrowTextPlacement(text, side);
				Box box = calcTextBlockBox(position, text.label, text.distance, alignment);
				float coverage = calcArea(box) - calcOverlapArea(box, panel);
				for (const Box& other : occupied)
				{
					coverage += calcOverlapArea(box, other);
				}
				if (coverage < bestCoverage)
				{
					bestCoverage = coverage;
					bestSide = side;
				}
			}

			auto [position, alignment] = calcArrowTextPlacement(text, bestSide);
			drawTextBlock(*text.hud, position, text.label, text.distance, alignment);
			occupied.push_back(calcTextBlockBox(position, text.label, text.distance, alignment));
			sides[text.id] = bestSide;
		}
		mArrowTextSides = std::move(sides);
	}

	//! Draws the background, the rings, the ticks, N and the range, and adds the areas of the texts to occupied
	void drawGrid(const glm::vec2& mapCenter, double up, std::vector<Box>& occupied)
	{
		mBackground->drawSolidBox(mapCenter, 2.0f * msPanelHalfSize, 2.0f * msPanelHalfSize);

		mGrid->drawCircle(mapCenter, msRadius, 96);
		const int dashSegments = 48; // every other segment is drawn
		for (int i = 0; i < dashSegments; i += 2)
		{
			double angle = 2.0 * math::piD() * double(i) / double(dashSegments);
			double nextAngle = 2.0 * math::piD() * double(i + 1) / double(dashSegments);
			mGrid->drawLine(mapCenter + toMapDirection(angle) * (0.5f * msRadius), mapCenter + toMapDirection(nextAngle) * (0.5f * msRadius));
		}

		// Ticks every 30 degrees, the one at north longer and marked N
		for (int i = 0; i < 12; ++i)
		{
			glm::vec2 direction = toMapDirection(double(i) * math::piD() / 6.0 - up);
			float length = (i == 0) ? 2.0f * msTickLength : msTickLength;
			mGrid->drawLine(mapCenter + direction * msRadius, mapCenter + direction * (msRadius - length));
		}
		glm::vec2 north = mapCenter + toMapDirection(-up) * (msRadius - 2.0f * msTickLength - 0.6f * msTextSize);
		drawTextBlock(*mGrid, north, "", "N", HudDrawer::Alignment::Center);
		occupied.push_back(calcTextBlockBox(north, "", "N", HudDrawer::Alignment::Center));

		glm::vec2 rangePosition = mapCenter + glm::vec2(-msPanelHalfSize + msCornerInset, -msPanelHalfSize + msCornerInset + msTextCenterHeight);
		std::string range = formatRange(mRange);
		drawTextBlock(*mGrid, rangePosition, "", range, HudDrawer::Alignment::Left);
		occupied.push_back(calcTextBlockBox(rangePosition, "", range, HudDrawer::Alignment::Left));
	}

	//! Draws the label above the value, beside the position as given by the alignment, centered vertically on the position.
	//! Without a label, the value is centered vertically on the position.
	static void drawTextBlock(VisHud& hud, const glm::vec2& position, const std::string& label, const std::string& value, HudDrawer::Alignment alignment)
	{
		if (label.empty())
		{
			hud.drawText(position - glm::vec2(0.0f, msTextCenterHeight), value, 0.0f, msTextSize, alignment);
			return;
		}
		hud.drawText(position + glm::vec2(0.0f, 0.1f * msTextSize), label, 0.0f, msTextSize, alignment);
		hud.drawText(position - glm::vec2(0.0f, 1.05f * msTextSize), value, 0.0f, msTextSize, alignment);
	}

	//! @returns the height of a text block of drawTextBlock() above its position, to the top of the capitals of the upper line
	static float calcTextBlockHeight(const std::string& label)
	{
		return (label.empty() ? 0.4f : 0.85f) * msTextSize;
	}

	//! @returns the depth of a text block of drawTextBlock() below its position, to the descenders of the lower line
	static float calcTextBlockDepth(const std::string& label)
	{
		return (label.empty() ? 0.6f : 1.3f) * msTextSize;
	}

	//! @returns the approximate area of a text block drawn by drawTextBlock()
	static Box calcTextBlockBox(const glm::vec2& position, const std::string& label, const std::string& value, HudDrawer::Alignment alignment)
	{
		float width = estimateTextWidth(label.size() > value.size() ? label : value);
		float left = (alignment == HudDrawer::Alignment::Left) ? position.x
			: (alignment == HudDrawer::Alignment::Right) ? position.x - width : position.x - 0.5f * width;
		return {glm::vec2(left, position.y - calcTextBlockDepth(label)), glm::vec2(left + width, position.y + calcTextBlockHeight(label))};
	}

	//! @returns the position and alignment of the text block of an aircraft on the side of its arrow
	static std::pair<glm::vec2, HudDrawer::Alignment> calcArrowTextPlacement(const ArrowText& text, Side side)
	{
		const glm::vec2& arrow = text.arrowPosition;
		switch (side)
		{
			case Side::Left:
				return {arrow - glm::vec2(msTextOffset, 0.0f), HudDrawer::Alignment::Right};
			case Side::Below:
				return {arrow - glm::vec2(0.0f, msTextGap + calcTextBlockHeight(text.label)), HudDrawer::Alignment::Center};
			case Side::Above:
				return {arrow + glm::vec2(0.0f, msTextGap + calcTextBlockDepth(text.label)), HudDrawer::Alignment::Center};
			case Side::Right:
				break;
		}
		return {arrow + glm::vec2(msTextOffset, 0.0f), HudDrawer::Alignment::Left};
	}

	//! @returns the area around an arrow (or an edge marker) of the given length in any direction
	static Box calcArrowBox(const glm::vec2& position, float size)
	{
		return {position - glm::vec2(0.6f * size), position + glm::vec2(0.6f * size)};
	}

	static float calcArea(const Box& box)
	{
		return (box.max.x - box.min.x) * (box.max.y - box.min.y);
	}

	static float calcOverlapArea(const Box& a, const Box& b)
	{
		float width = std::min(a.max.x, b.max.x) - std::max(a.min.x, b.min.x);
		float height = std::min(a.max.y, b.max.y) - std::max(a.min.y, b.min.y);
		return (width > 0.0f && height > 0.0f) ? width * height : 0.0f;
	}

	//! Draws an arrow centered at the position, pointing in the direction (clockwise from up)
	static void drawArrow(VisHud& hud, const glm::vec2& position, double direction, float size)
	{
		glm::vec2 forward = toMapDirection(direction);
		glm::vec2 right(forward.y, -forward.x);
		glm::vec2 tip = position + forward * (0.6f * size);
		glm::vec2 notch = position - forward * (0.15f * size);
		glm::vec2 back = position - forward * (0.4f * size);
		hud.drawSolidTriangle(tip, back - right * (0.4f * size), notch);
		hud.drawSolidTriangle(tip, notch, back + right * (0.4f * size));
	}

	//! Draws a camera centered at the position, a body with a lens widening to the front, pointing in the direction (clockwise from up)
	static void drawCamera(VisHud& hud, const glm::vec2& position, double direction, float size)
	{
		glm::vec2 forward = toMapDirection(direction);
		glm::vec2 right(forward.y, -forward.x);
		auto at = [&] (float ahead, float aside) { return position + forward * (ahead * size) + right * (aside * size); };
		hud.drawSolidTriangle(at(-0.5f, -0.25f), at(0.05f, -0.25f), at(0.05f, 0.25f));
		hud.drawSolidTriangle(at(-0.5f, -0.25f), at(0.05f, 0.25f), at(-0.5f, 0.25f));
		hud.drawSolidTriangle(at(0.05f, -0.1f), at(0.45f, -0.25f), at(0.45f, 0.25f));
		hud.drawSolidTriangle(at(0.05f, -0.1f), at(0.45f, 0.25f), at(0.05f, 0.1f));
	}

	//! @returns the part of the line from a to b inside the ring, if any
	static std::optional<std::pair<glm::vec2, glm::vec2>> clipToRing(const glm::vec2& a, const glm::vec2& b, const glm::vec2& mapCenter)
	{
		// Solves |a + t (b - a) - mapCenter| = msRadius for t
		glm::vec2 u = a - mapCenter;
		glm::vec2 v = b - a;
		float vv = glm::dot(v, v);
		float uv = glm::dot(u, v);
		float discriminant = uv * uv - vv * (glm::dot(u, u) - msRadius * msRadius);
		if (vv <= 0.0f || discriminant <= 0.0f)
		{
			return std::nullopt;
		}
		float t0 = std::max(0.0f, (-uv - std::sqrt(discriminant)) / vv);
		float t1 = std::min(1.0f, (-uv + std::sqrt(discriminant)) / vv);
		if (t0 >= t1)
		{
			return std::nullopt;
		}
		return std::pair(a + v * t0, a + v * t1);
	}

	//! @returns the unit vector on the map in the direction (clockwise from up)
	static glm::vec2 toMapDirection(double direction)
	{
		return glm::vec2(std::sin(direction), std::cos(direction));
	}

	//! @returns the approximate width of the text, with the average character width of the default font (Verdana)
	static float estimateTextWidth(const std::string& text)
	{
		return 0.62f * msTextSize * float(text.size());
	}

	//! @returns the color of the texts of an aircraft: its color mixed with white, so that dark colors are readable on the background
	static osg::Vec4f toTextColor(const osg::Vec4f& color)
	{
		osg::Vec4f result = color;
		for (int i = 0; i < 3; ++i)
		{
			result[i] += (1.0f - result[i]) * msTextWhiteness;
		}
		return result;
	}

	//! @returns e.g. "850 m" (rounded to 10 m), "7.3 km" or "23 km" for a distance in m
	static std::string formatDistance(double distance)
	{
		char text[32];
		if (distance < 995.0)
		{
			std::snprintf(text, sizeof(text), "%.0f m", 10.0 * std::round(distance / 10.0));
		}
		else
		{
			std::snprintf(text, sizeof(text), (distance < 10000.0) ? "%.1f km" : "%.0f km", distance / 1000.0);
		}
		return text;
	}

	//! @returns e.g. "500 m", "7.5 km" or "20 km" for the range in m
	static std::string formatRange(double range)
	{
		char text[32];
		if (range < 1000.0)
		{
			std::snprintf(text, sizeof(text), "%.0f m", range);
		}
		else
		{
			double km = range / 1000.0;
			std::snprintf(text, sizeof(text), (std::abs(km - std::round(km)) < 0.05) ? "%.0f km" : "%.1f km", km);
		}
		return text;
	}

	struct ColorHud
	{
		osg::Vec4f color;
		osg::ref_ptr<VisHud> hud;
		bool used; //!< drawn in the current frame
	};

	//! @returns the HUD that draws the other aircraft and the texts of the aircraft in the color, created if there is none
	VisHud& getAircraftHud(const osg::Vec4f& color)
	{
		auto it = std::find_if(mAircraftHuds.begin(), mAircraftHuds.end(), [&] (const ColorHud& colorHud) { return colorHud.color == color; });
		if (it == mAircraftHuds.end())
		{
			osg::ref_ptr<VisHud> hud = new VisHud;
			hud->setColor(color);
			hud->setAspectRatio(mAspectRatio);
			mAircraftGroup->addChild(hud);
			it = mAircraftHuds.insert(mAircraftHuds.end(), ColorHud{color, hud, false});
		}
		it->used = true;
		return *it->hud;
	}

private:
	// Sizes in half heights of the window, like the coordinates of VisHud
	static constexpr float msRadius = 0.25f; //!< of the ring at the range
	static constexpr float msPanelHalfSize = 0.28f; //!< half the width and height of the square background
	static constexpr float msMargin = 0.04f; //!< between the background and the right and bottom edges of the window
	static constexpr float msSymbolSize = 0.04f; //!< length of the arrows
	static constexpr float msCenterSymbolSize = 0.048f; //!< length of the arrow of the center aircraft
	static constexpr float msTextSize = 0.032f;
	static constexpr float msTextCenterHeight = 0.36f * msTextSize; //!< above the baseline, half the height of the capitals
	static constexpr float msTextOffset = 0.034f; //!< from the center of an arrow to its text on its left or right
	static constexpr float msTextGap = 0.03f; //!< from the center of an arrow to its text below or above it
	static constexpr float msTextWhiteness = 0.45f; //!< of the text colors, see toTextColor()
	static constexpr float msCameraSymbolSize = 0.045f; //!< length of the camera
	static constexpr float msFieldOfViewDashLength = 0.01f;
	static constexpr float msFieldOfViewGapLength = 0.008f;
	static constexpr float msPathDotSize = 0.007f;
	static constexpr float msPathDotSpacing = 0.015f; //!< along the path on the map
	static constexpr float msPathDotStart = 0.025f; //!< from the center of the arrow to the first dot
	static constexpr double msPathDuration = 30; //!< s of the flown path shown
	static constexpr double msPathSampleInterval = 0.5; //!< s between the points of the paths
	static constexpr double msMaxPathSpeed = 1500; //!< m/s, faster movements start a new path
	static constexpr float msTickLength = 0.012f;
	static constexpr float msEdgeMarkerLength = 0.028f; //!< of the triangles of the aircraft beyond the range
	static constexpr float msEdgeMarkerHalfWidth = 0.013f;
	static constexpr float msEdgeTextInset = 0.065f; //!< from the ring to the text of the aircraft beyond the range
	static constexpr float msCornerInset = 0.014f; //!< from the corners of the background to the texts there
	static inline const osg::Vec4f msBackgroundColor = osg::Vec4f(0.0f, 0.0f, 0.0f, 0.7f); //!< blended in linear RGB, so it looks lighter
	static inline const osg::Vec4f msGridColor = osg::Vec4f(0.8f, 0.8f, 0.8f, 0.85f); //!< light gray as displayed, in linear RGB
	static inline const osg::Vec4f msCameraColor = osg::Vec4f(0.85f, 0.85f, 0.85f, 1.0f); //!< light gray, opaque unlike the grid

	osg::ref_ptr<osg::Camera> mParentCamera;
	osg::ref_ptr<osg::Group> mRoot;
	osg::ref_ptr<VisHud> mBackground;
	osg::ref_ptr<VisHud> mGrid; //!< rings, ticks and range
	osg::ref_ptr<osg::Group> mAircraftGroup; //!< HUDs of mAircraftHuds
	osg::ref_ptr<VisHud> mCenterHud;
	osg::ref_ptr<VisHud> mCameraHud;
	std::vector<ColorHud> mAircraftHuds; //!< of the other aircraft, one per color
	std::map<int, Side> mArrowTextSides; //!< of the texts of the aircraft in range in the last frame, by aircraft ID
	std::map<int, Path> mPaths; //!< flown paths of the visible aircraft, by aircraft ID
	float mAspectRatio = 1.0f;
	double mRange = 20000; //!< m
	bool mHeadingUp = false;
};

namespace detail {

inline const char* wezIndicatorsVertexSource = R"(#version 330
in vec4 osg_Vertex;
out vec2 hudPosition;
uniform mat4 osg_ModelViewProjectionMatrix;
void main()
{
	gl_Position = osg_ModelViewProjectionMatrix * osg_Vertex;
	hudPosition = osg_Vertex.xy;
}
)";

//! Fragment shader of WezIndicators, after "#version 330" and "#define MAX_THREATS <WezIndicators::msMaxThreats>".
//! Positions are in the coordinates of VisHud, half heights of the view from its center.
inline const char* wezIndicatorsFragmentSource = R"(
in vec2 hudPosition;
out vec4 color;

uniform float aspectRatio;
uniform vec2 target; // aircraft on the screen, where the beams end
uniform vec3 fireCue; // center and radius of the green circle, radius 0 for none
uniform vec4 fireCueColor; // linear RGB and opacity of the inside of the circle
uniform vec2 fireCueRim; // half width and opacity of the rim of the circle
uniform int threatCount;
// Per beam: its source, i.e. the opponent on the screen if it is in view, otherwise the edge of the view in its direction, and the
// weight of a red border all around the view instead of the beam, for an opponent behind the camera
uniform vec3 threats[MAX_THREATS];
uniform vec4 threatColor; // linear RGB and opacity at the source of a beam, and at the edge of the view for the border
uniform float threatFadeExponent; // of the opacity from the source of a beam (the edge of the view for the border) to the target
uniform float threatEdgeSharpness; // of the sides of the beams, 1 for soft sides
uniform float threatSourceHalfWidth; // of a beam at its source, where it has half the opacity
uniform float threatSpread; // tangent of the half angle of a beam, where it has half the opacity

// Distance from the target to the edge of the view in the unit direction
float distanceToEdge(vec2 direction)
{
	vec2 edge = vec2(direction.x >= 0.0 ? aspectRatio : -aspectRatio, direction.y >= 0.0 ? 1.0 : -1.0);
	float x = abs(direction.x) > 1e-5 ? (edge.x - target.x) / direction.x : 1e5;
	float y = abs(direction.y) > 1e-5 ? (edge.y - target.y) / direction.y : 1e5;
	return max(min(x, y), 1e-3);
}

// Opacity of a beam like a flashlight from the source to the target: narrowest and most opaque at the source, widening and fading
// out towards the target, where it ends
float beam(vec2 source)
{
	vec2 axis = target - source;
	float axisLength = length(axis);
	if (axisLength < 1e-4)
	{
		return 0.0;
	}
	axis /= axisLength;
	vec2 offset = hudPosition - source;
	float along = dot(offset, axis);
	// Distance from the axis in half widths, with a round end behind the source
	float halfWidths = along > 0.0 ? length(offset - along * axis) / (threatSourceHalfWidth + along * threatSpread)
		: length(offset) / threatSourceHalfWidth;
	float profile = exp2(-pow(halfWidths * halfWidths, threatEdgeSharpness));
	return profile * pow(clamp(1.0 - along / axisLength, 0.0, 1.0), threatFadeExponent);
}

void main()
{
	vec2 offset = hudPosition - target;
	float distance = length(offset);
	vec2 direction = distance > 1e-6 ? offset / distance : vec2(0.0);

	// Red border all around the view, most opaque at the edge and transparent at the target
	float border = pow(min(distance / distanceToEdge(direction), 1.0), threatFadeExponent);
	float threatAlpha = 0.0;
	for (int i = 0; i < threatCount; ++i)
	{
		float opacity = mix(beam(threats[i].xy), border, threats[i].z);
		threatAlpha = 1.0 - (1.0 - threatAlpha) * (1.0 - threatColor.a * opacity);
	}

	float fireCueAlpha = 0.0;
	if (fireCue.z > 0.0)
	{
		float d = length(hudPosition - fireCue.xy);
		float aa = fwidth(d); // smooth edges of about a pixel
		float inside = 1.0 - smoothstep(fireCue.z - aa, fireCue.z + aa, d);
		float rim = 1.0 - smoothstep(fireCueRim.x - aa, fireCueRim.x + aa, abs(d - fireCue.z));
		fireCueAlpha = max(inside * fireCueColor.a, rim * fireCueRim.y);
	}

	// The beams over the circle, so that a threat is also seen in the circle, e.g. of an opponent ahead
	float alpha = threatAlpha + fireCueAlpha * (1.0 - threatAlpha);
	vec3 rgb = (threatColor.rgb * threatAlpha + fireCueColor.rgb * fireCueAlpha * (1.0 - threatAlpha)) / max(alpha, 1e-6);
	color = vec4(rgb, alpha);
}
)";

} // namespace detail

//! Indicates over the view the weapon engagement zones (WEZ) of an aircraft, e.g. the camera target, and of its opponents: a light
//! green circle ahead of its nose while an opponent is in its WEZ, so that firing would make sense, and for each opponent that has the
//! aircraft in its WEZ a red beam like a flashlight from the opponent to the aircraft on the screen: narrow and most opaque at the opponent,
//! widening and fading out towards the aircraft.
//! A WEZ is a cone along the velocity of its aircraft (along its nose while it stands still) up to a range, like the one of the cannon of
//! HHMARL_3D_NEW (CannonSim). A beam starts at its opponent if it is in view, otherwise at the edge of the view in the direction where the
//! opponent would be seen by turning the camera towards it. For an opponent nearly behind the camera, whose direction on the screen changes
//! quickly, the beam turns into a red border all around the view.
//! It is drawn over the view below the labels and the minimap, so videos show it.
class WezIndicators
{
public:
	struct Aircraft
	{
		sim::Vector3 position; //!< geocentric
		sim::Quaternion orientation; //!< geocentric
		sim::Vector3 velocity; //!< geocentric
	};

	//! Camera of the view
	struct Camera
	{
		sim::Vector3 position; //!< geocentric
		sim::Quaternion orientation; //!< geocentric, x forward, y right, z down
		double fovY; //!< rad
	};

	//! @param parentCamera draws the indicators over the view, e.g. the camera of the final render target, below its other children
	explicit WezIndicators(const osg::ref_ptr<osg::Camera>& parentCamera) :
		mParentCamera(parentCamera),
		mCamera(new osg::Camera),
		mGeode(new osg::Geode),
		mAspectRatioUniform(new osg::Uniform("aspectRatio", 1.0f)),
		mTargetUniform(new osg::Uniform("target", osg::Vec2f(0, 0))),
		mFireCueUniform(new osg::Uniform("fireCue", osg::Vec3f(0, 0, 0))),
		mThreatCountUniform(new osg::Uniform("threatCount", 0)),
		mThreatsUniform(new osg::Uniform(osg::Uniform::FLOAT_VEC3, "threats", msMaxThreats))
	{
		// Like VisHud, in half heights of the view from its center
		mCamera->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
		mCamera->setViewMatrix(osg::Matrix::identity());
		mCamera->setProjectionMatrix(osg::Matrix::ortho2D(-1, 1, -1, 1));
		mCamera->setClearMask(0);
		mCamera->setRenderOrder(osg::Camera::POST_RENDER);
		mCamera->setAllowEventFocus(false);

		// A quad over the whole view, wide enough for any aspect ratio, whose fragment shader draws the indicators
		osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
		for (const osg::Vec3& vertex : {osg::Vec3(-100, -1, 0), osg::Vec3(100, -1, 0), osg::Vec3(-100, 1, 0), osg::Vec3(100, 1, 0)})
		{
			vertices->push_back(vertex);
		}
		osg::ref_ptr<osg::Geometry> quad = new osg::Geometry;
		vis::configureDrawable(*quad);
		quad->setVertexArray(vertices);
		quad->addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLE_STRIP, 0, 4));
		quad->setCullingActive(false);
		mGeode->addDrawable(quad);
		mGeode->setCullingActive(false);

		osg::ref_ptr<osg::Program> program = new osg::Program;
		program->addShader(new osg::Shader(osg::Shader::VERTEX, detail::wezIndicatorsVertexSource));
		program->addShader(new osg::Shader(osg::Shader::FRAGMENT, std::string("#version 330\n#define MAX_THREATS ")
			+ std::to_string(msMaxThreats) + "\n" + detail::wezIndicatorsFragmentSource));

		osg::StateSet* stateSet = mGeode->getOrCreateStateSet();
		stateSet->setAttributeAndModes(program, osg::StateAttribute::ON);
		stateSet->setAttributeAndModes(new osg::BlendFunc(osg::BlendFunc::SRC_ALPHA, osg::BlendFunc::ONE_MINUS_SRC_ALPHA));
		stateSet->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);
		stateSet->setMode(GL_CULL_FACE, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);
		for (osg::Uniform* uniform : {mAspectRatioUniform.get(), mTargetUniform.get(), mFireCueUniform.get(), mThreatCountUniform.get(), mThreatsUniform.get()})
		{
			stateSet->addUniform(uniform);
		}
		stateSet->addUniform(new osg::Uniform("fireCueColor", msFireCueColor));
		stateSet->addUniform(new osg::Uniform("fireCueRim", osg::Vec2f(msFireCueRimHalfWidth, msFireCueRimAlpha)));
		stateSet->addUniform(new osg::Uniform("threatColor", msThreatColor));
		stateSet->addUniform(new osg::Uniform("threatFadeExponent", msThreatFadeExponent));
		stateSet->addUniform(new osg::Uniform("threatEdgeSharpness", msThreatEdgeSharpness));
		stateSet->addUniform(new osg::Uniform("threatSourceHalfWidth", msThreatSourceHalfWidth));
		stateSet->addUniform(new osg::Uniform("threatSpread", float(std::tan(msThreatHalfAngle))));

		mGeode->setNodeMask(0u); // nothing to draw yet
		mCamera->addChild(mGeode);
		mParentCamera->insertChild(0, mCamera);
	}

	~WezIndicators()
	{
		mParentCamera->removeChild(mCamera);
	}

	void setVisible(bool visible) { mCamera->setNodeMask(visible ? ~0u : 0u); }
	bool isVisible() const { return mCamera->getNodeMask() != 0; }

	//! @param range of the WEZ in m
	void setRange(double range) { mRange = range; }
	double getRange() const { return mRange; }

	//! @param halfAngle of the cone of the WEZ in rad, the largest angle between the velocity and the line of sight to the target
	void setHalfAngle(double halfAngle) { mHalfAngle = halfAngle; }
	double getHalfAngle() const { return mHalfAngle; }

	//! @returns true if the position is in the WEZ of the aircraft
	bool isInZone(const Aircraft& aircraft, const sim::Vector3& position) const
	{
		sim::Vector3 lineOfSight = position - aircraft.position;
		double distance = glm::length(lineOfSight);
		double speed = glm::length(aircraft.velocity);
		sim::Vector3 direction = (speed > msMinSpeed) ? aircraft.velocity / speed : aircraft.orientation * sim::Vector3(1, 0, 0);
		return distance > 0.0 && distance < mRange && glm::dot(lineOfSight, direction) > distance * std::cos(mHalfAngle);
	}

	//! Draws the indicators if they are visible
	//! @param aspectRatio width / height of the view
	//! @param aircraft whose WEZ and threats are indicated, none to draw nothing
	//! @param opponents of the aircraft
	void update(float aspectRatio, const Camera& camera, const std::optional<Aircraft>& aircraft, const std::vector<Aircraft>& opponents)
	{
		mGeode->setNodeMask(0u);
		if (!isVisible() || !aircraft)
		{
			return;
		}

		const sim::Quaternion cameraFromWorld = glm::inverse(camera.orientation);
		const double tanHalfFovY = std::tan(0.5 * camera.fovY);
		// x forward, y right, z down
		auto toCamera = [&] (const sim::Vector3& position) { return cameraFromWorld * (position - camera.position); };
		// Position on the screen of a point in camera coordinates in front of the camera
		auto toScreen = [&] (const sim::Vector3& p) { return glm::vec2(float(p.y / (tanHalfFovY * p.x)), float(-p.z / (tanHalfFovY * p.x))); };

		// The beams converge at the aircraft on the screen, or at the center of the view if the camera is in it or close to it (cockpit)
		glm::vec2 target(0.0f);
		if (sim::Vector3 p = toCamera(aircraft->position); p.x > msMinTargetDepth)
		{
			target = glm::clamp(toScreen(p), glm::vec2(-aspectRatio, -1.0f), glm::vec2(aspectRatio, 1.0f));
		}

		// Green circle ahead of the nose, at the distance of the nearest opponent in the WEZ
		std::optional<double> nearest;
		for (const Aircraft& opponent : opponents)
		{
			if (isInZone(*aircraft, opponent.position))
			{
				double distance = glm::length(opponent.position - aircraft->position);
				nearest = std::min(nearest.value_or(distance), distance);
			}
		}
		osg::Vec3f fireCue(0, 0, 0);
		if (nearest)
		{
			if (sim::Vector3 p = toCamera(aircraft->position + aircraft->orientation * sim::Vector3(*nearest, 0, 0)); p.x > msNearDistance)
			{
				glm::vec2 center = toScreen(p);
				fireCue = osg::Vec3f(center.x, center.y, msFireCueRadius);
			}
		}

		// Red beam to the aircraft from each opponent that has it in its WEZ
		int threatCount = 0;
		for (const Aircraft& opponent : opponents)
		{
			if (threatCount == msMaxThreats || !isInZone(opponent, aircraft->position))
			{
				continue;
			}
			// From the opponent on the screen if it is in view, otherwise from the edge of the view in the direction where it would be
			// seen by turning the camera towards it
			sim::Vector3 p = toCamera(opponent.position);
			glm::vec2 direction(float(p.y), float(-p.z));
			std::optional<glm::vec2> position; // of the opponent on the screen if it is in view
			if (p.x > msNearDistance)
			{
				glm::vec2 projected = toScreen(p);
				direction = projected - target;
				if (std::abs(projected.x) <= aspectRatio && std::abs(projected.y) <= 1.0f)
				{
					position = projected;
				}
			}
			float length = glm::length(direction);
			direction = (length > 1e-6f) ? direction / length : glm::vec2(0.0f, -1.0f);
			glm::vec2 source = position.value_or(target + direction * calcDistanceToEdge(target, direction, aspectRatio));

			// Within msThreatBorderAngle of the direction behind the camera, the beam turns into a red border all around the view
			double angleOffTail = std::acos(math::clamp(-p.x / std::max(glm::length(p), 1e-6), -1.0, 1.0));
			double t = math::clamp(1.0 - angleOffTail / msThreatBorderAngle, 0.0, 1.0);
			float borderWeight = float(t * t * (3.0 - 2.0 * t)); // smoothstep
			mThreatsUniform->setElement(threatCount++, osg::Vec3f(source.x, source.y, borderWeight));
		}

		if (fireCue.z() > 0.0f || threatCount > 0)
		{
			mCamera->setProjectionMatrix(osg::Matrix::ortho2D(-aspectRatio, aspectRatio, -1, 1));
			mAspectRatioUniform->set(aspectRatio);
			mTargetUniform->set(osg::Vec2f(target.x, target.y));
			mFireCueUniform->set(fireCue);
			mThreatCountUniform->set(threatCount);
			mGeode->setNodeMask(~0u);
		}
	}

private:
	//! @returns the distance from the position in the view to the edge of the view in the unit direction
	static float calcDistanceToEdge(const glm::vec2& position, const glm::vec2& direction, float aspectRatio)
	{
		float x = (std::abs(direction.x) > 1e-5f) ? ((direction.x > 0.0f ? aspectRatio : -aspectRatio) - position.x) / direction.x : 1e5f;
		float y = (std::abs(direction.y) > 1e-5f) ? ((direction.y > 0.0f ? 1.0f : -1.0f) - position.y) / direction.y : 1e5f;
		return std::max(std::min(x, y), 0.0f);
	}

	static constexpr int msMaxThreats = 8; //!< beams drawn at most
	static constexpr double msMinSpeed = 1; //!< m/s, below which the WEZ is along the nose instead of the velocity
	static constexpr double msNearDistance = 1; //!< m in front of the camera, closer points are not projected on the screen
	static constexpr double msMinTargetDepth = 10; //!< m in front of the camera, closer aircraft have the beams end at the center
	// Sizes in half heights of the view, like the coordinates of VisHud
	static constexpr float msFireCueRadius = 0.144f;
	static constexpr float msFireCueRimHalfWidth = 0.006f;
	static constexpr float msFireCueRimAlpha = 0.5f;
	static inline const osg::Vec4f msFireCueColor = osg::Vec4f(0.3f, 0.9f, 0.3f, 0.2f); //!< light green as displayed, opacity of the inside
	static constexpr float msThreatSourceHalfWidth = 0.03f; //!< of a beam at its source, where it has half its opacity
	static constexpr double msThreatHalfAngle = 0.2094; //!< rad (12 degrees), half the angle at which a beam widens from its source
	static constexpr float msThreatFadeExponent = 1.5f; //!< of the opacity from the source of a beam to the target, above 1 hazier in between
	static constexpr float msThreatEdgeSharpness = 1.5f; //!< of the sides of the beams, 1 for soft sides like a Gaussian
	//! rad (30 degrees): within this angle of the direction behind the camera, a beam turns gradually into a red border around the view
	static constexpr double msThreatBorderAngle = 0.5236;
	//! Red as displayed, opacity at the source of a beam, and at the edge of the view for the border
	static inline const osg::Vec4f msThreatColor = osg::Vec4f(1.0f, 0.03f, 0.02f, 0.5f);

	osg::ref_ptr<osg::Camera> mParentCamera;
	osg::ref_ptr<osg::Camera> mCamera;
	osg::ref_ptr<osg::Geode> mGeode;
	osg::ref_ptr<osg::Uniform> mAspectRatioUniform;
	osg::ref_ptr<osg::Uniform> mTargetUniform;
	osg::ref_ptr<osg::Uniform> mFireCueUniform;
	osg::ref_ptr<osg::Uniform> mThreatCountUniform;
	osg::ref_ptr<osg::Uniform> mThreatsUniform;
	double mRange = 3500; //!< m
	double mHalfAngle = 10 * math::degToRadD(); //!< rad, like the cannon of HHMARL_3D_NEW
};

//! Short message at the top of the window, e.g. the mode that a key switched. As operation with the priority
//! vis::RenderOperationOrder::Hud, it is not recorded (see VideoRecorder).
class WindowNotice : public vis::RenderOperation
{
public:
	WindowNotice() :
		mHud(new VisHud)
	{
	}

	//! Shows the message for the given time in seconds, replacing the previous one
	void show(const std::string& message, double seconds = 2.0)
	{
		mMessage = message;
		mHideTime = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds));
		mAspectRatio = 0; // drawn again
		if (mHud->getNumParents() == 0)
		{
			addChild(mHud);
		}
	}

	void updatePreRender(const vis::RenderContext& context) override
	{
		if (mHud->getNumParents() == 0)
		{
			return;
		}
		if (std::chrono::steady_clock::now() >= mHideTime)
		{
			removeChild(mHud);
			return;
		}
		float aspectRatio = vis::calcAspectRatio(context);
		if (aspectRatio != mAspectRatio)
		{
			mAspectRatio = aspectRatio;
			mHud->setAspectRatio(aspectRatio);
			mHud->clear();
			mHud->drawText(glm::vec2(0.0f, 0.85f), mMessage, 0.0f, 0.05f, HudDrawer::Alignment::Center);
		}
	}

private:
	osg::ref_ptr<VisHud> mHud;
	std::string mMessage;
	std::chrono::steady_clock::time_point mHideTime;
	float mAspectRatio = 0; //!< of the drawn message, 0 if not drawn
};

} // namespace skybolt
