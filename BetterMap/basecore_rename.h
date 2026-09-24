#pragma once

#include "plugin_helpers.h"
#include "Engine_classes.hpp"
#include "ChimeraUI_classes.hpp"
#include "Chimera_classes.hpp"

#include <string>

// Overrides the map marker label for a base's Core building, which the game
// always hard-codes to "Base Core" instead of the player-provided custom name.
//
// UCrUW_MapMenuMapArea::CreateBuildingMarker copies Data->BuildingName directly
// into the new marker widget's BuildingName field (verified via IDA pseudocode)
// -- it is NOT looked up from category data, so it's per-instance replicated
// text. UCrUW_MapMenuMapArea::ChangeBuildingMarker (the update path once a
// marker already exists) never touches BuildingName, only Flags/malfunction
// status -- so the name only needs to be corrected once, at creation.
//
// The custom name is resolved straight from Data->BuildingHandle (the marker's
// replicated FMassEntityHandle) via the game's own
// UCrBuildingCustomNameSubsystem::GetBuildingCustomNameByReplicationHelper
// path -- the same dispatcher GetBuildingCustomName(AActor*) itself uses, which
// picks between a purely-replicated client-side name cache (multiplayer clients)
// and a direct/local lookup (standalone or listen-server) based on NetMode. This
// replaces an earlier approach that scanned GetAllActorsOfClass(ACrBaseCore) and
// matched by nearest 2D distance to Data->Location: that only worked for cores
// whose AActor happened to be locally spawned, i.e. cores near the player.
namespace BetterMap::BaseCoreRename
{
	inline constexpr const char* CreateBuildingMarkerPattern =
		"40 53 55 56 57 48 81 EC ?? ?? ?? ?? 48 8B FA 48 8B E9 E8 ?? ?? ?? ?? 48 8B D8";

	using CreateBuildingMarker_t = bool(__fastcall*)(SDK::UCrUW_MapMenuMapArea* self, const SDK::FBuildingMarkerDataFastArrayItem* Data);

	inline CreateBuildingMarker_t g_originalCreateBuildingMarker = nullptr;
	inline HookHandle             g_createBuildingMarkerHook     = nullptr;

	// Neither of these is a UFUNCTION, so Dumper-7 doesn't generate a callable
	// wrapper for them -- resolved by pattern scan and called directly, same as
	// CreateBuildingMarker above, just without installing a detour trampoline.
	inline constexpr const char* ReplicationHelperCtorPattern =
		"48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 57 48 83 EC ?? 33 ED C7 41 ?? ?? ?? ?? ?? ?? ?? 48 8B F1";
	inline constexpr const char* GetNameByReplicationHelperPattern =
		"48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 57 48 83 EC ?? 49 8B E9 49 8B D8 48 8B FA 48 8B F1 E8 ?? ?? ?? ?? 48 8B C8 E8";

	using ReplicationHelperCtor_t = void(__fastcall*)(SDK::FCrMassEntityReplicationHelper* self, SDK::FMassEntityHandle entity, const SDK::UObject* worldContextObject);
	using GetNameByReplicationHelper_t = SDK::FString*(__fastcall*)(SDK::UCrBuildingCustomNameSubsystem* self, SDK::FString* result, SDK::FCrMassEntityReplicationHelper* entity, bool* outIsDefaultName);

	inline ReplicationHelperCtor_t         g_replicationHelperCtor      = nullptr;
	inline GetNameByReplicationHelper_t    g_getNameByReplicationHelper = nullptr;

	// Filled in by ResolvePatterns during OnPluginLoadHooks; 0 means that pattern
	// missed on this build and the feature behind it degrades.
	inline uintptr_t g_addrCreateBuildingMarker = 0;

	// Resolves a building's current custom name directly from its replicated Mass
	// entity handle -- no AActor lookup required. Mirrors the dispatch logic inside
	// UCrBuildingCustomNameSubsystem::GetBuildingCustomName(AActor*), minus the
	// AActor->component->handle indirection that function starts from.
	//
	// outIsDefaultName is set true when the subsystem couldn't find a real custom
	// name and fell back to the building's default type name (e.g. "Base Core"):
	// on a multiplayer client that also means it just kicked off an async
	// server request (RequestBuildingCustomNameUpdate) under the hood -- the real
	// name will be cached for next time (e.g. the next time this marker is
	// recreated), but won't retroactively update an already-created marker.
	inline SDK::FString GetBaseCoreCustomName(SDK::UWorld* world, SDK::FMassEntityHandle handle, bool& outIsDefaultName)
	{
		SDK::FString result{};
		outIsDefaultName = true;

		LOG_TRACE("[BetterMap] GetBaseCoreCustomName: handle Index=%d SerialNumber=%d", handle.Index, handle.SerialNumber);

		if (!g_replicationHelperCtor || !g_getNameByReplicationHelper)
		{
			LOG_DEBUG("[BetterMap] GetBaseCoreCustomName: replication helper functions not resolved (ctor=%p, getName=%p), skipping",
				reinterpret_cast<void*>(g_replicationHelperCtor), reinterpret_cast<void*>(g_getNameByReplicationHelper));
			return result;
		}

		auto* nameSys = static_cast<SDK::UCrBuildingCustomNameSubsystem*>(
			SDK::USubsystemBlueprintLibrary::GetWorldSubsystem(world, SDK::UCrBuildingCustomNameSubsystem::StaticClass()));
		LOG_TRACE("[BetterMap] GetBaseCoreCustomName: nameSys=%p", static_cast<void*>(nameSys));
		if (!nameSys)
		{
			LOG_DEBUG("[BetterMap] GetBaseCoreCustomName: no UCrBuildingCustomNameSubsystem");
			return result;
		}

		SDK::FCrMassEntityReplicationHelper helper{};
		g_replicationHelperCtor(&helper, handle, static_cast<const SDK::UObject*>(world));
		LOG_TRACE("[BetterMap] GetBaseCoreCustomName: helper NetID=%u PersistentID=%d PersistentHandle(Index=%d,SerialNumber=%d)",
			helper.NetID.Value, helper.Entity.ID, helper.Entity.CachedHandle.Index, helper.Entity.CachedHandle.SerialNumber);

		g_getNameByReplicationHelper(nameSys, &result, &helper, &outIsDefaultName);
		return result;
	}

	inline bool __fastcall CreateBuildingMarkerDetour(SDK::UCrUW_MapMenuMapArea* self, const SDK::FBuildingMarkerDataFastArrayItem* Data)
	{
		LOG_TRACE("[BetterMap] CreateBuildingMarkerDetour: self=%p Data=%p", static_cast<void*>(self), static_cast<const void*>(Data));

		if (Data)
		{
			const std::string uniqueName = Data->BuildingUniqueName.ToString();
			LOG_TRACE("[BetterMap] CreateBuildingMarkerDetour: BuildingUniqueName='%s'", uniqueName.c_str());

			if (uniqueName == "BaseCore")
			{
				LOG_DEBUG("[BetterMap] CreateBuildingMarkerDetour: Base Core marker being created, resolving custom name...");

				SDK::UWorld* world = SDK::UWorld::GetWorld();
				if (!world)
				{
					LOG_DEBUG("[BetterMap] CreateBuildingMarkerDetour: no UWorld, skipping rename");
				}
				else
				{
					bool isDefaultName = true;
					SDK::FString customName = GetBaseCoreCustomName(world, Data->BuildingHandle, isDefaultName);
					const std::string customNameStr = customName.ToString();
					LOG_DEBUG("[BetterMap] CreateBuildingMarkerDetour: custom name='%s' (length=%d, isDefaultName=%d)", customNameStr.c_str(), customName.Num(), isDefaultName);

					if (customName.Num() > 0 && !isDefaultName)
					{
						// Conv_StringToText round-trips through UObject::ProcessEvent (the real
						// engine reflection call), so the returned FText owns a freshly-created,
						// correctly-refcounted ITextData -- safe to hand off below without us
						// having to replicate the engine's AddRef/Release bookkeeping ourselves.
						SDK::FText newText = SDK::UKismetTextLibrary::Conv_StringToText(customName);

						// Overwrite in place; CreateBuildingMarker's own (unmodified) code below
						// will AddRef this into the marker widget correctly. This does leak the
						// one ref that was on the replicated item's previous BuildingName, since
						// we don't call Release on it -- a small one-time cost per marker
						// creation, not a correctness/crash risk.
						const_cast<SDK::FBuildingMarkerDataFastArrayItem*>(Data)->BuildingName = newText;

						LOG_INFO("[BetterMap] Renamed Base Core map marker to '%s'", customNameStr.c_str());
					}
					else
					{
						LOG_DEBUG("[BetterMap] CreateBuildingMarkerDetour: no custom name set for this Base Core, leaving default label");
					}
				}
			}
		}

		return g_originalCreateBuildingMarker(self, Data);
	}

	// All three addresses below are function entries -- one gets a detour, the
	// other two are called directly -- so each request declares
	// PLUGIN_SCAN_FUNCTION_START. The loader checks that against the
	// executable's exception directory, which is what separates "the bytes
	// matched" from "the bytes matched the right function".
	inline uintptr_t ResolveFunction(IPluginSelf* self, IPluginHookScanner* scanner,
		const char* hookName, const char* pattern)
	{
		PluginScanRequest req = PLUGIN_SCAN_REQUEST_INIT;
		req.hookName = hookName;
		req.pattern  = pattern;
		req.kind     = PLUGIN_SCAN_FUNCTION_START;
		req.flags    = PLUGIN_SCAN_FLAG_OPTIONAL;

		return scanner->Resolve(self, &req);
	}

	// Resolve every AOB this module needs. Callable only from the plugin's
	// OnPluginLoadHooks export -- the loader refuses scans made anywhere else, and
	// nothing may be detoured until PluginInit.
	inline void ResolvePatterns(IPluginSelf* self, IPluginHookScanner* scanner)
	{
		if (!self || !scanner)
			return;

		// Optional throughout: a miss degrades Base Core renaming but leaves the
		// rest of BetterMap working, which is what the old scan-at-init path did.
		g_addrCreateBuildingMarker = ResolveFunction(self, scanner,
			"UCrUW_MapMenuMapArea::CreateBuildingMarker", CreateBuildingMarkerPattern);

		if (uintptr_t ctorAddr = ResolveFunction(self, scanner,
				"FCrMassEntityReplicationHelper::FCrMassEntityReplicationHelper", ReplicationHelperCtorPattern))
			g_replicationHelperCtor = reinterpret_cast<ReplicationHelperCtor_t>(ctorAddr);

		if (uintptr_t getNameAddr = ResolveFunction(self, scanner,
				"UCrBuildingCustomNameSubsystem::GetBuildingCustomNameByReplicationHelper", GetNameByReplicationHelperPattern))
			g_getNameByReplicationHelper = reinterpret_cast<GetNameByReplicationHelper_t>(getNameAddr);
	}

	inline void InstallHooks()
	{
		uintptr_t addr = g_addrCreateBuildingMarker;
		if (!addr)
		{
			LOG_ERROR("[BetterMap] UCrUW_MapMenuMapArea::CreateBuildingMarker unresolved");
			return;
		}

		g_createBuildingMarkerHook = GetHooks()->Hooks->Install(addr, (void*)&CreateBuildingMarkerDetour, (void**)&g_originalCreateBuildingMarker);
		if (!g_createBuildingMarkerHook)
		{
			LOG_ERROR("[BetterMap] Failed to install CreateBuildingMarker hook at 0x%llX", addr);
			return;
		}

		LOG_INFO("[BetterMap] CreateBuildingMarker hook installed at 0x%llX", addr);

		if (!g_replicationHelperCtor)
			LOG_ERROR("[BetterMap] FCrMassEntityReplicationHelper::FCrMassEntityReplicationHelper unresolved -- Base Core renaming will be disabled");

		if (!g_getNameByReplicationHelper)
			LOG_ERROR("[BetterMap] UCrBuildingCustomNameSubsystem::GetBuildingCustomNameByReplicationHelper unresolved -- Base Core renaming will be disabled");

		if (g_replicationHelperCtor && g_getNameByReplicationHelper)
			LOG_INFO("[BetterMap] Base Core custom-name resolution ready");
	}

	inline void RemoveHooks()
	{
		if (g_createBuildingMarkerHook)
		{
			LOG_DEBUG("[BetterMap] Removing CreateBuildingMarker hook");
			GetHooks()->Hooks->Remove(g_createBuildingMarkerHook);
			g_createBuildingMarkerHook = nullptr;
			g_originalCreateBuildingMarker = nullptr;
			LOG_INFO("[BetterMap] CreateBuildingMarker hook removed");
		}

		g_replicationHelperCtor = nullptr;
		g_getNameByReplicationHelper = nullptr;
	}
}
