# Sky's Shadow

A separate Shadow of War plugin. It draws its own menu and does not replace `ShadowOfWarMod.dll` or `SoW_ModMenu.dll`.

## Open it

The Release build copies `shadow-gear-menu.dll` to:

`E:\SteamLibrary\steamapps\common\ShadowOfWar\x64\plugins`

Start the game, then press **Numpad 1** (the 1 on the number pad). The menu stays open. Press Numpad 1 again, Esc, or the X to close it.

Press **Numpad 1** or the top-row **1** key. The menu opens over the game and leaves the game running.

Press **Home** to recheck the switches already in the menu. The result shows on the bottom line. Notes are also written to `shadow-gear-menu.log` beside the DLL.

The **Debug** page searches for a number you can see, such as XP. Type it, press **First scan**, change that number in the game, type the new number, then press **Next scan**. **Write number** unlocks when 8 or fewer copies remain.

If nothing appears, look for `shadow-gear-menu.log` in that same plugins folder. No log means the loader did not start this DLL.

Build from this folder:

```
cmake -S . -B build -A x64
cmake --build build --config Release
```

`build\Release\shadow-menu-preview.exe` opens the same window on the desktop. Toggles there do not change the game.

## What the rows do

| Row | State | How it works |
|---|---|---|
| God mode | Ready to test again | Published health update. The player check now uses the +0x90 flag. The first in-game test still took damage. |
| Infinite arrows, might, focus, wrath | Ready to test in game | Four published on/off flags. Found once in this install. |
| Mission timers | Ready to test in game | Published timer check, on the World page. Found once in this install. |
| Complete item challenges | Ready if the signature is present | Published challenge check. Finishes weapon challenges. Does not change level, rarity, or gems. |
| Damage, fly mode, XP, skill points, cooldowns | Unavailable | No published interface. |
| Gear level, rarity, gems | Unavailable | Home reports whether the published checks were found. It does not read worn gear. |
| Captain editor, chosen spawn, intel, recruitment | Unavailable | No published interface is wired in. |
| Time of day | Unavailable | No published interface. |

Settings are saved as `shadow-gear-menu.ini` next to the DLL. Size, accent, the menu key, feature switches, and per-feature hotkeys are in that file. Click **Unbound** on a row, then press a key. Esc cancels. Backspace clears.

## Checked on 24 Oct 2026

- Release DLL and desktop preview both compile.
- Preview: Player, Equipment, and Settings pages render, God mode's switch turns on, and the layout check reports no overlap.
- God mode, the resource flags, mission timers, and the item-challenge check each occur once in `ShadowOfWar.exe`. Home repeats that check on the loaded game.
- In-game combat was not run, so health, arrows, might, focus, wrath, timers, and item challenges are not confirmed there yet.
- `ShadowOfWarMod.dll` and `SoW_ModMenu.dll` were not modified.

## Dependencies

- Windows SDK, GDI+: system libraries.
- Menu behavior follows the public Middle-Earth Mod Loader exports (`OnPluginLoad`, `OnPluginUnload`, `OnLoad`) so either loader can start it. Those ReaperAnon repositories have no license file.
- The three signatures and the health-flag behavior come from the public Cheat Hotkeys source in [Shadow-Of-War-Mods](https://github.com/ReaperAnon/Shadow-Of-War-Mods). That repository also has no license file. This project does not copy its source files.
- Dear ImGui (MIT), MinHook (BSD-2), and SimpleIni (MIT) are used by those public mods. This menu does not ship them.

## Limits

- Another plugin that already changed the same health code or resource flags is left alone.
- The resource flags are the same ones Cheat Hotkeys uses. Running both can disagree.
- Gear, XP, skill points, movement, captains, and time of day stay unavailable until a published interface exists.
- Middle-Earth Mod Loader is not installed here, and this build does not rename `bink2w64.dll`.
