#include <SkyboltEngine/ComponentFactory.h>
#include <SkyboltEngine/EngineRoot.h>
#include <SkyboltEngine/Plugin/Plugin.h>
#include <SkyboltSim/Component.h>
#include <SkyboltSim/Components/ControlInputsComponent.h>
#include <SkyboltSim/Components/FdmComponent.h>
#include <SkyboltSim/Components/Motion.h>
#include <SkyboltSim/Entity.h>
#include <SkyboltSim/Spatial/Geocentric.h>
#include <SkyboltSim/Spatial/Orientation.h>
#include <SkyboltSim/Spatial/Position.h>
#include <SkyboltCommon/Json/JsonHelpers.h>
#include <SkyboltCommon/Math/MathUtility.h>
#include <SkyboltCommon/TypeMatcher.h>

#include <JSBSim/FGFDMExec.h>
#include <JSBSim/initialization/FGInitialCondition.h>
#include <JSBSim/models/FGPropulsion.h>
#include <JSBSim/models/propulsion/FGEngine.h>

#include <boost/config.hpp>
#include <boost/dll/alias.hpp>
#include <assert.h>
#include <stdexcept>

namespace skybolt {
using namespace sim;

constexpr double feetToMeters = 0.3048;
constexpr double metersToFeet = 1.0 / feetToMeters;

//! Simulates the entity with JSBSim.
//! Latitudes are geodetic on both sides: the entity's latitude, as used by the map data, is JSBSim's geodetic latitude.
class JsbSimAircraftComponent : public FdmComponent
{
public:
	JsbSimAircraftComponent(Entity* entity, std::unique_ptr<JSBSim::FGFDMExec> exec) :
		mEntity(entity),
		mExec(std::move(exec))
	{
		auto controls = entity->getFirstComponentRequired<ControlInputsComponent>();
		mStickInput = controls->createOrGet<glm::vec2>("stick", glm::vec2(0.0f), posNegUnitRange<glm::vec2>());
		mThrottleInput = controls->createOrGet<float>("throttle", 0.0f);
		mRudderInput = controls->createOrGet<float>("pedal", 0.0f, posNegUnitRange<float>());
	}

	SKYBOLT_BEGIN_REGISTER_UPDATE_HANDLERS
		SKYBOLT_REGISTER_UPDATE_HANDLER(sim::UpdateStage::PreDynamicsSubStep, updatePreDynamics)
	SKYBOLT_END_REGISTER_UPDATE_HANDLERS

	void updatePreDynamics()
	{
		auto currentPosition = getPosition(*mEntity);
		if (!currentPosition)
			return;

		auto currentOrientation = getOrientation(*mEntity);
		if (!currentOrientation)
			return;

		Vector3 currentVelocity = getVelocity(*mEntity).value_or(math::dvec3Zero());

		// Initialize JSBSim from the entity state before the first step, and again if the entity state
		// was changed externally to JSBSim, i.e. the entity was moved to another position, orientation or velocity.
		if (!mInitialized || mLastPosition != currentPosition || mLastOrientation != currentOrientation || mLastVelocity != currentVelocity)
		{
			initializeFromEntityState(*currentPosition, *currentOrientation, currentVelocity);
		}

		// Set flight controls
		if (!mExternalControl)
		{
			FlightControlCommands commands = toFlightControlCommands(mStickInput->value, mRudderInput->value, mThrottleInput->value);
			mExec->SetPropertyValue("fcs/aileron-cmd-norm", commands.aileron);
			mExec->SetPropertyValue("fcs/elevator-cmd-norm", commands.elevator);
			mExec->SetPropertyValue("fcs/rudder-cmd-norm", commands.rudder);
			mExec->SetPropertyValue("fcs/throttle-cmd-norm", commands.throttle);
		}

		// Tick JSBSim
		mExec->Run();

		writeStateToEntity();
	}

	void setSimTime(SecondsD time) override
	{
		if (time == 0)
		{
			// Reset initial conditions
			mInitialized = false;
		}
	}

	void advanceSimTime(SecondsD newTime, SecondsD dt) override
	{
		mExec->Setdt(dt);
	}

public: // FdmComponent interface
	double getProperty(const std::string& name) const override
	{
		return mExec->GetPropertyValue(name);
	}

	void setProperty(const std::string& name, double value) override
	{
		mExec->SetPropertyValue(name, value);
	}

	std::string queryPropertyCatalog(const std::string& check) const override
	{
		return mExec->QueryPropertyCatalog(check);
	}

	void setTimeStep(double dt) override
	{
		mExec->Setdt(dt);
	}

	bool runInitialConditions() override
	{
		bool result = mExec->RunIC();
		mInitialized = true;
		mEnginesStarted = true; // The caller starts the engines, see initEngineRunning()
		writeStateToEntity();
		return result;
	}

	int getEngineCount() const override
	{
		return int(mExec->GetPropulsion()->GetNumEngines());
	}

	void initEngineRunning(int engineIndex) override
	{
		if (engineIndex < 0 || engineIndex >= getEngineCount())
		{
			throw std::out_of_range("JSBSim aircraft has no engine with index " + std::to_string(engineIndex));
		}
		mExec->GetPropulsion()->GetEngine(engineIndex)->InitRunning();
	}

	bool runEnginesToSteadyState() override
	{
		return mExec->GetPropulsion()->GetSteadyState();
	}

	void setExternalControl(bool externalControl) override
	{
		mExternalControl = externalControl;
	}

	bool isExternalControl() const override
	{
		return mExternalControl;
	}

private:
	void initializeFromEntityState(const Vector3& position, const Quaternion& orientation, const Vector3& velocity)
	{
		auto IC = mExec->GetIC();

		sim::LatLonAlt lla = sim::toLatLonAlt(GeocentricPosition(position)).position;
		IC->SetGeodLatitudeRadIC(lla.lat);
		IC->SetLongitudeRadIC(lla.lon);
		IC->SetAltitudeASLFtIC(lla.alt * metersToFeet);

		Quaternion ltpNed = toLtpNed(GeocentricOrientation(orientation), geocentricToLatLon(position)).orientation;
		Vector3 rpy = math::eulerFromQuat(ltpNed);
		IC->SetPhiRadIC(rpy.x);
		IC->SetThetaRadIC(rpy.y);
		IC->SetPsiRadIC(rpy.z);

		Matrix3 ltpOrientation = geocentricToLtpOrientation(position);
		Vector3 velocityNed = glm::transpose(ltpOrientation) * velocity;
		IC->SetVNorthFpsIC(velocityNed.x * metersToFeet);
		IC->SetVEastFpsIC(velocityNed.y * metersToFeet);
		IC->SetVDownFpsIC(velocityNed.z * metersToFeet);

		mExec->RunIC();
		mInitialized = true;

		// Start all engines on the first initialization. Later initializations keep the engine state.
		if (!mEnginesStarted)
		{
			mExec->SetPropertyValue("propulsion/set-running", -1);
			mEnginesStarted = true;
		}
	}

	void writeStateToEntity()
	{
		// Update position
		sim::LatLonAlt lla(
			mExec->GetPropertyValue("position/lat-geod-rad"),
			mExec->GetPropertyValue("position/long-gc-rad"),
			mExec->GetPropertyValue("position/h-sl-meters")
		);

		Vector3 position = sim::toGeocentric(LatLonAltPosition(lla)).position;
		setPosition(*mEntity, position);
		mLastPosition = position;

		// Update orientation
		sim::Vector3 eulerRpy(
			mExec->GetPropertyValue("attitude/roll-rad"),
			mExec->GetPropertyValue("attitude/pitch-rad"),
			mExec->GetPropertyValue("attitude/heading-true-rad") // NOTE: heading is yaw
		);

		Quaternion ltpNedOrientation = math::quatFromEuler(eulerRpy);
		Quaternion orientation = toGeocentric(LtpNedOrientation(ltpNedOrientation), toLatLon(lla)).orientation;
		setOrientation(*mEntity, orientation);
		mLastOrientation = orientation;

		// Update linear velocity
		Matrix3 ltpOrientation = geocentricToLtpOrientation(position);
		sim::Vector3 velocity = ltpOrientation * sim::Vector3(
			mExec->GetPropertyValue("velocities/v-north-fps"),
			mExec->GetPropertyValue("velocities/v-east-fps"),
			mExec->GetPropertyValue("velocities/v-down-fps")
		) * feetToMeters;
		setVelocity(*mEntity, velocity);
		mLastVelocity = velocity;

		//update angular velocity in body axes
		if (auto motion = mEntity->getFirstComponent<Motion>(); motion)
		{
			motion->angularVelocity = orientation * sim::Vector3(
				mExec->GetPropertyValue("velocities/p-rad_sec"),
				mExec->GetPropertyValue("velocities/q-rad_sec"),
				mExec->GetPropertyValue("velocities/r-rad_sec"));
		}
	}

private:
	sim::Entity* mEntity;
	std::unique_ptr<JSBSim::FGFDMExec> mExec;

	std::optional<sim::Vector3> mLastPosition;
	std::optional<sim::Quaternion> mLastOrientation;
	std::optional<sim::Vector3> mLastVelocity;
	bool mInitialized = false;
	bool mEnginesStarted = false;
	bool mExternalControl = false;

	std::shared_ptr<ControlInputT<glm::vec2>> mStickInput;
	std::shared_ptr<ControlInputT<float>> mThrottleInput;
	std::shared_ptr<ControlInputT<float>> mRudderInput;
};

static sim::ComponentPtr loadJsbSimAircraftComponent(Entity* entity, const ComponentFactoryContext& context, const nlohmann::json& json, const std::string& defaultJsbSimRootDir)
{
	std::string jsbSimRoot = readOptionalOrDefault(json, "jsbSimRootDir", defaultJsbSimRootDir);
	std::string model = json.at("model");

	auto exec = std::make_unique<JSBSim::FGFDMExec>();
	exec->SetDebugLevel(0); // disable JSBSim debug output
	exec->SetRootDir(SGPath(jsbSimRoot));
	exec->SetAircraftPath(SGPath(jsbSimRoot + "/aircraft"));
	exec->SetEnginePath(SGPath(jsbSimRoot + "/engine"));
	exec->SetSystemsPath(SGPath(jsbSimRoot + "/systems"));
	if (!exec->LoadModel(model))
	{
		throw std::runtime_error("JSBSim could not load aircraft model '" + model + "' from '" + jsbSimRoot + "/aircraft'. Check 'jsb.rootDir' in the Skybolt settings.");
	}

	assert(entity);
	return std::make_shared<JsbSimAircraftComponent>(entity, std::move(exec));
}

static std::string readDefaultJsbSimRootDir(const nlohmann::json& j)
{
	std::string rootDir;
	ifChildExists(j, "jsb", [&] (const nlohmann::json& child) {
		rootDir = child.at("rootDir");
		});

	if (rootDir.empty())
	{
		rootDir = ".";
		BOOST_LOG_TRIVIAL(warning) << "'jsb.rootDir' setting not found in engine settings file. Setting to default of '" << rootDir << "'";
	}

	return rootDir;
}

const std::string JsbSimComponentName = "jsbSimAircraft";

class JsbSimPlugin : public Plugin
{
public:
	JsbSimPlugin(const PluginConfig& config) :
		mComponentFactoryRegistry(valueOrThrowException(getExpectedRegistry<ComponentFactoryRegistry>(*config.engineRoot->factoryRegistries)))
	{
		const nlohmann::json& settings = config.engineRoot->engineSettings;
		std::string defaultJsbSimRootDir = readDefaultJsbSimRootDir(settings);

		(*mComponentFactoryRegistry)[JsbSimComponentName] = std::make_shared<ComponentFactoryFunctionAdapter>([defaultJsbSimRootDir] (Entity* entity, const ComponentFactoryContext& context, const nlohmann::json& json) {
			return loadJsbSimAircraftComponent(entity, context, json, defaultJsbSimRootDir);
			});
	}

	~JsbSimPlugin()
	{
		mComponentFactoryRegistry->erase(JsbSimComponentName);
	}

private:
	ComponentFactoryRegistryPtr mComponentFactoryRegistry;
};

namespace plugins {

	std::shared_ptr<Plugin> createEnginePlugin(const PluginConfig& config)
	{
		return std::make_shared<JsbSimPlugin>(config);
	}

	BOOST_DLL_ALIAS(
		plugins::createEnginePlugin,
		createEnginePlugin
	)
}

} // namespace skybolt {
