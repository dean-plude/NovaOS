## XAML's value boxer and Windows.Foundation.PropertyValue

Part of the Phase 32 push on step 32.3 of `Windows.UI.Xaml` on NovaOS
([design](../design/windows-ui-xaml.md)): what `XamlReader.Load` hands
its results back through now builds from Microsoft's sources.

- **New DLL:** `wintypes.dll`, `Windows.Foundation.PropertyValue`: the
  Windows Runtime's boxes for numbers, characters, booleans, strings,
  GUIDs, dates, time spans, points, sizes, rectangles and their arrays,
  read through `IPropertyValue` (numeric conversions in range, overflow
  and type mismatches refused) and `IReference<T>` /
  `IReferenceArray<T>` (test `propvaltest`, 64- and 32-bit).
- **Built:** the value boxer `CValueBoxer`, the lifetime library the
  framework's peers are tracked with (`ReferenceTrackerManager`, weak
  reference sources, tracker targets), the framework's process-wide
  `StaticStore` and its release queue, and the core's error service,
  deployment and entry points (`CoreImports`).
- `xamlloadtest` starts the framework on a thread as Microsoft's XAML
  compiler does (`DXamlCore::Initialize` for XBF, with no window) and
  has the value boxer turn the core's parsed `x:String`, numbers and
  booleans into `PropertyValue` boxes and boxes back into the core's
  values.
