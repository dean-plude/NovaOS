## XAML's core services

Ninth part of step 32.3 of `Windows.UI.Xaml` on NovaOS (Phase 32,
[design](../design/windows-ui-xaml.md)): the XAML core's own core
services, `CCoreServices` (`core/dll/xcpcore.cpp`), now build from
Microsoft's sources, and XAML text loads through them as
`XamlReader.Load` loads it (`CCoreServices::ParseXaml`).

- **The core services:** `xcpcore.cpp` and its name scopes compiled with
  the core's own project (`xcpcore.vcxproj`) into `xaml_core.lib`, for
  programs that say `"xaml": "core"`; one new stand-in for a closed
  header (FrameworkUdk's `CoreWindowIntegrationInterface.h`).
- **The parts not built yet:** every function the built core calls in the
  rest of the core (rendering, input, layout, text, media) gets a
  generated stand-in at each build that says which function it is, once,
  and answers "not implemented"; a real definition always wins, so the
  list shrinks as the core builds.
- `xamlloadtest` runs on a real `CCoreServices` (Microsoft's parser mock
  is gone from it) with the core's theme resources, which a dictionary's
  lookup falls back on: 31 checks.
