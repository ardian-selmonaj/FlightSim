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

#include <osg/Geode>
#include <osg/Geometry>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <functional>
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
