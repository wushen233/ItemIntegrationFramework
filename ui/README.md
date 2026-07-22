# UI source

## FallUI item card patch

`actionscript/fallui-patches/Scripts/Components/ItemCard_Entry.as` is the
replacement item card script containing IIF's injected card types, positioning,
and rendering support. It is an integration source file rather than a
standalone FLA project. Use it with the matching Fallout 4/FallUI menu projects
and original assets.

## Prisma UI editor

`prisma/views` contains the HTML, CSS, and JavaScript for IIF's alternate rule
editor. It runs through
[Prisma UI 2.0](https://github.com/PRISMA-USER-INTERFACE-FRAMEWORK/Prisma2.0).

The default native build currently uses the ImGui editor and excludes
`src/GUIEditor` from compilation. The Prisma implementation remains available
for development and future integration.
