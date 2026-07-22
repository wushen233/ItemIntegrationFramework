# ItemIntegrationFramework

Shared F4SE/CommonLibF4 framework for centralized item card injection, runtime
hook handling, and integration between Fallout 4 mods.

Mod page and downloads: [Item Integration Framework on Nexus Mods](https://www.nexusmods.com/fallout4/mods/105672)

## Features

- Centralized JSON-driven item information card injection
- Public C++ API for cards, damage modifiers, and armor modifiers
- Shared runtime hooks for dependent plugins
- Type 0, Type 1, and Type 2 card layouts
- Configurable anchors, ordering, replacement, and value mapping
- In-game ImGui rule and layout editor
- Optional Prisma UI editor source
- Multi-runtime Fallout 4 support through CommonLibF4

## Source layout

- `src`: native backend, public API, hooks, and editor implementation.
- `config/ItemIntegrationFramework`: default card rules and translations.
- `docs/examples`: English and Chinese card format examples.
- `ui/actionscript/fallui-patches`: item card ActionScript integration source.
- `ui/prisma`: IIF-authored Prisma UI editor frontend.

Compiled DLL, SWF, PEX, plugin, font, and FallUI media assets are not included.
Install the complete mod package from Nexus Mods for normal gameplay.

## Requirements

- Windows 10 or later
- Visual Studio 2022 Build Tools with Desktop development with C++
- Git
- XMake 3.0 or later
- [Dear-Modding-FO4/commonlibf4](https://github.com/Dear-Modding-FO4/commonlibf4)
- Fallout 4 Script Extender

## Build

This project must be built against the
[Dear-Modding-FO4 CommonLibF4 fork](https://github.com/Dear-Modding-FO4/commonlibf4).
The build script downloads the pinned revision from that fork into `.deps/`
when needed, configures XMake, and builds the plugin:

```powershell
.\scripts\build.ps1
```

To use an existing checkout of the same CommonLibF4 fork:

```powershell
.\scripts\build.ps1 -CommonLibF4Path D:\path\to\commonlibf4
```

The default native build uses the ImGui editor. The alternate Prisma UI editor
implementation under `src/GUIEditor` is retained as source but excluded by the
default XMake target.

## Integration

Dependent F4SE plugins can include `src/IIF_API.h` and register cards or combat
modifiers through the F4SE messaging interface. Default JSON rules are loaded
from:

```text
Data/F4SE/Plugins/ItemIntegrationFramework
```

See `docs/examples` and the configuration files for supported card schemas.

## License

Original IIF source is released under the MIT License. Vendored Xbyak remains
under its BSD-3-Clause license. Third-party compatibility patch bases remain
subject to their respective terms. See `THIRD_PARTY_NOTICES.md`.
