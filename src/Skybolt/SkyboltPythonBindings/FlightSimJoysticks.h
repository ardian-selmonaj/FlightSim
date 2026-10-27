/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#pragma once

#include <SkyboltEngine/Input/LogicalAxis.h>
#include <SkyboltSim/System/System.h>

#include <nlohmann/json.hpp>

#define SDL_MAIN_HANDLED // SDL only reads the joysticks, it does not provide main()
#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace skybolt {

//! Flight controls of the manual aircraft that can come from joysticks, throttles and pedals instead of the keyboard
enum class JoystickControl
{
	Roll,
	Pitch,
	Throttle,
	Rudder,
	Fire
};

//! Axis or button of a joystick, throttle or pedals that a flight control comes from
struct JoystickInput
{
	std::string device; //!< name of the device, as shown by Tools/joysticks.py
	int axis = -1; //!< number of the axis as shown by Tools/joysticks.py, for all controls except Fire
	int button = -1; //!< number of the button as shown by Tools/joysticks.py, for Fire
	bool invert = false; //!< reverses the axis
	double deadzone = 0.0; //!< axis values closer to the center are zero, as fraction of the half range (roll, pitch and rudder)
};

//! Joystick inputs of the flight controls. The controls without an input come from the keyboard.
using JoystickMapping = std::map<JoystickControl, JoystickInput>;

namespace detail {

struct JoystickControlInfo
{
	JoystickControl control;
	const char* name; //!< of the control in Tools/joysticks.json
	const char* keys; //!< of the control without its joystick
};

//! @returns the flight controls in the order of the messages
inline const std::vector<JoystickControlInfo>& getJoystickControls()
{
	static const std::vector<JoystickControlInfo> controls = {
		{JoystickControl::Roll, "roll", "keys Left/Right"},
		{JoystickControl::Pitch, "pitch", "keys Up/Down"},
		{JoystickControl::Throttle, "throttle", "keys W/S"},
		{JoystickControl::Rudder, "rudder", "keys A/D"},
		{JoystickControl::Fire, "fire", "key Space"}
	};
	return controls;
}

inline const JoystickControlInfo& getJoystickControlInfo(JoystickControl control)
{
	const auto& controls = getJoystickControls();
	return *std::find_if(controls.begin(), controls.end(), [control] (const JoystickControlInfo& info) { return info.control == control; });
}

} // namespace detail

//! Reads the joystick inputs of the flight controls from a JSON file like Tools/joysticks.json, e.g.
//!     {"pitch": {"device": "Joystick - HOTAS Warthog", "axis": 1, "invert": false, "deadzone": 0.02},
//!      "fire": {"device": "Joystick - HOTAS Warthog", "button": 0}}
//! The flight controls are roll, pitch, throttle, rudder (axes) and fire (button). Keys starting with "_" are comments.
inline JoystickMapping readJoystickMapping(const std::filesystem::path& file)
{
	const std::u8string fileUtf8 = file.u8string();
	auto error = [name = std::string(fileUtf8.begin(), fileUtf8.end())] (const std::string& message) {
		return std::runtime_error("FlightSim: " + name + ": " + message);
	};

	std::ifstream stream(file);
	if (!stream)
	{
		throw error("could not open the file");
	}
	nlohmann::json json;
	try
	{
		stream >> json;
	}
	catch (const nlohmann::json::exception& e)
	{
		throw error(e.what());
	}
	if (!json.is_object())
	{
		throw error("expected an object with the flight controls roll, pitch, throttle, rudder and fire");
	}

	JoystickMapping mapping;
	for (const auto& [name, settings] : json.items())
	{
		if (name.empty() || name.front() == '_')
		{
			continue;
		}
		const auto& controls = detail::getJoystickControls();
		auto info = std::find_if(controls.begin(), controls.end(), [&name] (const detail::JoystickControlInfo& c) { return name == c.name; });
		if (info == controls.end())
		{
			throw error("'" + name + "' is no flight control, they are roll, pitch, throttle, rudder and fire");
		}
		const std::string number = (info->control == JoystickControl::Fire) ? "button" : "axis";
		if (!settings.is_object())
		{
			throw error(name + " needs an object with the device and its " + number);
		}

		std::vector<std::string> allowed = {"device", number};
		if (info->control != JoystickControl::Fire)
		{
			allowed.push_back("invert");
		}
		if (info->control != JoystickControl::Fire && info->control != JoystickControl::Throttle)
		{
			allowed.push_back("deadzone");
		}
		for (const auto& [setting, value] : settings.items())
		{
			if ((setting.empty() || setting.front() != '_') && std::find(allowed.begin(), allowed.end(), setting) == allowed.end())
			{
				std::string names;
				for (const std::string& a : allowed)
				{
					names += (names.empty() ? "" : ", ") + a;
				}
				throw error("'" + setting + "' is no setting of " + name + ", they are " + names);
			}
		}

		JoystickInput input;
		if (!settings.contains("device") || !settings.at("device").is_string() || settings.at("device").get<std::string>().empty())
		{
			throw error(name + " needs the device name, as shown by Tools/joysticks.py");
		}
		input.device = settings.at("device").get<std::string>();

		if (!settings.contains(number) || !settings.at(number).is_number_integer() || settings.at(number).get<int>() < 0)
		{
			throw error(name + " needs the " + number + " number (0 or more), as shown by Tools/joysticks.py");
		}
		(info->control == JoystickControl::Fire ? input.button : input.axis) = settings.at(number).get<int>();

		if (settings.contains("invert"))
		{
			if (!settings.at("invert").is_boolean())
			{
				throw error("invert of " + name + " must be true or false");
			}
			input.invert = settings.at("invert").get<bool>();
		}
		if (settings.contains("deadzone"))
		{
			input.deadzone = settings.at("deadzone").is_number() ? settings.at("deadzone").get<double>() : -1.0;
			if (!(input.deadzone >= 0.0 && input.deadzone < 1.0))
			{
				throw error("deadzone of " + name + " must be a number of at least 0 and less than 1");
			}
		}
		mapping[info->control] = input;
	}
	return mapping;
}

//! Reads the joysticks, throttles and pedals of a JoystickMapping with SDL, before the inputs are used at every simulation update.
//! The devices are found by their names, also when they are plugged in while running. Other devices are ignored.
class Joysticks : public sim::System
{
public:
	//! @param mappingFile is a JSON file like Tools/joysticks.json, see readJoystickMapping()
	explicit Joysticks(const std::filesystem::path& mappingFile) :
		mMapping(readJoystickMapping(mappingFile))
	{
		const std::u8string file = mappingFile.u8string();
		mMappingFile = std::string(file.begin(), file.end());

		for (const auto& [control, input] : mMapping)
		{
			mDeviceNames.insert(input.device);
		}

		if (!mMapping.empty())
		{
			// As with Tools/joysticks.py, the devices are read while the window of FlightSim does not have the focus
			SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
			if (SDL_InitSubSystem(SDL_INIT_JOYSTICK) == 0)
			{
				mInitialized = true;
				SDL_JoystickEventState(SDL_IGNORE); // the states of the devices are read instead of their events
				connectDevices(false);
			}
			else
			{
				std::cerr << "FlightSim: the joysticks cannot be read: " << SDL_GetError() << std::endl;
			}
		}
		printControls();
	}

	~Joysticks() override
	{
		if (mInitialized)
		{
			for (const auto& [name, joystick] : mDevices)
			{
				SDL_JoystickClose(joystick);
			}
			SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
		}
	}

	Joysticks(const Joysticks&) = delete;
	Joysticks& operator=(const Joysticks&) = delete;

	SKYBOLT_BEGIN_REGISTER_UPDATE_HANDLERS
		SKYBOLT_REGISTER_UPDATE_HANDLER(sim::UpdateStage::Input, updateState)
	SKYBOLT_END_REGISTER_UPDATE_HANDLERS

	//! Reads the states of the devices, and connects or disconnects the devices of the mapping that were plugged in or unplugged
	void updateState()
	{
		if (!mInitialized)
		{
			return;
		}
		SDL_JoystickUpdate();

		bool unplugged = std::any_of(mDevices.begin(), mDevices.end(), [] (const auto& device) { return !SDL_JoystickGetAttached(device.second); });
		if ((unplugged || SDL_NumJoysticks() != mDeviceCount) && connectDevices(true))
		{
			printControls();
			if (devicesChanged)
			{
				devicesChanged();
			}
		}

		bool firePressed = isPressed(JoystickControl::Fire);
		if (firePressed && !mFirePressed && fireButtonPressed)
		{
			fireButtonPressed();
		}
		mFirePressed = firePressed;
	}

	//! @returns true if the control comes from its joystick: it is in the mapping, and its device is connected and has its axis or button
	bool isActive(JoystickControl control) const
	{
		return getJoystick(control) != nullptr;
	}

	//! @returns the axis of the control in [-1, 1], inverted and with the deadzone of the mapping, or nullopt if the control is not active
	std::optional<float> getAxis(JoystickControl control) const
	{
		SDL_Joystick* joystick = getJoystick(control);
		if (!joystick)
		{
			return std::nullopt;
		}
		const JoystickInput& input = mMapping.at(control);
		double value = std::clamp(SDL_JoystickGetAxis(joystick, input.axis) / 32767.0, -1.0, 1.0);
		if (input.invert)
		{
			value = -value;
		}
		double magnitude = std::max(0.0, std::abs(value) - input.deadzone) / (1.0 - input.deadzone);
		return float(std::copysign(magnitude, value));
	}

	//! @returns true while the button of the control is pressed, false if the control is not active
	bool isPressed(JoystickControl control) const
	{
		SDL_Joystick* joystick = getJoystick(control);
		return joystick && SDL_JoystickGetButton(joystick, mMapping.at(control).button) != 0;
	}

	//! @returns the input of the control in the mapping, or null if it has none
	const JoystickInput* getInput(JoystickControl control) const
	{
		auto it = mMapping.find(control);
		return (it != mMapping.end()) ? &it->second : nullptr;
	}

	//! Called after devices of the mapping were plugged in or unplugged
	std::function<void()> devicesChanged;

	//! Called when the button of the fire control is pressed
	std::function<void()> fireButtonPressed;

private:
	//! @returns the device of the control if the control is active, otherwise null
	SDL_Joystick* getJoystick(JoystickControl control) const
	{
		const JoystickInput* input = getInput(control);
		if (!input)
		{
			return nullptr;
		}
		auto device = mDevices.find(input->device);
		if (device == mDevices.end())
		{
			return nullptr;
		}
		bool exists = (input->button >= 0) ? input->button < SDL_JoystickNumButtons(device->second) : input->axis < SDL_JoystickNumAxes(device->second);
		return exists ? device->second : nullptr;
	}

	//! Closes the devices that were unplugged and opens the devices of the mapping that were plugged in
	//! @param report prints the changes
	//! @returns true if a device was connected or disconnected
	bool connectDevices(bool report)
	{
		bool changed = false;
		for (auto it = mDevices.begin(); it != mDevices.end();)
		{
			if (SDL_JoystickGetAttached(it->second))
			{
				++it;
				continue;
			}
			if (report)
			{
				std::cout << "FlightSim: " << it->first << " was unplugged" << std::endl;
			}
			SDL_JoystickClose(it->second);
			it = mDevices.erase(it);
			changed = true;
		}

		mDeviceCount = SDL_NumJoysticks();
		for (int i = 0; i < mDeviceCount; ++i)
		{
			const char* name = SDL_JoystickNameForIndex(i);
			if (!name || mDeviceNames.find(name) == mDeviceNames.end() || mDevices.find(name) != mDevices.end())
			{
				continue;
			}
			if (SDL_Joystick* joystick = SDL_JoystickOpen(i); joystick)
			{
				if (report)
				{
					std::cout << "FlightSim: " << name << " was plugged in" << std::endl;
				}
				mDevices[name] = joystick;
				changed = true;
			}
			else
			{
				std::cerr << "FlightSim: could not open " << name << ": " << SDL_GetError() << std::endl;
			}
		}
		return changed;
	}

	//! @returns where the control comes from, e.g. "axis 1 of Joystick - HOTAS Warthog" or "keys A/D (TPR T.Pendular Rudder is not connected)"
	std::string describe(JoystickControl control) const
	{
		const std::string keys = detail::getJoystickControlInfo(control).keys;
		const JoystickInput* input = getInput(control);
		if (!input)
		{
			return keys;
		}
		const std::string number = (input->button >= 0) ? "button " + std::to_string(input->button) : "axis " + std::to_string(input->axis);
		if (isActive(control))
		{
			return number + " of " + input->device;
		}
		if (!mInitialized)
		{
			return keys;
		}
		if (mDevices.find(input->device) == mDevices.end())
		{
			return keys + " (" + input->device + " is not connected)";
		}
		return keys + " (" + input->device + " has no " + number + ")";
	}

	void printControls() const
	{
		std::cout << "FlightSim: flight controls of the manual aircraft (joysticks of " << mMappingFile << ")" << std::endl;
		for (const detail::JoystickControlInfo& info : detail::getJoystickControls())
		{
			std::cout << "    " << info.name << ": " << describe(info.control) << std::endl;
		}
	}

private:
	JoystickMapping mMapping;
	std::string mMappingFile; //!< UTF-8, for messages
	std::set<std::string> mDeviceNames; //!< of the mapping
	bool mInitialized = false; //!< SDL reads the joysticks
	std::map<std::string, SDL_Joystick*> mDevices; //!< connected devices of the mapping by name
	int mDeviceCount = 0; //!< of SDL when the devices were connected, including devices that are not in the mapping
	bool mFirePressed = false;
};

//! @returns the joysticks of the mapping file of the environment variable FLIGHTSIM_JOYSTICKS, which flightsim_env.py sets to
//! Tools/joysticks.json, or null if it is not set
inline std::shared_ptr<Joysticks> createJoysticksFromEnvironment()
{
#ifdef _WIN32
	const wchar_t* file = _wgetenv(L"FLIGHTSIM_JOYSTICKS");
#else
	const char* file = std::getenv("FLIGHTSIM_JOYSTICKS");
#endif
	return (file && *file) ? std::make_shared<Joysticks>(std::filesystem::path(file)) : nullptr;
}

//! Axis of a flight control of the manual aircraft for EntityInputSystem: offset + scale * the joystick axis while the control comes
//! from its joystick, otherwise the keys
class JoystickOrKeyAxis : public LogicalAxis
{
public:
	JoystickOrKeyAxis(std::shared_ptr<const Joysticks> joysticks, JoystickControl control, LogicalAxisPtr keys, float scale = 1.0f, float offset = 0.0f) :
		mJoysticks(std::move(joysticks)),
		mControl(control),
		mKeys(std::move(keys)),
		mScale(scale),
		mOffset(offset)
	{
	}

	void update(float dt) override
	{
		// The keys do not change the control while the joystick flies
		if (!mJoysticks->isActive(mControl))
		{
			mKeys->update(dt);
		}
	}

	void resetState() override { mKeys->resetState(); }

	float getState() const override
	{
		std::optional<float> value = mJoysticks->getAxis(mControl);
		return value ? mOffset + mScale * *value : mKeys->getState();
	}

private:
	std::shared_ptr<const Joysticks> mJoysticks;
	JoystickControl mControl;
	LogicalAxisPtr mKeys;
	float mScale;
	float mOffset;
};

//! Axis that is zero while it is disabled, e.g. keys that move the camera only while they do not fly the aircraft
class EnabledAxis : public LogicalAxis
{
public:
	EnabledAxis(LogicalAxisPtr axis, std::function<bool()> enabled) :
		mAxis(std::move(axis)),
		mEnabled(std::move(enabled))
	{
	}

	void update(float dt) override
	{
		if (mEnabled())
		{
			mAxis->update(dt);
		}
		else
		{
			mAxis->resetState();
		}
	}

	void resetState() override { mAxis->resetState(); }

	float getState() const override { return mEnabled() ? mAxis->getState() : 0.0f; }

private:
	LogicalAxisPtr mAxis;
	std::function<bool()> mEnabled;
};

} // namespace skybolt
