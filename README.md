# BetterMap — StarRupture Client Plugin

> **Deprecated.** StarRupture now covers what this plugin did, so it is no longer maintained and does not load on the current game build. The source stays here for reference.

Alters how things are displayed on the in-game map for the [StarRupture](https://store.steampowered.com/app/1631270/StarRupture/) [ModLoader](https://github.com/AlienXAXS/StarRupture-ModLoader), by hooking the game's own map UI functions rather than patching assets.

**Target:** Game client only

---

## What It Does

| Feature | Description |
|---|---|
| Base Core custom name | The map marker for a base's Core building always uses the hard-coded, replicated label instead of the (possibly localized) name from the game's own custom-name subsystem. BetterMap resolves and displays that name instead. |

The rename is applied whenever a Base Core marker is (re)created — which includes live renames, since the game deletes and recreates a base's marker whenever its custom name changes, not just on map open.

---

## Configuration

Config is stored in `Plugins\config\BetterMap.ini` and is generated on first launch.

| Setting | Description |
|---|---|
| `Enabled` | `0` or `1` — enables the plugin |

---

## Installation

1. Download the latest release ZIP from the [Releases](../../releases) page.
2. Extract into your game's `Binaries\Win64\` folder. The ZIP contains a `Plugins\` folder — it will sit alongside your existing `dwmapi.dll`.
3. After the first launch, edit `Plugins\config\BetterMap.ini` and set `Enabled=1`.

> **Requires [StarRupture-ModLoader](https://github.com/AlienXAXS/StarRupture-ModLoader)** to be installed first.

---

## Troubleshooting

| Problem | Solution |
|---|---|
| Base Core still shows the default name | Confirm `Enabled=1` is set in `Plugins\config\BetterMap.ini`. Also check `modloader.log` for `[BetterMap]` trace/debug lines showing whether the custom-name lookup or marker match failed. |
| Plugin not loading | Check `modloader.log` in `Binaries\Win64\` for errors. |

---

## Building from Source

Requires Visual Studio 2022 (C++20) and the following sibling checkouts alongside this repo:

- [StarRupture-Plugin-SDK](https://github.com/AlienXAXS/StarRupture-Plugin-SDK) (plugin API headers)
- A local `StarRupture-Game-SDK` checkout (Dumper-7 generated UE5 SDK headers, Client + Server)

Clone the repo, open `StarRupture-Plugin-BetterMap.sln`, and build the `Client Release|x64` configuration. The output DLL is placed in `build\Client Release\Plugins\`.

See [PluginDevelopment.md](PluginDevelopment.md) for the full plugin API reference, hook list, config system, and pattern scanner.

---

## Disclaimer

Use at your own risk. The authors are not responsible for any damage caused by using this software.
