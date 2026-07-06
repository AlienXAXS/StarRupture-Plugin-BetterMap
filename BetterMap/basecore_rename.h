#pragma once

#include "plugin_helpers.h"
#include "Engine_classes.hpp"
#include "ChimeraUI_classes.hpp"

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
namespace BetterMap::BaseCoreRename
{
	inline constexpr const char* CreateBuildingMarkerPattern =
		"40 53 55 56 57 48 81 EC ?? ?? ?? ?? 48 8B FA 48 8B E9 E8 ?? ?? ?? ?? 48 8B D8";

	using CreateBuildingMarker_t = bool(__fastcall*)(SDK::UCrUW_MapMenuMapArea* self, const SDK::FBuildingMarkerDataFastArrayItem* Data);

	inline CreateBuildingMarker_t g_originalCreateBuildingMarker = nullptr;
	inline HookHandle             g_createBuildingMarkerHook     = nullptr;

	// Finds the ACrBaseCore actor closest to a map marker's 2D location.
	// Same location-matching approach used by the Compass plugin, since
	// FBuildingMarkerDataFastArrayItem only carries a FMassEntityHandle,
	// not a direct actor pointer.
	inline SDK::AActor* FindBaseCoreNear(SDK::UWorld* world, const SDK::FVector2f& loc, float maxDistance = 2000.0f)
	{
		if (!world)
		{
			LOG_DEBUG("[BetterMap] FindBaseCoreNear: no world, skipping actor scan");
			return nullptr;
		}

		SDK::TArray<SDK::AActor*> actors;
		SDK::UGameplayStatics::GetAllActorsOfClass(world, SDK::ACrBaseCore::StaticClass(), &actors);
		LOG_TRACE("[BetterMap] FindBaseCoreNear: scanning %d ACrBaseCore actor(s) near (%.0f, %.0f)", actors.Num(), loc.X, loc.Y);

		SDK::AActor* best = nullptr;
		float bestDistSq = maxDistance * maxDistance;

		for (int i = 0; i < actors.Num(); i++)
		{
			SDK::AActor* actor = actors[i];
			if (!actor)
				continue;

			SDK::FVector pos = actor->K2_GetActorLocation();
			float dx = pos.X - loc.X;
			float dy = pos.Y - loc.Y;
			float distSq = dx * dx + dy * dy;

			LOG_TRACE("[BetterMap] FindBaseCoreNear: actor[%d]=%p at (%.0f, %.0f) distSq=%.0f", i, static_cast<void*>(actor), pos.X, pos.Y, distSq);

			if (distSq < bestDistSq)
			{
				bestDistSq = distSq;
				best = actor;
			}
		}

		if (best)
			LOG_DEBUG("[BetterMap] FindBaseCoreNear: matched actor=%p (distSq=%.0f)", static_cast<void*>(best), bestDistSq);
		else
			LOG_DEBUG("[BetterMap] FindBaseCoreNear: no actor within range");

		return best;
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
					auto* nameSys = static_cast<SDK::UCrBuildingCustomNameSubsystem*>(
						SDK::USubsystemBlueprintLibrary::GetWorldSubsystem(world, SDK::UCrBuildingCustomNameSubsystem::StaticClass()));
					LOG_TRACE("[BetterMap] CreateBuildingMarkerDetour: nameSys=%p", static_cast<void*>(nameSys));

					SDK::AActor* coreActor = FindBaseCoreNear(world, Data->Location);

					if (!nameSys || !coreActor)
					{
						LOG_DEBUG("[BetterMap] CreateBuildingMarkerDetour: nameSys or coreActor missing (nameSys=%p, coreActor=%p) -- skipping rename",
							static_cast<void*>(nameSys), static_cast<void*>(coreActor));
					}
					else
					{
						SDK::FString customName = nameSys->GetBuildingCustomName(coreActor);
						LOG_DEBUG("[BetterMap] CreateBuildingMarkerDetour: custom name length=%d", customName.Num());

						if (customName.Num() > 0)
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

							LOG_INFO("[BetterMap] Renamed Base Core map marker to custom name (length=%d)", customName.Num());
						}
						else
						{
							LOG_DEBUG("[BetterMap] CreateBuildingMarkerDetour: no custom name set for this Base Core, leaving default label");
						}
					}
				}
			}
		}

		return g_originalCreateBuildingMarker(self, Data);
	}

	inline void InstallHooks()
	{
		LOG_DEBUG("[BetterMap] Scanning for UCrUW_MapMenuMapArea::CreateBuildingMarker...");

		uintptr_t addr = GetScanner()->FindPatternInMainModule(CreateBuildingMarkerPattern);
		if (!addr)
		{
			LOG_ERROR("[BetterMap] Failed to find UCrUW_MapMenuMapArea::CreateBuildingMarker pattern");
			return;
		}
		LOG_DEBUG("[BetterMap] CreateBuildingMarker pattern matched at 0x%llX", addr);

		g_createBuildingMarkerHook = GetHooks()->Hooks->Install(addr, (void*)&CreateBuildingMarkerDetour, (void**)&g_originalCreateBuildingMarker);
		if (!g_createBuildingMarkerHook)
		{
			LOG_ERROR("[BetterMap] Failed to install CreateBuildingMarker hook at 0x%llX", addr);
			return;
		}

		LOG_INFO("[BetterMap] CreateBuildingMarker hook installed at 0x%llX", addr);
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
	}
}
