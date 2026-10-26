/* Copyright Matthew Reid
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "FlightSimRecording.h"
#include "FlightSimVisuals.h"
#include "PyComponent.h"
#include "PyComponentProperty.h"
#include "PythonBindings.h"

#include <SkyboltCommon/Json/JsonHelpers.h>
#include <SkyboltCommon/Math/Box3.h>
#include <SkyboltEngine/EntityFactory.h>
#include <SkyboltEngine/EngineRoot.h>
#include <SkyboltEngine/EngineRootFactory.h>
#include <SkyboltEngine/EngineSettings.h>
#include <SkyboltEngine/WindowUtil.h>
#include <SkyboltEngine/Components/TemplateNameComponent.h>
#include <SkyboltEngine/Components/VisObjectsComponent.h>
#include <SkyboltEngine/Plugin/PluginHelpers.h>
#include <SkyboltEngine/Scenario/ScenarioMetadataComponent.h>
#include <SkyboltEngine/Scenario/ScenarioSerialization.h>
#include <SkyboltEngine/SimVisBinding/CameraSimVisBinding.h>
#include <SkyboltEngine/SimVisBinding/SimVisSystem.h>
#include <SkyboltSim/Entity.h>
#include <SkyboltSim/World.h>
#include <SkyboltSim/CameraController/CameraController.h>
#include <SkyboltSim/CameraController/CameraControllerSelector.h>
#include <SkyboltSim/CameraController/EntityTargeter.h>
#include <SkyboltSim/Components/AttacherComponent.h>
#include <SkyboltSim/Components/CameraComponent.h>
#include <SkyboltSim/Components/CameraControllerComponent.h>
#include <SkyboltSim/Components/ControlInputsComponent.h>
#include <SkyboltSim/Components/FdmComponent.h>
#include <SkyboltSim/Components/MainRotorComponent.h>
#include <SkyboltSim/Components/NameComponent.h>
#include <SkyboltSim/Spatial/Frustum.h>
#include <SkyboltSim/Spatial/Geocentric.h>
#include <SkyboltSim/Spatial/GreatCircle.h>
#include <SkyboltSim/Spatial/Orientation.h>
#include <SkyboltSim/Spatial/Position.h>
#include <SkyboltSim/System/SimStepper.h>

#include <SkyboltVis/Rect.h>
#include <SkyboltVis/VisRoot.h>
#include <SkyboltVis/Renderable/Planet/Planet.h>
#include <SkyboltVis/Renderable/Water/WaterMaterial.h>
#include <SkyboltVis/RenderOperation/DefaultRenderCameraViewport.h>
#include <SkyboltVis/RenderOperation/RenderOperationSequence.h>
#include <SkyboltVis/Window/CaptureScreenshot.h>
#include <SkyboltVis/Window/OffscreenWindow.h>
#include <SkyboltVis/Window/StandaloneWindow.h>

#include <osg/Image>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/operators.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>
#include <pybind11/stl_bind.h>

// MANUAL INCLUDE
#include <SkyboltSim/System/System.h>
#include <SkyboltVis/Scene.h>

#include <ExamplesCommon/EntityInputSystem.h>
#include <ExamplesCommon/HudSystem.h>
#include <ExamplesCommon/VisRootUtil.h>
#include <ExamplesCommon/WindowUtil.h>
#include <ExamplesCommon/HelpDisplayRenderOperation.h>
#include <ExamplesCommon/HelpDisplayToggleEventListener.h>

#include <SkyboltEngine/CameraInputSystem.h>
#include <SkyboltEngine/EngineCommandLineParser.h>
#include <SkyboltEngine/Input/InputPlatformOsg.h>
#include <SkyboltEngine/Input/InputSystem.h>
#include <SkyboltEngine/UpdateLoop/UpdateLoopUtility.h>
#include <SkyboltEngine/VisHud.h>
#include <SkyboltEngine/Input/LogicalAxis.h>
#include <SkyboltEngine/Diagnostics/StatsDisplaySystem.h>

#include <SkyboltVis/RenderOperation/RenderCameraViewport.h>
#include <SkyboltVis/RenderOperation/RenderOperationOrder.h>
#include <SkyboltVis/RenderOperation/RenderTarget.h>

#include <SkyboltCommon/Exception.h>
#include <SkyboltCommon/MapUtility.h>
#include <SkyboltCommon/Json/ReadJsonFile.h>

#include <SkyboltSim/CameraController/OrbitCameraController.h>
#include <SkyboltSim/Physics/Astronomy.h>
#include <SkyboltVis/Camera.h>

#include <osgDB/ReadFile>
#include <osgDB/WriteFile>
#include <osg/MatrixTransform>
#include <osgUtil/Optimizer>

#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <filesystem>
#include <optional>
#include <set>
////// MANUAL INCLUDE

namespace py = pybind11;

using namespace skybolt;
using namespace skybolt::sim;

namespace skybolt {

EngineRoot* gEngineRoot = nullptr;
EngineRoot* getGlobalEngineRoot() { return gEngineRoot; }

static void setGlobalEngineRoot(EngineRoot* root) { gEngineRoot = root; 
	// FIXME: There's currently an issue where if this plugin is linked statically, calling this addStaticallyRegisteredTypes
	// will re-register previously registered types, creating duplicate types because there is only one `globalHandlers` list across statically-linked objects.
	addStaticallyRegisteredTypes(*root->typeRegistry);
}

static std::unique_ptr<EngineRoot> createEngineRootWithDefaults() {
	return EngineRootFactory::create({});
}

static std::unique_ptr<EngineRoot> createEngineRoot(bool enableVis = true, bool loadPlugins = true)
{
	nlohmann::json settings = readEngineSettings({});

	std::vector<PluginFactory> pluginFactories;
	
	if (loadPlugins)
	{
		std::vector<file::Path> pluginFilepaths = getAllPluginFilepathsInDirectories(EngineRootFactory::getDefaultPluginDirs());
		
		// Filter out SkyboltPython.dll to avoid conflicts when running from Python
		pluginFilepaths.erase(
			std::remove_if(pluginFilepaths.begin(), pluginFilepaths.end(),
				[](const file::Path& path) {
					std::string filename = path.filename().string();
					// remove SkyboltPython Plugin, otherwise there is interference with Python itself!
					return filename.find("SkyboltPython") != std::string::npos;
				}),
			pluginFilepaths.end()
		);
		
		pluginFactories = loadPluginFactories<Plugin, PluginConfig>(pluginFilepaths);
	}

	EngineRootConfig config;
	config.engineSettings = settings;
	config.enableVis = enableVis;
	auto engineRoot = std::make_unique<EngineRoot>(config);
	engineRoot->loadPlugins(pluginFactories);
	return engineRoot;
}

} // namespace skybolt

/////////////////////////////////////////////////////////////////////////////////////

static void createEnvironmentEntities(const EntityFactory& entityFactory, World& world)
{
    world.addEntity(entityFactory.createEntity("Stars"));
    world.addEntity(entityFactory.createEntity("SunBillboard"));
    world.addEntity(entityFactory.createEntity("MoonBillboard"));
    world.addEntity(entityFactory.createEntity("PlanetEarth"));
}

static std::vector<LogicalAxisPtr> createAircraftInputAxesKeyboard(const InputPlatform& inputPlatform)
{
	InputDevicePtr keyboard = inputPlatform.getInputDevicesOfType(InputDeviceTypeKeyboard)[0];
	float rate = 10;

	std::vector<LogicalAxisPtr> logicalAxes;
	logicalAxes.push_back(std::make_shared<KeyAxis>(keyboard, KC_DOWN, KC_UP, rate, rate, -1.0f, 1.0f));
	logicalAxes.push_back(std::make_shared<KeyAxis>(keyboard, KC_LEFT, KC_RIGHT, rate, rate, -1.0f, 1.0f));
	logicalAxes.push_back(std::make_shared<KeyAxis>(keyboard, KC_S, KC_W, 1.0, 0.0, 0.0f, 1.0f, 0.5f));
	logicalAxes.push_back(std::make_shared<KeyAxis>(keyboard, KC_A, KC_D, rate, rate, -1.0f, 1.0f));

	return logicalAxes;
}

//! Calls a handler for every pressed key, also when the keyboard repeats a key held down (repeat is then true)
class KeyPressListener : public EventListener
{
public:
	KeyPressListener(std::function<void(KeyCode, bool repeat)> handler) :
		mHandler(std::move(handler))
	{
	}

	void onEvent(const Event& event) override
	{
		if (const KeyEvent* keyEvent = dynamic_cast<const KeyEvent*>(&event); keyEvent)
		{
			if (keyEvent->type == KeyEvent::Pressed)
			{
				bool repeat = !mHeldKeys.insert(keyEvent->code).second;
				mHandler(keyEvent->code, repeat);
			}
			else if (keyEvent->type == KeyEvent::Released)
			{
				mHeldKeys.erase(keyEvent->code);
			}
		}
	}

private:
	std::function<void(KeyCode, bool repeat)> mHandler;
	std::set<KeyCode> mHeldKeys;
};

static osg::ref_ptr<HelpDisplayRenderOperation> createHelpDisplay(bool manualControl = false)
{
	std::string helpMessage;
	if (manualControl)
	{
		helpMessage =
R"(= Controls =

W: Throttle +
S: Throttle -
A: Rudder left
D: Rudder right
Up: Pitch down
Down: Pitch up
Left: Roll left
Right: Roll right
Space: Fire

F1: Cockpit view
F2: External view
F3: Overview of all aircraft
F4: Free view
Tab: Next aircraft
L: Toggle labels
T: Toggle trails
R: Start/stop recording

H: Toggle help message
Esc: Exit
Mouse: Pan camera
Mouse wheel: Zoom (overview)
)";
	}
	else
	{
		helpMessage =
R"(= Controls =
F1: Cockpit view
F2: External view
F3: Overview of all aircraft
F4: Free view
Tab: Next aircraft
L: Toggle labels
T: Toggle trails
R: Start/stop recording

In Overview (F3):
Mouse, A/D: Rotate around the aircraft
W/S, mouse wheel: Zoom

In Free view (F4):
Shift (hold) + Mouse: Camera tilt modifier
W/S: Move forward/back

H: Toggle help message
Esc: Exit
Mouse: Rotate camera
)";
	}
	osg::ref_ptr<HelpDisplayRenderOperation> hd = new HelpDisplayRenderOperation();
	hd->setMessage(helpMessage);
	return hd;
}

/////////////////////////////////////////////////////////////////////////////////////

bool convertToOSGT(const std::string& inputPath)
{
	// Load the scene
	osg::ref_ptr<osg::Node> node = osgDB::readNodeFile(inputPath);
	if (!node)
	{
		std::cerr << "Failed to read file: " << inputPath << std::endl;
		return false;
	}

	// Build output path with .osgt extension
	std::filesystem::path inPath(inputPath);
	std::filesystem::path outPath = inPath;
	outPath.replace_extension(".osgt");

	// Write as OSGT
	if (!osgDB::writeNodeFile(*node, outPath.string()))
	{
		std::cerr << "Failed to write file: " << outPath << std::endl;
		return false;
	}

	return true;
}

bool scaleAndSaveModel(const std::string& inputPath, float scaleFactor)
{
    // Load the scene
    osg::ref_ptr<osg::Node> node = osgDB::readNodeFile(inputPath);
    if (!node)
    {
        std::cerr << "Failed to read file: " << inputPath << std::endl;
        return false;
    }

    // Wrap the node in a MatrixTransform to apply uniform scale
    osg::ref_ptr<osg::MatrixTransform> scaleTransform = new osg::MatrixTransform();
    scaleTransform->setMatrix(osg::Matrix::scale(scaleFactor, scaleFactor, scaleFactor));
    scaleTransform->addChild(node);

    // Flatten the static transform into the geometry so the scale is baked in,
    // rather than just being a runtime transform node. This makes the output
    // portable and compatible with tools that don't honour MatrixTransform.
    osgUtil::Optimizer optimizer;
    optimizer.optimize(scaleTransform, osgUtil::Optimizer::FLATTEN_STATIC_TRANSFORMS);

    // Build base output path (no extension), with "_scaled" suffix
    std::filesystem::path inPath(inputPath);
    std::filesystem::path basePath = inPath.parent_path() / (inPath.stem().string() + "_scaled");

    // Save as .osgt
    std::filesystem::path osgtPath = basePath;
    osgtPath.replace_extension(".osgt");
    if (!osgDB::writeNodeFile(*scaleTransform, osgtPath.string()))
    {
        std::cerr << "Failed to write OSGT file: " << osgtPath << std::endl;
        return false;
    }
    std::cout << "Saved OSGT: " << osgtPath << std::endl;

    // Save as .ive
    std::filesystem::path ivePath = basePath;
    ivePath.replace_extension(".ive");
    if (!osgDB::writeNodeFile(*scaleTransform, ivePath.string()))
    {
        std::cerr << "Failed to write IVE file: " << ivePath << std::endl;
        return false;
    }
    std::cout << "Saved IVE:  " << ivePath << std::endl;

    return true;
}

static void removeNamespaceQualifier(std::string& str)
{
	size_t p = str.find_last_of(":");
	if (p != std::string::npos)
	{
		str = str.substr(p + 1);
	}
}

static std::vector<ComponentPtr> getComponentsOfTypeName(Entity* entity, const std::string& typeName)
{
	std::vector<ComponentPtr> result;

	std::vector<ComponentPtr> components = entity->getComponents();
	for (const ComponentPtr& component : components)
	{
		std::string name(typeid(*component).name());
		py::detail::clean_type_id(name);
		removeNamespaceQualifier(name);
		if (name == typeName)
		{
			result.push_back(component);
		}
	}
	return result;
};

static ComponentPtr getFirstComponentOfTypeName(Entity* entity, const std::string& typeName)
{
	const auto v = getComponentsOfTypeName(entity, typeName);
	return v.empty() ? nullptr : v.front();
}

static double dotFunc(const Vector3& a, const Vector3& b)
{
	return glm::dot(a, b);
}

static Vector3 crossFunc(const Vector3& a, const Vector3& b)
{
	return glm::cross(a, b);
}

static Vector3 normalizeFunc(const Vector3& v)
{
	return glm::normalize(v);
}

static void setWaveHeight(sim::Entity& entity, double height)
{
	if (vis::Planet* planet = getFirstVisObject<vis::Planet>(entity).get(); planet)
	{
		if (const auto& material = planet->getWaterMaterial(); material)
		{
			material->setWaveHeight(height);
		}
	}
}

static bool attachCameraToWindowWithEngine(sim::Entity& camera, vis::Window& window, EngineRoot& engineRoot)
{
	vis::CameraPtr visCamera = getVisCamera(camera);
	if (visCamera)
	{
		const auto& viewport = createAndAddViewportToWindowWithEngine(window, engineRoot);
		viewport->setCamera(visCamera);
		return true;
	}
	return false;
}

static void advanceSimTime(EngineRoot& engineRoot, double dt)
{
	// FIXME TODO: we should be starting from current sim time, not starting from zero
	SimStepper stepper(engineRoot.systemRegistry);
	stepper.update(dt);
}

static void advanceWallTime(EngineRoot& engineRoot, double dt)
{
	for (const auto& system : *engineRoot.systemRegistry)
	{
		system->advanceWallTime(0, dt); // FIXME TODO: we should be tracking current wall time, not starting from zero
	}
}

static bool render(EngineRoot& engineRoot, vis::VisRoot& visRoot)
{
	advanceSimTime(engineRoot, 0.0); // FIXME: We need to call this to update all the systems prior to rendering
	return visRoot.render();
}

static py::array_t<std::uint8_t> captureScreenshotToImage(vis::VisRoot& visRoot)
{
	osg::ref_ptr<osg::Image> image = vis::captureScreenshot(visRoot);
	if (!image)
	{
		throw std::runtime_error("Could not capture screenshot");
	}

	const int channelCount = image->getPixelSizeInBits() / 8;
	const std::size_t size = image->s() * image->t() * channelCount;
	auto result = py::array_t<std::uint8_t>(size);

	py::buffer_info buf = result.request();
	std::uint8_t* data = static_cast<std::uint8_t*>(buf.ptr);
	std::memcpy(data, image->data(), size);

	result.resize({ image->t(), image->s(), channelCount });
    return result;
}

//! Register a python class as a component. The python class must derive from `skybolt.Component`.
static void registerComponent(EngineRoot& engineRoot, const py::handle& pyClass)
{
	auto mComponentFactoryRegistry = valueOrThrowException(getExpectedRegistry<ComponentFactoryRegistry>(*engineRoot.factoryRegistries));

	auto factory = std::make_shared<ComponentFactoryFunctionAdapter>([pyClass](Entity* entity, const ComponentFactoryContext& context, const nlohmann::json& json) {
        // Instantiate the Python class
        py::object object = pyClass(entity);

		// Create the C++ component to wrap the python object
        return std::make_shared<PyComponent>(object);
	});

	std::string componentClassName = py::str(pyClass.attr("__name__"));
	mComponentFactoryRegistry->insert(std::make_pair(componentClassName, factory));
}

static void loadScenarioFromJsonString(EngineRoot& engineRoot, const std::string& jsonString)
{
	EntityFactoryFn entityFactoryFn = [entityFactory = engineRoot.entityFactory.get()](const std::string& templateName, const std::string& instanceName) {
		return entityFactory->createEntity(templateName, instanceName);
	};

	nlohmann::json j = nlohmann::json::parse(jsonString);
	ifChildExists(j, "scenario", [&] (const nlohmann::json& child) {
		readScenario(*engineRoot.typeRegistry, *engineRoot.scenario, entityFactoryFn, child);
	});
}

static std::string saveScenarioToJsonString(EngineRoot& engineRoot)
{
	nlohmann::json j;
	j["scenario"] = writeScenario(*engineRoot.typeRegistry, *engineRoot.scenario);
	return j.dump(4);
}

static std::optional<LatLonAlt> getPositionLla(const Entity& entity)
{
	std::optional<Vector3> position = getPosition(entity);
	if (!position) { return std::nullopt; }

	return toLatLonAlt(GeocentricPosition(*position)).position;
}

static void setPositionLla(Entity& entity, const LatLonAlt& lla)
{
	Vector3 position = toGeocentric(LatLonAltPosition(lla)).position;
	setPosition(entity, position);
}

static std::optional<Vector3> getOrientationRpy(const Entity& entity)
{
	std::optional<Vector3> position = getPosition(entity);
	if (!position) { return std::nullopt; }

	std::optional<Quaternion> orientation = getOrientation(entity);
	if (!orientation) { return std::nullopt; }

	Quaternion ltpNed = toLtpNed(GeocentricOrientation(*orientation), geocentricToLatLon(*position)).orientation;
	return math::eulerFromQuat(ltpNed);
}

static void setOrientationRpy(Entity& entity, const Vector3& rpy)
{
	std::optional<Vector3> position = getPosition(entity);
	if (!position) { return; }

	Quaternion orientation = toGeocentric(LtpNedOrientation(math::quatFromEuler(rpy)), geocentricToLatLon(*position)).orientation;
	setOrientation(entity, orientation);
}

static std::optional<Vector3> getVelocityNed(const Entity& entity)
{
	std::optional<Vector3> velocity = getVelocity(entity);
	if (!velocity) { return std::nullopt; }

	std::optional<Vector3> position = getPosition(entity);
	if (!position) { return std::nullopt; }

	Matrix3 ltpOrientation = geocentricToLtpOrientation(*position);

	return glm::transpose(ltpOrientation) * *velocity;
}

static void setVelocityNed(Entity& entity, const Vector3& velocity)
{
	std::optional<Vector3> position = getPosition(entity);
	if (!position) { return; }

	Matrix3 ltpOrientation = geocentricToLtpOrientation(*position);

	setVelocity(entity, ltpOrientation * velocity);
}

/////////////////////////////////////////////////////////////////////////////////////

static void setAircraftState(Entity& aircraft, const LatLonAlt& position, const Vector3& rpy, const std::optional<Vector3>& velocityNed)
{
	setPositionLla(aircraft, position);
	setOrientation(aircraft, toGeocentric(LtpNedOrientation(math::quatFromEuler(rpy)), toLatLon(position)).orientation);
	if (velocityNed)
	{
		setVelocityNed(aircraft, *velocityNed);
	}
}

//! Shows aircraft over the earth and simulates them, e.g. for multi-agent air combat from Python.
//! The aircraft are identified by IDs chosen by the caller. Aircraft of entity templates without flight dynamics,
//! e.g. SepecatJaguarBlue, are moved from Python with set_aircraft_state(). Aircraft of templates with a 'jsbSimAircraft'
//! component, e.g. SepecatJaguarJsbBlue, are simulated with JSBSim, whose properties are accessible with get_property() etc.
class FlightSim
{
public:
	FlightSim(int x = 0, int y = 0, int w = 1080, int h = 720,
		const std::map<int, std::string>& entityTypes = {},
		bool manual = false, bool multiwindow = false,
		double dynamicsRate = 75.0, bool headless = false) :
		manualControl(manual),
		multiwindowEnabled(multiwindow),
		headless(headless)
	{
		if (dynamicsRate <= 0)
		{
			throw std::invalid_argument("FlightSim: dynamicsRate must be positive");
		}
		if (headless && (manual || multiwindow))
		{
			throw std::invalid_argument("FlightSim: manual and multiwindow need a window, so they cannot be combined with headless");
		}

		engineRoot = createEngineRoot();

		stepper = std::make_unique<SimStepper>(engineRoot->systemRegistry);
		stepper->setDynamicsStepSize(1.0 / dynamicsRate);

		visRoot = std::make_unique<vis::VisRoot>();
		visRoot->getViewer().setKeyEventSetsDone(osgGA::GUIEventAdapter::KEY_Escape);

		if (!headless)
		{
			window = std::make_shared<vis::StandaloneWindow>(vis::RectI(x, y, w, h));
			visRoot->addWindow(window);
		}

		simCamera = engineRoot->entityFactory->createEntity("Camera");
		engineRoot->scenario->world.addEntity(simCamera);

		cameraControllerComponent = simCamera->getFirstComponentRequired<CameraControllerComponent>();
		auto orbitController = cameraControllerComponent->getControllerOfType<OrbitCameraController>();
		orbitController->setLagTimeConstant(0.3);
		orbitController->setTargetOffset(sim::Vector3(0, 0, -3));
		orbitController->setZoom(0.8);

		cameraControllerComponent->addController("Overview", std::make_shared<OverviewCameraController>(simCamera.get(),
			[this] { return getVisibleAircraftMotion(); },
			[this] { return getCameraTargetMotion(); },
			[this] { return getAspectRatio(); }));

		if (!headless)
		{
			viewport = createAndAddViewportToWindowWithEngine(*window, *engineRoot);
			viewport->setCamera(getVisCamera(*simCamera));
			overlayCamera = viewport->getFinalRenderTarget()->getOsgCamera();

			const osg::ref_ptr<osg::Program>& linesProgram = engineRoot->programs.getRequiredProgram("unlitColored");
			trails = std::make_shared<AircraftTrails>(engineRoot->scene.get(), linesProgram,
				[this] { return getTrailAircraft(); },
				[this] { return getCameraPose(); },
				[this] { return getPixelSizeAtUnitDistance(); });
			tracers = std::make_shared<CannonTracers>(engineRoot->scene.get(), linesProgram,
				[this] { return stepper->getTime(); },
				[this] { return getCameraPose(); },
				[this] { return getPixelSizeAtUnitDistance(); });
			auto simVisSystem = sim::findRequiredSystem<SimVisSystem>(*engineRoot->systemRegistry);
			simVisSystem->addBinding(trails);
			simVisSystem->addBinding(tracers);
		}

		// second Window
		if (multiwindowEnabled)
		{
			simCamera2 = engineRoot->entityFactory->createEntity("Camera");
			engineRoot->scenario->world.addEntity(simCamera2);

			cameraControllerComponent2 = simCamera2->getFirstComponentRequired<CameraControllerComponent>();
			cameraControllerComponent2->selectController("Free");

			// Lugano Coordinates
			sim::LatLonAlt camPos(45.972 * math::degToRadD(), 8.965318 * math::degToRadD(), 3500);
			setPosition(*simCamera2, toGeocentric(sim::LatLonAltPosition(camPos)).position);
			setOrientation(*simCamera2, toGeocentric(sim::LtpNedOrientation(math::quatIdentity()), sim::toLatLon(camPos)).orientation);

			window2 = std::make_shared<vis::StandaloneWindow>(vis::RectI(x + w, y, w, h));
			viewport2 = createAndAddViewportToWindowWithEngine(*window2, *engineRoot);
			viewport2->setCamera(getVisCamera(*simCamera2));
			visRoot->addWindow(window2);
		}

		if (!headless)
		{
			// Input platform and axes
			inputPlatform = std::make_shared<InputPlatformOsg>(window->getView());
			if (manualControl)
			{
				logicalAxes = createAircraftInputAxesKeyboard(*inputPlatform);
				engineRoot->systemRegistry->push_back(std::make_shared<InputSystem>(inputPlatform, logicalAxes));
				cameraInputSystem = std::make_shared<CameraInputSystem>(inputPlatform);
			}
			else
			{
				cameraAxes = createDefaultCameraInputAxes(*inputPlatform);
				engineRoot->systemRegistry->push_back(std::make_shared<InputSystem>(inputPlatform, toValuesVector(cameraAxes)));
				cameraInputSystem = std::make_shared<CameraInputSystem>(inputPlatform, cameraAxes);
			}

			configure(*cameraInputSystem, window->getWidth(), engineRoot->engineSettings);
			connectToCameraExclusivly(*cameraInputSystem, &engineRoot->scenario->world, simCamera->getId());
			engineRoot->systemRegistry->push_back(cameraInputSystem);

			if (manualControl)
			{
				entityInputSystem = std::make_shared<EntityInputSystem>(logicalAxes);
				engineRoot->systemRegistry->push_back(entityInputSystem);

				vis::CameraPtr visCam = getVisCamera(*simCamera);
				hudSystem = std::make_shared<HudSystem>(overlayCamera, [visCam] { return visCam->getFovY(); });
				engineRoot->systemRegistry->push_back(hudSystem);
			}

			keyListener = std::make_shared<KeyPressListener>([this] (KeyCode code, bool repeat) { onKeyPressed(code, repeat); });
			inputPlatform->getEventEmitter()->addEventListener<KeyEvent>(keyListener.get());

			helpDisplay = createHelpDisplay(manualControl);
			window->getRenderOperationSequence().addOperation(helpDisplay, (int)vis::RenderOperationOrder::Hud);

			helpToggleListener = std::make_shared<HelpDisplayToggleEventListener>(helpDisplay);
			inputPlatform->getEventEmitter()->addEventListener<KeyEvent>(helpToggleListener.get());

			// Records the view without the help message, which has the HUD priority
			recorder = std::make_unique<VideoRecorder>(*window);
		}

		select_camera_view("Cockpit");

		createEnvironmentEntities(*engineRoot->entityFactory, engineRoot->scenario->world);

		// Time of day
		engineRoot->scenario->startJulianDate = sim::calcJulianDate(2030, 6, 6, 12);

		// Aircraft of the entityTypes argument, all at the same position over Lugano
		const sim::LatLonAlt position(45.975305 * math::degToRadD(), 8.965318 * math::degToRadD(), 2000);
		for (const auto& [id, type] : entityTypes)
		{
			add_aircraft(id, type, position, sim::Vector3(0, 0, 0), sim::Vector3(100, 0, 0));
		}
		if (manualControl && !aircraftMap.empty())
		{
			set_manual_aircraft(aircraftMap.begin()->first);
		}
	}

	~FlightSim()
	{
		recorder.reset(); // finishes the video file of a recording

		if (trails)
		{
			auto simVisSystem = sim::findRequiredSystem<SimVisSystem>(*engineRoot->systemRegistry);
			simVisSystem->removeBinding(trails);
			simVisSystem->removeBinding(tracers);
		}
		for (const auto& [id, display] : displays)
		{
			if (display.labelHud)
			{
				overlayCamera->removeChild(display.labelHud);
			}
		}
	}

	//! Adds an aircraft of the entity template at the position (latitude and longitude in radians, altitude in meters), with roll, pitch, yaw
	//! in radians relative to north-east-down and the velocity in north-east-down in m/s. An existing aircraft with the same ID is replaced.
	//! The position, orientation and velocity are the initial conditions of a JSBSim aircraft, unless run_ic() is called.
	void add_aircraft(int id, const std::string& templateName, const LatLonAlt& position, const Vector3& rpy, const Vector3& velocityNed)
	{
		sim::EntityPtr aircraft = engineRoot->entityFactory->createEntity(templateName);
		setAircraftState(*aircraft, position, rpy, velocityNed);
		if (auto fdm = aircraft->getFirstComponent<FdmComponent>(); fdm)
		{
			fdm->setTimeStep(stepper->getDynamicsStepSize());
		}

		World& world = engineRoot->scenario->world;
		if (auto it = aircraftMap.find(id); it != aircraftMap.end())
		{
			world.removeEntity(it->second.get());
		}
		world.addEntity(aircraft);
		aircraftMap[id] = aircraft;
		hiddenAircraft.erase(id);
		if (trails)
		{
			trails->clear(id);
		}

		if (id == manualAircraftId)
		{
			bindManualAircraft(aircraft);
		}
		if (!cameraTargetId || id == *cameraTargetId)
		{
			set_camera_target(id);
		}
	}

	void remove_aircraft(int id)
	{
		auto it = aircraftMap.find(id);
		if (it == aircraftMap.end())
		{
			throw std::runtime_error("FlightSim: aircraft with ID " + std::to_string(id) + " not found");
		}
		engineRoot->scenario->world.removeEntity(it->second.get());
		aircraftMap.erase(it);
		hiddenAircraft.erase(id);
		if (trails)
		{
			trails->clear(id);
		}

		if (id == manualAircraftId)
		{
			// Keep manualAircraftId, so that an aircraft added with this ID is flown manually again
			bindManualAircraft(nullptr);
		}
		if (id == cameraTargetId)
		{
			cameraTargetId.reset();
			if (!aircraftMap.empty())
			{
				set_camera_target(aircraftMap.begin()->first);
			}
			updateHud();
		}
	}

	//! Sets the position, orientation and optionally the velocity of an aircraft, see add_aircraft().
	//! A JSBSim aircraft is initialized again at the new state.
	void set_aircraft_state(int id, const LatLonAlt& position, const Vector3& rpy, const std::optional<Vector3>& velocityNed)
	{
		setAircraftState(*getAircraft(id), position, rpy, velocityNed);
	}

	void set_aircraft_visible(int id, bool visible)
	{
		const sim::EntityPtr& aircraft = getAircraft(id);
		if (auto visObjects = aircraft->getFirstComponent<VisObjectsComponent>(); visObjects)
		{
			for (const vis::VisObjectPtr& object : visObjects->getObjects())
			{
				object->setVisible(visible);
			}
		}

		if (visible)
		{
			hiddenAircraft.erase(id);
		}
		else
		{
			hiddenAircraft.insert(id);
			if (id == cameraTargetId)
			{
				cycleCameraTarget();
			}
		}
	}

	//! Aircraft whose dynamics are disabled keep their state, e.g. a JSBSim aircraft is not simulated
	void set_dynamics_enabled(int id, bool enabled)
	{
		getAircraft(id)->setDynamicsEnabled(enabled);
	}

	sim::Entity* get_aircraft(int id)
	{
		auto it = aircraftMap.find(id);
		return (it != aircraftMap.end()) ? it->second.get() : nullptr;
	}

	std::vector<int> get_aircraft_ids() const
	{
		std::vector<int> ids;
		for (const auto& [id, aircraft] : aircraftMap)
		{
			ids.push_back(id);
		}
		return ids;
	}

	std::map<int, sim::EntityPtr> get_all_aircraft() { return aircraftMap; }

	void set_camera_target(int id)
	{
		cameraControllerComponent->setTargetId(getAircraft(id)->getId());
		cameraTargetId = id;
		updateHud();
	}

	//! @param view is one of the camera controllers, e.g. "Cockpit", "Follow" or "Free"
	void select_camera_view(const std::string& view)
	{
		const auto& controllers = cameraControllerComponent->getControllers();
		if (controllers.find(view) == controllers.end())
		{
			std::string names;
			for (const auto& [name, controller] : controllers)
			{
				names += (names.empty() ? "" : ", ") + name;
			}
			throw std::invalid_argument("FlightSim: unknown camera view '" + view + "', available views: " + names);
		}
		cameraControllerComponent->selectController(view);
		cameraView = view;
		updateHud();
	}

	//! Sets the label shown above the aircraft, e.g. its name. An empty label shows none.
	void set_aircraft_label(int id, const std::string& label)
	{
		AircraftDisplay& display = displays[id];
		display.label = label;
		if (!display.labelHud && overlayCamera && !label.empty())
		{
			display.labelHud = new VisHud();
			display.labelHud->setColor(display.color);
			overlayCamera->addChild(display.labelHud);
		}
	}

	//! Sets the color of the aircraft's label and trail
	void set_aircraft_color(int id, const std::array<float, 3>& color)
	{
		AircraftDisplay& display = displays[id];
		display.color = osg::Vec4f(color[0], color[1], color[2], 1.0f);
		if (display.labelHud)
		{
			display.labelHud->setColor(display.color);
		}
	}

	bool get_labels_visible() const { return labelsVisible; }
	void set_labels_visible(bool visible) { labelsVisible = visible; }

	bool get_trails_visible() const { return trails && trails->isVisible(); }
	void set_trails_visible(bool visible)
	{
		if (trails)
		{
			trails->setVisible(visible);
		}
	}

	//! @returns the length in m of the flown path shown by the trails
	double get_trail_length() const { return trails ? trails->getMaxLength() : 0.0; }
	void set_trail_length(double length)
	{
		if (trails)
		{
			trails->setMaxLength(length);
		}
	}

	void clear_trails()
	{
		if (trails)
		{
			trails->clearAll();
		}
	}

	//! The keyboard flies this aircraft, which needs FlightSim(manual=True). The camera is set to the aircraft.
	//! A JSBSim aircraft is controlled by the keyboard unless set_external_control() was called,
	//! otherwise read the controls with get_manual_controls().
	void set_manual_aircraft(int id)
	{
		if (!manualControl)
		{
			throw std::runtime_error("FlightSim: manual control needs FlightSim(manual=True)");
		}
		const sim::EntityPtr& aircraft = getAircraft(id);
		manualAircraftId = id;
		bindManualAircraft(aircraft);
		set_camera_target(id);
	}

	//! @returns the keyboard controls of the manual aircraft as JSBSim flight control commands: aileron, elevator, rudder, throttle
	std::array<double, 4> get_manual_controls() const
	{
		if (!manualAircraftId)
		{
			throw std::runtime_error("FlightSim: there is no manual aircraft, see set_manual_aircraft()");
		}
		auto inputs = getAircraft(*manualAircraftId)->getFirstComponentRequired<ControlInputsComponent>();
		auto stick = inputs->get<glm::vec2>("stick");
		auto pedal = inputs->get<float>("pedal");
		auto throttle = inputs->get<float>("throttle");
		if (!stick || !pedal || !throttle)
		{
			throw std::runtime_error("FlightSim: the manual aircraft has no stick, pedal and throttle control inputs");
		}
		FlightControlCommands commands = toFlightControlCommands(stick->value, pedal->value, throttle->value);
		return { commands.aileron, commands.elevator, commands.rudder, commands.throttle };
	}

	//! @returns the number of presses of the fire key (space) since the last call
	int consume_fire_requests()
	{
		int requests = fireRequests;
		fireRequests = 0;
		return requests;
	}

	//! Shows a cannon shot as a short dark line flying from the aircraft's nose with the given speed relative to the aircraft,
	//! for the given time in seconds
	void fire_tracer(int entityId, double speed = 1000.0, double lifetime = 1.0)
	{
		const sim::EntityPtr& aircraft = getAircraft(entityId);
		std::optional<Vector3> position = getPosition(*aircraft);
		std::optional<Quaternion> orientation = getOrientation(*aircraft);
		if (!position || !orientation)
		{
			throw std::runtime_error("FlightSim: aircraft with ID " + std::to_string(entityId) + " has no position");
		}
		if (tracers)
		{
			const Vector3 forward = *orientation * Vector3(1, 0, 0);
			tracers->add(*position + forward * 10.0, getVelocity(*aircraft).value_or(math::dvec3Zero()) + forward * speed, lifetime);
		}
	}

	void clear_tracers()
	{
		if (tracers)
		{
			tracers->clear();
		}
	}

	//! Advances the simulation by the given number of dynamics steps of 1/dynamicsRate seconds,
	//! or only updates all systems, e.g. the visuals of moved aircraft, if substeps is 0. Does not render.
	void step(int substeps = 1)
	{
		if (substeps < 0)
		{
			throw std::invalid_argument("FlightSim: substeps must not be negative");
		}
		stepper->updateBySubsteps(substeps);
		advanceWallTime(substeps * stepper->getDynamicsStepSize());
	}

	//! Renders a frame, which is also taken for the video while recording (see start_recording())
	//! @returns false if the window was closed or escape was pressed
	bool render()
	{
		updateLabels();
		if (recorder)
		{
			recorder->beforeRender(stepper->getTime());
		}
		bool open = visRoot->render();
		if (recorder)
		{
			recorder->afterRender();
			if (!open)
			{
				recorder->stop(); // finishes the video file
			}
		}
		return open;
	}

	//! Advances the simulation by the given time, in steps of 1/dynamicsRate seconds (left-over time is kept for the next call), and renders a frame
	//! @returns false if the window was closed or escape was pressed
	bool frame(double dtWallClock, bool paused = false)
	{
		stepper->update(paused ? 0.0 : dtWallClock);
		advanceWallTime(dtWallClock);
		return render();
	}

	//! Advances the simulation in real time and renders frames with frame() until the window is closed or escape is pressed
	void run()
	{
		UpdateLoop loop(0.01f);
		loop.exec([this] (float dtWallClock) { return frame(dtWallClock); }, UpdateLoop::neverExit);
	}

	double get_sim_time() const { return stepper->getTime(); }
	double get_dynamics_step_size() const { return stepper->getDynamicsStepSize(); }

	//! Starts recording the view of the window into an MP4 video file, until stop_recording(), the window is closed or the FlightSim
	//! is deleted. Key R starts and stops recording as well. The video shows the view as in the window, with the labels, trails and
	//! the cockpit HUD, but without the help message and the red dot shown while recording. It has recording_fps frames per second
	//! of simulation time, each taken from the simulation step closest to its time, so that it plays at the speed of the simulation.
	//! @param path of the video file, or none for a new file named after the current date and time in recording_directory
	//! @returns the path of the video file
	std::filesystem::path start_recording(const std::optional<std::filesystem::path>& path)
	{
		if (!recorder)
		{
			throw std::runtime_error("FlightSim: recording needs a window, so it cannot be used with headless");
		}
		std::filesystem::path file = path ? *path : VideoRecorder::createFilePath(recordingDirectory);
		recorder->start(file, recordingFps, stepper->getTime(), stepper->getDynamicsStepSize());
		return file;
	}

	//! Stops recording and finishes the video file
	void stop_recording()
	{
		if (recorder)
		{
			recorder->stop();
		}
	}

	bool is_recording() const { return recorder && recorder->isRecording(); }

	//! @returns true while recording if render() takes a frame of the video at the current simulation time. Callers that skip
	//! rendering to keep up with real time should render these frames, otherwise the video repeats the next rendered frame.
	bool is_recording_frame_due() const { return recorder && recorder->isFrameDue(stepper->getTime()); }

	//! Folder of the videos recorded with key R or start_recording() without a path. A relative folder is relative to the current directory.
	const std::filesystem::path& get_recording_directory() const { return recordingDirectory; }
	void set_recording_directory(const std::filesystem::path& directory) { recordingDirectory = directory; }

	//! Frames per second of simulation time of the following recordings
	double get_recording_fps() const { return recordingFps; }
	void set_recording_fps(double fps)
	{
		if (!(fps > 0))
		{
			throw std::invalid_argument("FlightSim: recording_fps must be positive");
		}
		recordingFps = fps;
	}

	// JSBSim properties of aircraft with flight dynamics

	double get_property(int id, const std::string& name) const
	{
		return getFdm(id).getProperty(name);
	}

	std::vector<double> get_properties(int id, const py::sequence& names) const
	{
		const FdmComponent& fdm = getFdm(id);
		std::vector<double> values;
		values.reserve(names.size());
		for (const py::handle& name : names)
		{
			values.push_back(fdm.getProperty(name.cast<std::string>()));
		}
		return values;
	}

	void set_property(int id, const std::string& name, double value)
	{
		getFdm(id).setProperty(name, value);
	}

	std::string query_property_catalog(int id, const std::string& check) const
	{
		return getFdm(id).queryPropertyCatalog(check);
	}

	bool run_ic(int id)
	{
		return getFdm(id).runInitialConditions();
	}

	int get_num_engines(int id) const
	{
		return getFdm(id).getEngineCount();
	}

	void init_engine_running(int id, int engineIndex)
	{
		getFdm(id).initEngineRunning(engineIndex);
	}

	bool get_steady_state(int id)
	{
		return getFdm(id).runEnginesToSteadyState();
	}

	void set_external_control(int id, bool enabled)
	{
		getFdm(id).setExternalControl(enabled);
	}

	// Useful accessors
	EngineRoot* get_engine_root() { return engineRoot.get(); }
	sim::World* get_world() { return &engineRoot->scenario->world; }
	vis::VisRoot* get_vis_root() { return visRoot.get(); }
	vis::Window* get_window() { return window.get(); }
	sim::Entity* get_camera() { return simCamera.get(); }

private:
	const sim::EntityPtr& getAircraft(int id) const
	{
		auto it = aircraftMap.find(id);
		if (it == aircraftMap.end())
		{
			throw std::runtime_error("FlightSim: aircraft with ID " + std::to_string(id) + " not found");
		}
		return it->second;
	}

	FdmComponent& getFdm(int id) const
	{
		auto fdm = getAircraft(id)->getFirstComponent<FdmComponent>();
		if (!fdm)
		{
			throw std::runtime_error("FlightSim: aircraft with ID " + std::to_string(id) + " has no flight dynamics model. "
				"Use an entity template with a 'jsbSimAircraft' component, e.g. SepecatJaguarJsbBlue.");
		}
		return *fdm;
	}

	void bindManualAircraft(const sim::EntityPtr& aircraft)
	{
		if (aircraft)
		{
			// The keyboard sets these control inputs (see EntityInputSystem), which templates without flight dynamics do not have
			auto inputs = aircraft->getFirstComponentRequired<ControlInputsComponent>();
			inputs->createOrGet<glm::vec2>("stick", glm::vec2(0.0f), posNegUnitRange<glm::vec2>());
			inputs->createOrGet<float>("throttle", 0.0f);
			inputs->createOrGet<float>("pedal", 0.0f, posNegUnitRange<float>());
		}
		entityInputSystem->setEntity(aircraft);
		hudSystem->setEntity(aircraft.get());
	}

	void updateHud()
	{
		if (hudSystem)
		{
			hudSystem->setPitchLadderVisible(cameraView == "Cockpit");
			hudSystem->setEnabled(manualAircraftId && cameraTargetId == manualAircraftId);
		}
	}

	void onKeyPressed(KeyCode code, bool repeat)
	{
		switch (code)
		{
			case KC_F1:
				select_camera_view("Cockpit");
				break;
			case KC_F2:
				select_camera_view("Follow");
				break;
			case KC_F3:
				select_camera_view("Overview");
				break;
			case KC_F4:
				select_camera_view("Free");
				break;
			case KC_TAB:
				cycleCameraTarget();
				break;
			case KC_L:
				labelsVisible = !labelsVisible;
				break;
			case KC_T:
				set_trails_visible(!get_trails_visible());
				break;
			case KC_R:
				if (!repeat) // holding the key down does not start and stop recording repeatedly
				{
					toggleRecording();
				}
				break;
			case KC_SPACE:
				if (manualControl)
				{
					++fireRequests;
				}
				break;
			default:
				break;
		}
	}

	//! Starts or stops recording for key R. Errors are reported instead of thrown, because keys are handled while rendering.
	void toggleRecording()
	{
		try
		{
			if (is_recording())
			{
				stop_recording();
			}
			else
			{
				start_recording(std::nullopt);
			}
		}
		catch (const std::exception& e)
		{
			std::cerr << e.what() << std::endl;
		}
	}

	//! Sets the camera to the next visible aircraft
	void cycleCameraTarget()
	{
		std::vector<int> ids;
		for (const auto& [id, aircraft] : aircraftMap)
		{
			if (hiddenAircraft.find(id) == hiddenAircraft.end())
			{
				ids.push_back(id);
			}
		}
		if (ids.empty())
		{
			return;
		}

		auto next = cameraTargetId ? std::upper_bound(ids.begin(), ids.end(), *cameraTargetId) : ids.begin();
		set_camera_target(next != ids.end() ? *next : ids.front());
	}

	void advanceWallTime(double dt)
	{
		wallTime += dt;
		for (const auto& system : *engineRoot->systemRegistry)
		{
			system->advanceWallTime(wallTime, dt);
		}
	}

	bool isVisible(int id) const
	{
		return hiddenAircraft.find(id) == hiddenAircraft.end();
	}

	//! @returns the aircraft shown by the overview camera
	std::vector<OverviewCameraController::Aircraft> getVisibleAircraftMotion() const
	{
		std::vector<OverviewCameraController::Aircraft> result;
		for (const auto& [id, aircraft] : aircraftMap)
		{
			if (std::optional<Vector3> position = getPosition(*aircraft); position && isVisible(id))
			{
				result.push_back({*position, getVelocity(*aircraft).value_or(math::dvec3Zero())});
			}
		}
		return result;
	}

	//! @returns the aircraft in the foreground of the overview camera
	std::optional<OverviewCameraController::Aircraft> getCameraTargetMotion() const
	{
		if (cameraTargetId && isVisible(*cameraTargetId))
		{
			if (auto it = aircraftMap.find(*cameraTargetId); it != aircraftMap.end())
			{
				if (std::optional<Vector3> position = getPosition(*it->second); position)
				{
					return OverviewCameraController::Aircraft{*position, getVelocity(*it->second).value_or(math::dvec3Zero())};
				}
			}
		}
		return std::nullopt;
	}

	double getAspectRatio() const
	{
		return window ? double(window->getWidth()) / double(std::max(1, window->getHeight())) : 1.0;
	}

	//! @returns the visible aircraft, whose trails are extended
	std::vector<AircraftTrails::Aircraft> getTrailAircraft() const
	{
		std::vector<AircraftTrails::Aircraft> result;
		for (const auto& [id, aircraft] : aircraftMap)
		{
			if (std::optional<Vector3> position = getPosition(*aircraft); position && isVisible(id))
			{
				auto display = displays.find(id);
				result.push_back({id, *position, display != displays.end() ? display->second.color : osg::Vec4f(1, 1, 1, 1)});
			}
		}
		return result;
	}

	CameraPose getCameraPose() const
	{
		return {getPosition(*simCamera).value_or(math::dvec3Zero()),
			getOrientation(*simCamera).value_or(math::dquatIdentity()) * Vector3(1, 0, 0)};
	}

	//! @returns size in m of a pixel at a distance of 1 m from the camera
	double getPixelSizeAtUnitDistance() const
	{
		double fovY = simCamera->getFirstComponentRequired<CameraComponent>()->getState().fovY;
		return 2.0 * std::tan(fovY * 0.5) / double(std::max(1, window ? window->getHeight() : 1));
	}

	//! Draws the labels of the visible aircraft above them
	void updateLabels()
	{
		if (!overlayCamera)
		{
			return;
		}

		const osg::Viewport* viewport = overlayCamera->getViewport();
		double aspectRatio = (viewport && viewport->height() > 0) ? viewport->width() / viewport->height() : getAspectRatio();

		Frustum frustum;
		frustum.origin = getPosition(*simCamera).value_or(math::dvec3Zero());
		frustum.orientation = getOrientation(*simCamera).value_or(math::dquatIdentity());
		frustum.fieldOfViewVertical = simCamera->getFirstComponentRequired<CameraComponent>()->getState().fovY;
		frustum.fieldOfViewHorizontal = 2.0 * std::atan(std::tan(frustum.fieldOfViewVertical * 0.5) * aspectRatio);

		for (const auto& [id, display] : displays)
		{
			if (!display.labelHud)
			{
				continue;
			}
			display.labelHud->clear();
			display.labelHud->setAspectRatio(float(aspectRatio));

			auto it = aircraftMap.find(id);
			if (!labelsVisible || display.label.empty() || it == aircraftMap.end() || !isVisible(id))
			{
				continue;
			}
			if (std::optional<Vector3> position = getPosition(*it->second); position)
			{
				Vector3 screen = transformToScreenSpace(frustum, *position);
				if (screen.z > 1.0) // in front of the camera
				{
					// Above the aircraft on the screen, by at least its projected size, assuming 5 m radius
					double aircraftRadius = 5.0 / (screen.z * std::tan(frustum.fieldOfViewVertical * 0.5));
					display.labelHud->drawText(glm::vec2(screen.x * aspectRatio, screen.y + std::max(0.035, aircraftRadius + 0.02)), display.label, 0, 0.035f, HudDrawer::Alignment::Center);
				}
			}
		}
	}

private:
	bool manualControl;
	bool multiwindowEnabled;
	bool headless;

	std::unique_ptr<EngineRoot> engineRoot;
	std::unique_ptr<SimStepper> stepper;
	double wallTime = 0.0;

	std::unique_ptr<vis::VisRoot> visRoot;
	std::shared_ptr<vis::Window> window;
	osg::ref_ptr<vis::RenderCameraViewport> viewport;

	std::map<int, sim::EntityPtr> aircraftMap;
	std::set<int> hiddenAircraft;
	sim::EntityPtr simCamera;
	CameraControllerComponentPtr cameraControllerComponent;
	std::string cameraView;
	std::optional<int> cameraTargetId;
	std::optional<int> manualAircraftId;
	int fireRequests = 0;

	std::shared_ptr<InputPlatformOsg> inputPlatform;
	CameraInputAxes cameraAxes;
	std::vector<LogicalAxisPtr> logicalAxes;
	std::shared_ptr<CameraInputSystem> cameraInputSystem;

	osg::ref_ptr<HelpDisplayRenderOperation> helpDisplay;
	std::shared_ptr<HelpDisplayToggleEventListener> helpToggleListener;
	std::shared_ptr<KeyPressListener> keyListener;

	osg::ref_ptr<osg::Camera> overlayCamera;
	std::shared_ptr<EntityInputSystem> entityInputSystem;
	std::shared_ptr<HudSystem> hudSystem;

	struct AircraftDisplay
	{
		std::string label;
		osg::Vec4f color = osg::Vec4f(1, 1, 1, 1);
		osg::ref_ptr<VisHud> labelHud;
	};
	std::map<int, AircraftDisplay> displays; //!< label and color by aircraft ID
	bool labelsVisible = true;
	std::shared_ptr<AircraftTrails> trails; //!< null without a window
	std::shared_ptr<CannonTracers> tracers; //!< null without a window

	std::unique_ptr<VideoRecorder> recorder; //!< null without a window
	std::filesystem::path recordingDirectory = "recordings";
	double recordingFps = 30.0;

	sim::EntityPtr simCamera2;
	CameraControllerComponentPtr cameraControllerComponent2;

	std::shared_ptr<vis::Window> window2;
	osg::ref_ptr<vis::RenderCameraViewport> viewport2;
};


PYBIND11_MAKE_OPAQUE(std::vector<std::string>);

PYBIND11_MODULE(skybolt, m) {
	py::bind_vector<std::vector<std::string>>(m, "VectorString");

	py::class_<Vector3>(m, "Vector3", "A 3D vector of [x, y, z] components")
		.def(py::init())
		.def(py::init<double, double, double>())
		.def_readwrite("x", &Vector3::x)
		.def_readwrite("y", &Vector3::y)
		.def_readwrite("z", &Vector3::z)
		.def(py::self + py::self)
		.def(py::self - py::self)
		.def(py::self * py::self)
		.def(py::self / py::self)
		.def(py::self += py::self)
		.def(py::self -= py::self)
		.def(py::self *= double())
		.def(py::self /= double())
		.def(double() * py::self)
		.def(py::self * double())
		.def(py::self / double());

	py::class_<Quaternion>(m, "Quaternion", "Represents an orientation as a quarternion of [x, y, z, w] components")
		.def(py::init())
		.def(py::init<double, double, double, double>())
		.def_readwrite("x", &Quaternion::x)
		.def_readwrite("y", &Quaternion::y)
		.def_readwrite("z", &Quaternion::z)
		.def_readwrite("w", &Quaternion::w)
		.def(py::self * Vector3());

	py::class_<LatLon>(m, "LatLon", "Represents a 2D location on a planet location as a latitude and longitude in radians")
		.def(py::init())
		.def(py::init<double, double>())
		.def_readwrite("lat", &LatLon::lat)
		.def_readwrite("lon", &LatLon::lon);

	py::class_<LatLonAlt>(m, "LatLonAlt", "Represents a 3D location on a planet location as a latitude and longitude in radians, and altitude in meters")
		.def(py::init())
		.def(py::init<double, double, double>())
		.def_readwrite("lat", &LatLonAlt::lat)
		.def_readwrite("lon", &LatLonAlt::lon)
		.def_readwrite("alt", &LatLonAlt::alt);

	py::class_<Frustum>(m, "Frustum")
		.def(py::init())
		.def_readwrite("origin", &Frustum::origin)
		.def_readwrite("orientation", &Frustum::orientation)
		.def_readwrite("fieldOfViewHorizontal", &Frustum::fieldOfViewHorizontal)
		.def_readwrite("fieldOfViewVertical", &Frustum::fieldOfViewVertical);

	py::class_<Box3d>(m, "Box3d")
		.def(py::init())
		.def(py::init<const Vector3&, const Vector3&>())
		.def("size", &Box3d::size)
		.def("center", &Box3d::center)
		.def("merge", &Box3d::merge)
		.def_readwrite("minimum", &Box3d::minimum)
		.def_readwrite("maximum", &Box3d::maximum);
		
	py::class_<CameraState>(m, "CameraState")
		.def(py::init())
		.def_readwrite("nearClipDistance", &CameraState::nearClipDistance)
		.def_readwrite("farClipDistance", &CameraState::farClipDistance)
		.def_readwrite("fovY", &CameraState::fovY);

	py::class_<vis::RectI>(m, "RectI", "A rectangle defined by [x, y, width, height]")
		.def(py::init<int, int, int, int>());

	py::class_<Position, std::shared_ptr<Position>>(m, "Position");

	py::class_<GeocentricPosition, std::shared_ptr<GeocentricPosition>, Position>(m, "GeocentricPosition")
		.def(py::init<Vector3>())
		.def_readwrite("position", &GeocentricPosition::position);

	py::class_<LatLonAltPosition, std::shared_ptr<LatLonAltPosition>, Position>(m, "LatLonAltPosition")
		.def(py::init<LatLonAlt>())
		.def_readwrite("position", &LatLonAltPosition::position);

	py::class_<Orientation, std::shared_ptr<Orientation>>(m, "Orientation");

	py::class_<GeocentricOrientation, std::shared_ptr<GeocentricOrientation>, Orientation>(m, "GeocentricOrientation")
		.def(py::init<Quaternion>())
		.def_readwrite("orientation", &GeocentricOrientation::orientation);

	py::class_<LtpNedOrientation, std::shared_ptr<LtpNedOrientation>, Orientation>(m, "LtpNedOrientation")
		.def(py::init<Quaternion>())
		.def_readwrite("orientation", &LtpNedOrientation::orientation);

	py::class_<EntityId, std::shared_ptr<EntityId>>(m, "EntityId")
		.def_readwrite("applicationId", &EntityId::applicationId)
		.def_readwrite("entityId", &EntityId::entityId);

	py::class_<Component, std::shared_ptr<Component>>(m, "Component", "Base class for components which can be attached to an `Entity`")
		.def(py::init<>())
		.def("setSimTime", &Component::setSimTime);

	py::class_<MainRotorComponent, std::shared_ptr<MainRotorComponent>, Component>(m, "MainRotorComponent")
		.def("getPitchAngle", &MainRotorComponent::getPitchAngle)
		.def("getRotationAngle", &MainRotorComponent::getRotationAngle)
		.def("getTppOrientationRelBody", &MainRotorComponent::getTppOrientationRelBody)
		.def("setNormalizedRpm", &MainRotorComponent::setNormalizedRpm);

	py::class_<ScenarioMetadataComponent, std::shared_ptr<ScenarioMetadataComponent>, Component>(m, "ScenarioMetadataComponent")
		.def_readwrite("serializable", &ScenarioMetadataComponent::serializable)
		.def_property("deletable", // TODO: deprecate deletable and expose setting the ScenarioMetadataComponent::LifetimePolicy enum value directly
			[] (const ScenarioMetadataComponent& c) { return c.lifetimePolicy == ScenarioMetadataComponent::LifetimePolicy::User; }, // user is allowed to delete
			[] (ScenarioMetadataComponent& c, bool value) { return c.lifetimePolicy = value ? ScenarioMetadataComponent::LifetimePolicy::User : ScenarioMetadataComponent::LifetimePolicy::Procedural; })
		.def_readwrite("directory", &ScenarioMetadataComponent::directory);

	py::class_<TemplateNameComponent, std::shared_ptr<TemplateNameComponent>, Component>(m, "TemplateNameComponent", "A component storing the name of the template which an `Entity` instantiates")
		.def_readonly("name", &TemplateNameComponent::name);

	py::class_<CameraComponent, std::shared_ptr<CameraComponent>, Component>(m, "CameraComponent")
		.def_property("state",
			[](const CameraComponent& c) -> const CameraState& { return c.getState(); },
			[](CameraComponent& c, const CameraState& state) { c.getState() = state; },
			py::return_value_policy::reference_internal);

	py::class_<CameraControllerSelector, std::shared_ptr<CameraControllerSelector>>(m, "CameraControllerSelector")
		.def("selectController", &CameraControllerSelector::selectController)
		.def("getSelectedControllerName", &CameraControllerSelector::getSelectedControllerName)
		.def("setTargetId", &CameraControllerSelector::setTargetId);

	py::class_<CameraControllerComponent, std::shared_ptr<CameraControllerComponent>, Component, CameraControllerSelector>(m, "CameraControllerComponent");

	py::class_<AttacherComponent, std::shared_ptr<AttacherComponent>>(m, "AttacherComponent")
		.def_readwrite("enabled", &AttacherComponent::enabled)
		.def_readwrite("parentEntityId", &AttacherComponent::parentEntityId)
		.def_readwrite("parentEntityAttachmentPoint", &AttacherComponent::parentEntityAttachmentPoint)
		.def_readwrite("ownEntityAttachmentPoint", &AttacherComponent::ownEntityAttachmentPoint)
		.def_readwrite("positionOffset", &AttacherComponent::positionOffset)
		.def_readwrite("orientationOffset", &AttacherComponent::orientationOffset);

	py::class_<EntityTargeter>(m, "EntityTargeter", "Interface for a class which references a target `Entity` by `EntityId`")
		.def("getTargetId", &EntityTargeter::getTargetId)
		.def("setTargetId", &EntityTargeter::setTargetId)
		.def("getTargetName", &EntityTargeter::getTargetName)
		.def("setTargetName", &EntityTargeter::setTargetName);

	py::class_<Entity, std::shared_ptr<Entity>>(m, "Entity")
		.def("getId", &Entity::getId)
		.def("getName", [](Entity* entity) { return getName(*entity); })
		.def("getPosition", [](Entity* entity) {
			return *getPosition(*entity);
		})
		.def("getPositionLla", [](Entity* entity) {
			return *getPositionLla(*entity);
		})
		.def("setPosition", [](Entity* entity, const Vector3& position) {
			setPosition(*entity, position);
		})
		.def("setPositionLla", [](Entity* entity, const LatLonAlt& position) {
			setPositionLla(*entity, position);
		})
		.def("getOrientation", [](Entity* entity) {
			return *getOrientation(*entity);
		})
		.def("getOrientationRpy", [](Entity* entity) {
			return *getOrientationRpy(*entity);
		})
		.def("setOrientation", [](Entity* entity, const Quaternion& orientation) {
			setOrientation(*entity, orientation);
		})
		.def("setOrientationRpy", [](Entity* entity, const Vector3& orientation) {
			setOrientationRpy(*entity, orientation);
		})
		.def("getVelocity", [](Entity* entity) {
			return *getVelocity(*entity);
		})
		.def("getVelocityNed", [](Entity* entity) {
			return *getVelocityNed(*entity);
		})
		.def("setVelocity", [](Entity* entity, const Vector3& velocity) {
			setVelocity(*entity, velocity);
		})
		.def("setVelocityNed", [](Entity* entity, const Vector3& velocity) {
			setVelocityNed(*entity, velocity);
		})
		.def("getComponents", &Entity::getComponents)
		.def("getComponentsOfType", &getComponentsOfTypeName)
		.def("getFirstComponentOfType", &getFirstComponentOfTypeName)
		.def("addComponent", &Entity::addComponent)
		.def_property("dynamicsEnabled", &Entity::isDynamicsEnabled, &Entity::setDynamicsEnabled);

	py::class_<World>(m, "World")
		.def("getEntities", &World::getEntities, py::return_value_policy::reference)
		.def("addEntity", &World::addEntity)
		.def("removeEntity", &World::removeEntity)
		.def("removeAllEntities", &World::removeAllEntities)
		.def("findEntityByName", &World::findObjectByName);

	py::class_<EntityFactory>(m, "EntityFactory", "Class responsible for creating `Entity` instances based on a template name")
		.def("createEntity", &EntityFactory::createEntity, py::return_value_policy::reference,
			py::arg("templateName"), py::arg("name") = "", py::arg("position") = math::dvec3Zero(), py::arg("orientation") = math::dquatIdentity(), py::arg("id") = sim::nullEntityId());

	py::class_<Scenario>(m, "Scenario")
		.def_readwrite("startJulianDate", &Scenario::startJulianDate)
		.def_property("time", [](Scenario* scenario) { return scenario->timeSource.getTime(); }, [](Scenario* scenario, double time) { return scenario->timeSource.setTime(time); })
		.def_property_readonly("currentJulianDate", [](Scenario* scenario) {return getCurrentJulianDate(*scenario); });

	py::class_<PyComponentProperty, PyComponentPropertyPtr>(m, "Property")
		.def_property_readonly("name", &PyComponentProperty::getName)
		.def_property_readonly("readOnly", &PyComponentProperty::isReadOnly)
		.def_property("value", [] (PyComponentProperty& p) { return p.getValue(); }, [] (PyComponentProperty& p, const py::handle& value) {p.setValue(value);});

	py::class_<EngineRoot>(m, "EngineRoot")
		.def_property_readonly("world", [](const EngineRoot& r) {return &r.scenario->world; }, py::return_value_policy::reference_internal)
		.def_property_readonly("entityFactory", [](const EngineRoot& r) {return r.entityFactory.get(); }, py::return_value_policy::reference_internal)
		.def_property_readonly("scenario", [](const EngineRoot& r) {return r.scenario.get(); }, py::return_value_policy::reference_internal)
		.def("locateFile", [](const EngineRoot& r, const std::string& filename) { return value(r.fileLocator(filename)).value_or("").string(); })
		.def("loadScenarioFromJson", loadScenarioFromJsonString)
		.def("saveScenarioToJson", saveScenarioToJsonString);

	py::class_<vis::Window, std::shared_ptr<vis::Window>>(m, "Window");

	py::class_<vis::StandaloneWindow, std::shared_ptr<vis::StandaloneWindow>, vis::Window>(m, "StandaloneWindow")
		.def(py::init<vis::RectI>());

	py::class_<vis::OffscreenWindow, std::shared_ptr<vis::OffscreenWindow>, vis::Window>(m, "OffscreenWindow")
		.def(py::init<int, int>()); // width, height

	py::enum_<vis::LoadTimingPolicy>(m, "LoadTimingPolicy", "Enum specifying policy for timing of loading data into the renderer")
    .value("LoadAcrossMultipleFrames", vis::LoadTimingPolicy::LoadAcrossMultipleFrames)
    .value("LoadBeforeRender", vis::LoadTimingPolicy::LoadBeforeRender)
    .export_values();

	py::class_<vis::VisRoot>(m, "VisRoot")
		.def(py::init())
		.def("addWindow", &vis::VisRoot::addWindow)
		.def("removeWindow", &vis::VisRoot::removeWindow)
		.def("setLoadTimingPolicy", &vis::VisRoot::setLoadTimingPolicy);

	m.def("getGlobalEngineRoot", &getGlobalEngineRoot, "Get global EngineRoot", py::return_value_policy::reference);
	m.def("setGlobalEngineRoot", &setGlobalEngineRoot, "Set global EngineRoot");
	m.def("createEngineRootWithDefaults", &createEngineRootWithDefaults, "Create an EngineRoot with default values"); //@deprecated
	m.def("createEngineRoot", &createEngineRoot, py::arg("enableVis"), py::arg("loadPlugins"), "Create an EngineRoot");
	m.def("attachCameraToWindowWithEngine", &attachCameraToWindowWithEngine);
	m.def("registerComponent", &registerComponent);
	m.def("advanceSimTime", &advanceSimTime);
	m.def("advanceWallTime", &advanceWallTime);
	m.def("render", &render, py::arg("engineRoot"), py::arg("window"));
	m.def("toGeocentricPosition", [](const PositionPtr& position) { return std::make_shared<GeocentricPosition>(toGeocentric(*position)); });
	m.def("toGeocentricOrientation", [](const OrientationPtr& orientation, const LatLon& latLon) { return std::make_shared<GeocentricOrientation>(toGeocentric(*orientation, latLon)); });
	m.def("toLatLonAlt", [](const PositionPtr& position) { return std::make_shared<LatLonAltPosition>(toLatLonAlt(*position)); });
	m.def("toLatLon", &toLatLon);
	m.def("toLatLonAlt", py::overload_cast<const LatLon&, double>(&toLatLonAlt));
	m.def("dot", &dotFunc);
	m.def("cross", &crossFunc);
	m.def("normalize", &normalizeFunc);
	m.def("quaternionFromEuler", py::overload_cast<const Vector3&>(&math::quatFromEuler));
	m.def("captureScreenshot", [](vis::VisRoot& visRoot) { return captureScreenshotToImage(visRoot); });
	m.def("captureScreenshot", [](vis::VisRoot& visRoot, const std::string& filename) { return vis::captureScreenshot(visRoot, filename); });
	m.def("moveDistanceAndBearing", &moveDistanceAndBearing);
	m.def("transformToScreenSpace", &transformToScreenSpace);
	m.def("setWaveHeight", &setWaveHeight);
	m.def("calcSmallestAngleFromTo", &math::calcSmallestAngleFromTo<double>);
	m.def("getProperties", &getComponentProperties);
	m.def("getProperty", &getComponentProperty);
	m.def("convertToOSGT", &convertToOSGT, py::arg("inpath"));
	m.def("scaleAndSaveModel", &scaleAndSaveModel, py::arg("inpath"), py::arg("scale"));

	//! Version of the FlightSim Python API, raised when it changes, so that Python code can check that the build is up to date
	m.attr("FLIGHTSIM_API_VERSION") = 4;

	py::class_<FlightSim>(m, "FlightSim", "Shows aircraft over the earth and simulates them, see add_aircraft()")
	.def(py::init<int,int,int,int,const std::map<int, std::string>&,bool,bool,double,bool>(),
		py::arg("x") = 0,
		py::arg("y") = 0,
		py::arg("width") = 1080,
		py::arg("height") = 720,
		py::arg("entityTypes") = std::map<int, std::string>(),
		py::arg("manual") = false,
		py::arg("multiwindow") = false,
		py::arg("dynamicsRate") = 75.0,
		py::arg("headless") = false)
	.def("add_aircraft", &FlightSim::add_aircraft,
		py::arg("id"), py::arg("template"), py::arg("position"), py::arg("rpy") = Vector3(0, 0, 0), py::arg("velocityNed") = Vector3(0, 0, 0))
	.def("remove_aircraft", &FlightSim::remove_aircraft, py::arg("id"))
	.def("remove_entity", &FlightSim::remove_aircraft, py::arg("entityId"))
	.def("set_aircraft_state", &FlightSim::set_aircraft_state,
		py::arg("id"), py::arg("position"), py::arg("rpy"), py::arg("velocityNed") = py::none())
	.def("set_entity_attitude", [](FlightSim& flightSim, int entityId, const LatLonAlt& position, const Vector3& rpy) {
			flightSim.set_aircraft_state(entityId, position, rpy, std::nullopt);
		}, py::arg("entityId"), py::arg("position"), py::arg("rpy"))
	.def("set_aircraft_visible", &FlightSim::set_aircraft_visible, py::arg("id"), py::arg("visible"))
	.def("set_dynamics_enabled", &FlightSim::set_dynamics_enabled, py::arg("id"), py::arg("enabled"))
	.def("get_aircraft", &FlightSim::get_aircraft, py::arg("id"), py::return_value_policy::reference)
	.def("get_aircraft_ids", &FlightSim::get_aircraft_ids)
	.def_property_readonly("all_aircraft", &FlightSim::get_all_aircraft)
	.def("set_camera_target", &FlightSim::set_camera_target, py::arg("id"))
	.def("select_camera_view", &FlightSim::select_camera_view, py::arg("view"))
	.def("set_aircraft_label", &FlightSim::set_aircraft_label, py::arg("id"), py::arg("label"))
	.def("set_aircraft_color", &FlightSim::set_aircraft_color, py::arg("id"), py::arg("color"))
	.def_property("labels_visible", &FlightSim::get_labels_visible, &FlightSim::set_labels_visible)
	.def_property("trails_visible", &FlightSim::get_trails_visible, &FlightSim::set_trails_visible)
	.def_property("trail_length", &FlightSim::get_trail_length, &FlightSim::set_trail_length)
	.def("clear_trails", &FlightSim::clear_trails)
	.def("set_manual_aircraft", &FlightSim::set_manual_aircraft, py::arg("id"))
	.def("get_manual_controls", &FlightSim::get_manual_controls)
	.def("consume_fire_requests", &FlightSim::consume_fire_requests)
	.def("fire_tracer", &FlightSim::fire_tracer, py::arg("entityId"), py::arg("speed") = 1000.0, py::arg("lifetime") = 1.0)
	.def("fire_rocket", &FlightSim::fire_tracer, py::arg("entityId"), py::arg("speed") = 1000.0, py::arg("lifetime") = 1.0)
	.def("clear_tracers", &FlightSim::clear_tracers)
	.def("step", &FlightSim::step, py::arg("substeps") = 1)
	.def("render", &FlightSim::render)
	.def("frame", &FlightSim::frame, py::arg("dtWallClock"), py::arg("paused") = false)
	.def("run", &FlightSim::run)
	.def_property_readonly("sim_time", &FlightSim::get_sim_time)
	.def_property_readonly("dynamics_step_size", &FlightSim::get_dynamics_step_size)
	.def("start_recording", &FlightSim::start_recording, py::arg("path") = py::none())
	.def("stop_recording", &FlightSim::stop_recording)
	.def_property_readonly("recording", &FlightSim::is_recording)
	.def_property_readonly("recording_frame_due", &FlightSim::is_recording_frame_due)
	.def_property("recording_directory", &FlightSim::get_recording_directory, &FlightSim::set_recording_directory)
	.def_property("recording_fps", &FlightSim::get_recording_fps, &FlightSim::set_recording_fps)
	.def("get_property", &FlightSim::get_property, py::arg("id"), py::arg("name"))
	.def("get_properties", &FlightSim::get_properties, py::arg("id"), py::arg("names"))
	.def("set_property", &FlightSim::set_property, py::arg("id"), py::arg("name"), py::arg("value"))
	.def("query_property_catalog", &FlightSim::query_property_catalog, py::arg("id"), py::arg("check") = "")
	.def("run_ic", &FlightSim::run_ic, py::arg("id"))
	.def("get_num_engines", &FlightSim::get_num_engines, py::arg("id"))
	.def("init_engine_running", &FlightSim::init_engine_running, py::arg("id"), py::arg("engine"))
	.def("get_steady_state", &FlightSim::get_steady_state, py::arg("id"))
	.def("set_external_control", &FlightSim::set_external_control, py::arg("id"), py::arg("enabled"))
	.def_property_readonly("engineRoot", &FlightSim::get_engine_root, py::return_value_policy::reference)
	.def_property_readonly("world", &FlightSim::get_world, py::return_value_policy::reference)
	.def_property_readonly("visRoot", &FlightSim::get_vis_root, py::return_value_policy::reference)
	.def_property_readonly("window", &FlightSim::get_window, py::return_value_policy::reference)
	.def_property_readonly("camera", &FlightSim::get_camera, py::return_value_policy::reference);
}
