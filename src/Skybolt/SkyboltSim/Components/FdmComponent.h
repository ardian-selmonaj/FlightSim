/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#pragma once

#include "SkyboltSim/Component.h"

#include <glm/vec2.hpp>
#include <string>

namespace skybolt {
namespace sim {

//! Interface of a component that simulates an entity with a flight dynamics model (FDM), e.g. JSBSim,
//! whose inputs and state are accessed by property name, e.g. "fcs/aileron-cmd-norm" or "ic/h-sl-ft".
//! Implemented by the JSBSim plugin. Find it with Entity::getFirstComponent<FdmComponent>().
class FdmComponent : public Component
{
public:
	//! @returns the value of the property, or 0 if the property does not exist
	virtual double getProperty(const std::string& name) const = 0;

	//! Sets the property, which is created if it does not exist
	virtual void setProperty(const std::string& name, double value) = 0;

	//! @returns the properties whose name contains 'check', as one "name (access)" line per property
	virtual std::string queryPropertyCatalog(const std::string& check) const = 0;

	//! Sets the integration time step in seconds
	virtual void setTimeStep(double dt) = 0;

	//! Initializes the FDM from its initial condition properties (e.g. "ic/lat-geod-deg") and
	//! moves the entity to the resulting state. The engines are not started, see initEngineRunning().
	//! @returns false if the initialization failed
	virtual bool runInitialConditions() = 0;

	virtual int getEngineCount() const = 0;

	//! Starts the engine, as if it had been running already
	virtual void initEngineRunning(int engineIndex) = 0;

	//! Runs the engines until their thrust is steady
	//! @returns the result of the FDM's steady state calculation
	virtual bool runEnginesToSteadyState() = 0;

	//! If true, the flight control commands are only set through properties, and the entity's ControlInputsComponent is ignored.
	//! If false (default), they are set from the "stick", "pedal" and "throttle" control inputs before every step, see toFlightControlCommands().
	virtual void setExternalControl(bool externalControl) = 0;
	virtual bool isExternalControl() const = 0;

	std::vector<std::type_index> getExposedTypes() const override
	{
		return { typeid(*this), typeid(FdmComponent) };
	}
};

//! Normalized flight control commands of an FDM: aileron, elevator and rudder in [-1, 1], throttle in [0, 1]
struct FlightControlCommands
{
	double aileron;
	double elevator;
	double rudder;
	double throttle;
};

//! @returns the flight control commands for the "stick", "pedal" and "throttle" control inputs.
//! With JSBSim, positive aileron rolls right, positive elevator pitches down and positive rudder yaws left.
inline FlightControlCommands toFlightControlCommands(const glm::vec2& stick, float pedal, float throttle)
{
	return { stick.x, -stick.y, -pedal, throttle };
}

} // namespace sim
} // namespace skybolt
