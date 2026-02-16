/* Copyright Matthew Reid
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

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

#include <iostream>
#include <filesystem>
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

class CameraViewSelector : public EventListener
{
public:
	CameraViewSelector(const CameraControllerComponentPtr& cameraController, const std::shared_ptr<HudSystem>& hudSystem, const std::function<void(bool)>& hudVisibilitySwitch) :
		mCameraController(cameraController),
		mHudSystem(hudSystem),
		mHudVisibilitySwitch(hudVisibilitySwitch)
	{
		assert(mCameraController);
		assert(mHudSystem);
		mCameraController->selectController("Cockpit");
	}

	void onEvent(const Event& event) override
	{
		if (const KeyEvent* keyEvent = dynamic_cast<const KeyEvent*>(&event))
		{
			if (keyEvent->type == KeyEvent::Pressed)
			{
				switch (keyEvent->code)
				{
					case KC_F1:
					{
						mCameraController->selectController("Cockpit");
						mHudSystem->setPitchLadderVisible(true);
						mHudVisibilitySwitch(true);
						break;
					}
					case KC_F2:
					{
						mCameraController->selectController("Follow");
						mHudSystem->setPitchLadderVisible(false);
						mHudVisibilitySwitch(true);
						break;
					}
					case KC_F3:
					{
                        mCameraController->selectController("Free");
                        mHudSystem->setPitchLadderVisible(false);
                        mHudVisibilitySwitch(true);
                        break;
					}
                    default:
                        break;
				}
			}
		}
	}

private:
	CameraControllerComponentPtr mCameraController;
	std::shared_ptr<HudSystem> mHudSystem;
	std::function<void(bool)> mHudVisibilitySwitch;
};


class CameraViewSelectorSimpel : public EventListener
{
public:
    CameraViewSelectorSimpel(const CameraControllerComponentPtr& cameraController)
        : mCameraController(cameraController)
    {
        mCameraController->selectController("Cockpit");
    }

    void onEvent(const Event& event) override
    {
        if (const KeyEvent* keyEvent = dynamic_cast<const KeyEvent*>(&event))
        {
            if (keyEvent->type == KeyEvent::Pressed)
            {
                switch (keyEvent->code)
                {
                    case KC_F1:
                        mCameraController->selectController("Cockpit");
                        break;

                    case KC_F2:
                        mCameraController->selectController("Follow");
                        break;

                    case KC_F3:
                        mCameraController->selectController("Free");
                        break;

                    default:
                        break;
                }
            }
        }
    }

private:
    CameraControllerComponentPtr mCameraController;
};


static osg::ref_ptr<HelpDisplayRenderOperation> createHelpDisplay(bool manualControl = false)
{
    std::string helpMessage;
    if (manualControl){
        helpMessage =
R"(= Controls =

W: Throttle +
S: Throttle -
A: Rudder Left
D: Rudder Right
Up: Pitch up
Down: Pitch down
Left: Roll right
Right: Roll left

F1: Cockpit view
F2: External view
F3: Free view

H: Toggle help message
Esc: Exit
Mouse: Pan camera
)";
    } else {
        helpMessage =
R"(= Controls =
F1: Cockpit view
F2: External view
F3: Free view

In Free view (F3):
Shift (hold) + Mouse: Camera tilt modifier
W: Zoom in
S: Zoom out

H: Toggle help message
Esc: Exit
Mouse: Rotate camera
)";
    }
    osg::ref_ptr<HelpDisplayRenderOperation> hd = new HelpDisplayRenderOperation();
    hd->setMessage(helpMessage);
    return hd;
}



class FlightSim
{
public:
    FlightSim(int x = 0, int y = 0, int w = 1080, int h = 720,
                const std::map<int, std::string>& entityTypes = {{1, "UH60"}},
                bool manual = false, bool multiwindow = false)
        : entityTypes(entityTypes),
          manualControl(manual),
          multiwindowEnabled(multiwindow)
    {
        engineRoot = createEngineRoot();

        visRoot = std::make_unique<vis::VisRoot>();
        visRoot->getViewer().setKeyEventSetsDone(osgGA::GUIEventAdapter::KEY_Escape);

        window = std::make_shared<vis::StandaloneWindow>(vis::RectI(x, y, w, h));
        visRoot->addWindow(window);

        simCamera = engineRoot->entityFactory->createEntity("Camera");
        engineRoot->scenario->world.addEntity(simCamera);

        cameraControllerComponent = simCamera->getFirstComponentRequired<CameraControllerComponent>();
        auto orbitController = cameraControllerComponent->getControllerOfType<OrbitCameraController>();
        orbitController->setLagTimeConstant(0.3);
        orbitController->setTargetOffset(sim::Vector3(0, 0, -3));
        orbitController->setZoom(0.8);

        viewport = createAndAddViewportToWindowWithEngine(*window, *engineRoot);
        viewport->setCamera(getVisCamera(*simCamera));

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

        // Input platform and axes
        inputPlatform = std::make_shared<InputPlatformOsg>(window->getView());
        if (manualControl){
            logicalAxes = createAircraftInputAxesKeyboard(*inputPlatform);
            engineRoot->systemRegistry->push_back(std::make_shared<InputSystem>(inputPlatform, logicalAxes));
            cameraInputSystem = std::make_shared<CameraInputSystem>(inputPlatform);
        } else {
            cameraAxes = createDefaultCameraInputAxes(*inputPlatform);
            engineRoot->systemRegistry->push_back(std::make_shared<InputSystem>(inputPlatform, toValuesVector(cameraAxes)));
            cameraInputSystem = std::make_shared<CameraInputSystem>(inputPlatform, cameraAxes);
        }

        configure(*cameraInputSystem, window->getWidth(), engineRoot->engineSettings);
        //connectToCamera(*cameraInputSystem, simCamera);
		connectToCameraExclusivly(*cameraInputSystem, &engineRoot->scenario->world, simCamera->getId());
        engineRoot->systemRegistry->push_back(cameraInputSystem);

        if (manualControl){

            overlayCamera = viewport->getFinalRenderTarget()->getOsgCamera();

            entityInputSystem = std::make_shared<EntityInputSystem>(logicalAxes);
            engineRoot->systemRegistry->push_back(entityInputSystem);

            vis::CameraPtr visCam = getVisCamera(*simCamera);
            hudSystem = std::make_shared<HudSystem>(overlayCamera, [visCam] { return visCam->getFovY(); });
            engineRoot->systemRegistry->push_back(hudSystem);

            auto hudVisibilitySwitch = [&](bool visible) { hudSystem->setEnabled(visible); };
            cameraViewSelector = std::make_shared<CameraViewSelector>(cameraControllerComponent, hudSystem, hudVisibilitySwitch);
            inputPlatform->getEventEmitter()->addEventListener<KeyEvent>(cameraViewSelector.get());
        } else {
            cameraViewSelectorSimpel = std::make_shared<CameraViewSelectorSimpel>(cameraControllerComponent);
            inputPlatform->getEventEmitter()->addEventListener<KeyEvent>(cameraViewSelectorSimpel.get());
        }

        helpDisplay = createHelpDisplay(manualControl);
        window->getRenderOperationSequence().addOperation(helpDisplay, (int)vis::RenderOperationOrder::Hud);

        helpToggleListener = std::make_shared<HelpDisplayToggleEventListener>(helpDisplay);
        inputPlatform->getEventEmitter()->addEventListener<KeyEvent>(helpToggleListener.get());

        create_entities();
    }

    void create_entities()
    {
        // Create multiple aircraft based on entityTypes map
        for (const auto& [id, type] : entityTypes)
        {
            sim::EntityPtr aircraft = engineRoot->entityFactory->createEntity(type);
            engineRoot->scenario->world.addEntity(aircraft);
            
            // initial pose (at 0° lat, 0° lon, 500 m)
            sim::LatLonAlt boeingFieldPosition(45.975305 * math::degToRadD(), 8.965318 * math::degToRadD(), 2000);
            Quaternion aircraftOrientation = toGeocentric(sim::LtpNedOrientation(math::quatIdentity()), sim::toLatLon(boeingFieldPosition)).orientation;
			setPosition(*aircraft, toGeocentric(sim::LatLonAltPosition(boeingFieldPosition)).position);
            setOrientation(*aircraft, aircraftOrientation);
            setVelocity(*aircraft, aircraftOrientation * Vector3(100,0,0));

            aircraftMap[id] = aircraft;
        }

        // Environment - these will get IDs starting from the next available ID
        createEnvironmentEntities(*engineRoot->entityFactory, engineRoot->scenario->world);

        set_camera_view();

        // Time of day
        engineRoot->scenario->startJulianDate = sim::calcJulianDate(2030, 6, 6, 12);
    }

    void set_entity_attitude(int entityId, const sim::LatLonAlt& position, const sim::Vector3& rpy)
    {
        // Try aircraft map first
        auto itA = aircraftMap.find(entityId);
        if (itA != aircraftMap.end())
        {
            sim::EntityPtr& entity = itA->second;
            if (!entity){
                throw std::runtime_error("FlightSim: aircraft entity pointer is null for ID " + std::to_string(entityId));
            }
            setPosition(*entity, toGeocentric(sim::LatLonAltPosition(position)).position);
            setOrientation(*entity, toGeocentric(sim::LtpNedOrientation(math::quatFromEuler(rpy)),sim::toLatLon(position)).orientation);
            return;
        }

        // Then rocket map
        auto itR = rocketMap.find(entityId);
        if (itR != rocketMap.end())
        {
            sim::EntityPtr& entity = itR->second;
            if (!entity){
                throw std::runtime_error("FlightSim: rocket entity pointer is null for ID " + std::to_string(entityId));
            }
            setPosition(*entity, toGeocentric(sim::LatLonAltPosition(position)).position);
            setOrientation(*entity,toGeocentric(sim::LtpNedOrientation(math::quatFromEuler(rpy)), sim::toLatLon(position)).orientation);
            return;
        }

        // Not found in either map
        throw std::runtime_error("FlightSim: entity with ID " + std::to_string(entityId) + " not found");
    }

    void remove_entity(int entityId)
    {
        // Try aircraft map first
        auto itA = aircraftMap.find(entityId);
        if (itA != aircraftMap.end())
        {
            sim::EntityPtr& entity = itA->second;
            if (!entity){
                throw std::runtime_error("FlightSim: aircraft entity pointer is null for ID " + std::to_string(entityId));
            }
            engineRoot->scenario->world.removeEntity(entity.get());
            aircraftMap.erase(itA);

            // Update camera view
            set_camera_view();
            return;
        }

        // Then rocket map
        auto itR = rocketMap.find(entityId);
        if (itR != rocketMap.end())
        {
            sim::EntityPtr& entity = itR->second;
            if (!entity){
                throw std::runtime_error("FlightSim: rocket entity pointer is null for ID " + std::to_string(entityId));
            }

            engineRoot->scenario->world.removeEntity(entity.get());
            rocketMap.erase(itR);
            return;
        }

        // Not found in either map
        throw std::runtime_error("FlightSim: entity with ID " + std::to_string(entityId) + " not found");
    }

    int fire_rocket(int entityId)
    {
        // changed Matrix component in ShuttleSRB.osgt file to following values to decrease size of Rocket:
        // Matrix {
        // -0.00000  -0.25000   0.00000   0.00000
        // 0.15000  -0.00000   0.00000   0.00000
        // 0.00000   0.00000   0.25000   0.00000
        // 0.00000   0.00000   0.00000   1.00000
        // }
        
        sim::EntityPtr rocket = engineRoot->entityFactory->createEntity("ShuttleSRB");
        engineRoot->scenario->world.addEntity(rocket);

        auto it = aircraftMap.find(entityId);
        if (it == aircraftMap.end())
        {
            throw std::runtime_error("FlightSim: aircraft with ID " + std::to_string(entityId) + " not found");
        }
        
        setPosition(*rocket, *getPosition(*it->second));
        setOrientation(*rocket, *getOrientation(*it->second));
        rocketMap[rocket->getId().entityId] = rocket;

        return rocket->getId().entityId;
    }

    void set_camera_view()
    {
        // Configure camera to point to the first aircraft (lowest ID)
        if (!aircraftMap.empty())
        {
            const sim::EntityPtr& firstAircraft = aircraftMap.begin()->second;
            cameraControllerComponent->setTargetId(firstAircraft->getId());

            if (manualControl) {
                entityInputSystem->setEntity(firstAircraft);
                hudSystem->setEntity(firstAircraft.get());
            }
        }
    }

    // Manual single frame, identical to runMainLoop inner lambda
    bool frame(double dtWallClock, bool paused = false)
    {
        double dtSim = paused ? 0.0 : dtWallClock;

        SimStepper stepper(engineRoot->systemRegistry);
        stepper.update(dtSim);

        for (const auto& system : *engineRoot->systemRegistry)
        {
            system->advanceWallTime(currentWallTime, dtWallClock);
        }
        currentWallTime += dtWallClock;

        return visRoot->render();
    }

    void run()
    {
        runMainLoop(*visRoot, *engineRoot, UpdateLoop::neverExit);
    }

    // Useful accessors
    EngineRoot* get_engine_root() { return engineRoot.get(); }
    sim::World* get_world() { return &engineRoot->scenario->world; }
    vis::VisRoot* get_vis_root() { return visRoot.get(); }
    vis::Window* get_window() { return window.get(); }
    sim::Entity* get_camera() { return simCamera.get(); }
    sim::Entity* get_aircraft(int id) 
    { 
        auto it = aircraftMap.find(id);
        return (it != aircraftMap.end()) ? it->second.get() : nullptr;
    }
    
    std::map<int, sim::EntityPtr> get_all_aircraft() { return aircraftMap; }

private:
    std::unique_ptr<EngineRoot> engineRoot;

    std::unique_ptr<vis::VisRoot> visRoot;
    std::shared_ptr<vis::Window> window;
    osg::ref_ptr<vis::RenderCameraViewport> viewport;

    std::map<int, sim::EntityPtr> aircraftMap;
    std::map<int, std::string> entityTypes;
    std::map<int, sim::EntityPtr> rocketMap;
    sim::EntityPtr simCamera;
    CameraControllerComponentPtr cameraControllerComponent;

    std::shared_ptr<InputPlatformOsg> inputPlatform;
    CameraInputAxes cameraAxes;
    std::vector<LogicalAxisPtr> logicalAxes;
    std::shared_ptr<CameraInputSystem> cameraInputSystem;

    osg::ref_ptr<HelpDisplayRenderOperation> helpDisplay;
    std::shared_ptr<HelpDisplayToggleEventListener> helpToggleListener;
    std::shared_ptr<CameraViewSelectorSimpel> cameraViewSelectorSimpel;

    osg::ref_ptr<osg::Camera> overlayCamera;
    std::shared_ptr<EntityInputSystem> entityInputSystem;
    std::shared_ptr<HudSystem> hudSystem;
    std::shared_ptr<CameraViewSelector> cameraViewSelector;

    double currentWallTime = 0.0;

    sim::EntityPtr simCamera2;
    CameraControllerComponentPtr cameraControllerComponent2;

    std::shared_ptr<vis::Window> window2;
    osg::ref_ptr<vis::RenderCameraViewport> viewport2;

    bool multiwindowEnabled = false;
    bool manualControl = false;
};


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

	py::class_<FlightSim>(m, "FlightSim")
    .def(py::init<int,int,int,int,const std::map<int, std::string>&,bool,bool>(),
         py::arg("x") = 0,
         py::arg("y") = 0,
         py::arg("width") = 1080,
         py::arg("height") = 720,
         py::arg("entityTypes") = std::map<int, std::string>{{1, "UH60"}},
         py::arg("manual") = false,
         py::arg("multiwindow") = false)
    .def("create_entities", &FlightSim::create_entities)
    .def("set_entity_attitude", &FlightSim::set_entity_attitude, 
         py::arg("entityId"), py::arg("position"), py::arg("rpy"))
    .def("remove_entity", &FlightSim::remove_entity, py::arg("entityId"))
    .def("fire_rocket", &FlightSim::fire_rocket, py::arg("entityId"))
    .def("frame", &FlightSim::frame, py::arg("dtWallClock"), py::arg("paused") = false)
    .def("run", &FlightSim::run)
    .def_property_readonly("engineRoot", &FlightSim::get_engine_root, py::return_value_policy::reference)
    .def_property_readonly("world", &FlightSim::get_world, py::return_value_policy::reference)
    .def_property_readonly("visRoot", &FlightSim::get_vis_root, py::return_value_policy::reference)
    .def_property_readonly("window", &FlightSim::get_window, py::return_value_policy::reference)
    .def_property_readonly("camera", &FlightSim::get_camera, py::return_value_policy::reference)
    .def("get_aircraft", &FlightSim::get_aircraft, py::arg("id"), py::return_value_policy::reference)
    .def_property_readonly("all_aircraft", &FlightSim::get_all_aircraft);
}
