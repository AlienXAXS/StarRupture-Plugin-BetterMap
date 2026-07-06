#include "plugin.h"
#include "plugin_helpers.h"
#include "plugin_config.h"
#include "basecore_rename.h"

// Global plugin self pointer — stable for the plugin's lifetime, retained from PluginInit
static IPluginSelf* g_self = nullptr;

IPluginSelf* GetSelf() { return g_self; }

// Plugin metadata
#ifndef MODLOADER_BUILD_TAG
#define MODLOADER_BUILD_TAG "dev"
#endif

static PluginInfo s_pluginInfo = {
	"BetterMap",
	MODLOADER_BUILD_TAG,
	"AlienX",
	"Alters how things are displayed on the in-game map",
	PLUGIN_INTERFACE_VERSION,
	PLUGIN_TARGET_CLIENT // map UI only exists client-side
};

extern "C" {

	__declspec(dllexport) PluginInfo* GetPluginInfo()
	{
		return &s_pluginInfo;
	}

	__declspec(dllexport) bool PluginInit(IPluginSelf* self)
	{
		// Store the plugin self pointer — valid for the plugin's entire lifetime
		g_self = self;

		LOG_INFO("Plugin initializing...");

		// Initialize config system (optional - creates config file with defaults)
		BetterMapConfig::Config::Initialize(self);

		// Check if plugin is enabled via config
		if (!BetterMapConfig::Config::IsEnabled())
		{
			LOG_WARN("Plugin is disabled in config file");
			return true; // Return true so plugin loads but doesn't activate
		}

		BetterMap::BaseCoreRename::InstallHooks();

		LOG_INFO("Plugin initialized successfully");

		return true;
	}

	__declspec(dllexport) void PluginShutdown()
	{
		LOG_INFO("Plugin shutting down...");

		BetterMap::BaseCoreRename::RemoveHooks();

		g_self = nullptr;
	}

} // extern "C"
