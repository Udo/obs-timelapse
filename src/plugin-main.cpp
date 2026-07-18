/*
OBS Timelapse
Copyright (C) 2026 Udo <udo@openfu.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include "controller.hpp"

#include <memory>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {

std::unique_ptr<timelapse::Controller> controller;

void openTimelapse(void *) noexcept
{
	try {
		if (controller)
			controller->showDialog();
	} catch (const std::exception &exception) {
		obs_log(LOG_ERROR, "could not open configuration dialog: %s", exception.what());
	} catch (...) {
		obs_log(LOG_ERROR, "could not open configuration dialog: unexpected exception");
	}
}

void frontendEvent(enum obs_frontend_event event, void *) noexcept
{
	try {
		if (!controller)
			return;
		if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING)
			controller->installControlsButton();
		else if (event == OBS_FRONTEND_EVENT_PROFILE_CHANGING)
			controller->stop(QStringLiteral("OBS profile changing"));
		else if (event == OBS_FRONTEND_EVENT_EXIT)
			controller->shutdown();
	} catch (const std::exception &exception) {
		obs_log(LOG_ERROR, "frontend event handling failed: %s", exception.what());
	} catch (...) {
		obs_log(LOG_ERROR, "frontend event handling failed: unexpected exception");
	}
}

} // namespace

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("OBS Timelapse.Description");
}

bool obs_module_load(void)
{
	try {
		controller = std::make_unique<timelapse::Controller>();
		obs_frontend_add_tools_menu_item(obs_module_text("Timelapse.MenuItem"), openTimelapse, nullptr);
		obs_frontend_add_event_callback(frontendEvent, nullptr);
		obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
		return true;
	} catch (const std::exception &exception) {
		obs_log(LOG_ERROR, "plugin initialization failed: %s", exception.what());
	} catch (...) {
		obs_log(LOG_ERROR, "plugin initialization failed: unexpected exception");
	}
	controller.reset();
	return false;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(frontendEvent, nullptr);
	if (controller)
		controller->shutdown();
	controller.reset();
	obs_log(LOG_INFO, "plugin unloaded");
}
