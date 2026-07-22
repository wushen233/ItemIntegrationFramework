# Third-Party Notices

## FallUI compatibility source

`ui/actionscript/fallui-patches` contains an ActionScript item card file
modified for ItemIntegrationFramework interoperability. IIF additions are
released as source, but underlying Fallout 4 and FallUI portions remain the
property of their respective authors and are not relicensed by the repository
MIT License. No FallUI fonts, icons, FLA projects, or compiled SWFs are
included.

## Prisma UI 2.0

`ui/prisma` contains IIF-authored editor pages that use the Prisma UI F4
bridge. The Prisma UI framework is not vendored. Obtain it from the
[official repository](https://github.com/PRISMA-USER-INTERFACE-FRAMEWORK/Prisma2.0),
where it is distributed under its own `LICENSE.md` terms.

`src/PrismaUI_F4_API.h` and `src/GUIEditor/PrismaUI_F4_API.h` are public modder
API headers distributed for copying into consumer projects.

## Xbyak

The `src/xbyak` directory contains Xbyak by MITSUNARI Shigeo. Xbyak is
distributed under the 3-Clause BSD License. Its original copyright and license
text are retained in `src/xbyak/COPYRIGHT`.

## Build dependencies

The following projects are obtained separately by the build system and are not
vendored in this repository:

- CommonLibF4 by Dear Modding, MIT License.
- Dear ImGui, MIT License.
- Microsoft Detours, MIT License.
- MinHook, 2-Clause BSD License.
- SimpleIni, MIT License.
- JSON for Modern C++, MIT License.
- TinyXML-2, zlib License.

Refer to each dependency's source distribution for its complete license text.
