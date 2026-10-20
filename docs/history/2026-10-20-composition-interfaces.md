## The composition interfaces XAML's render tree asks for

Part of the Phase 32 push for `Windows.UI.Xaml` on NovaOS
([design](../design/windows-ui-xaml.md)): `dcomp.dll`'s
Windows.UI.Composition grows the later interface versions, animations
and property sets the XAML framework's compositor tree host uses.

- **Both namespaces:** XAML's source asks for the Windows App SDK's
  `Microsoft.UI.Composition` interfaces, which match Windows' member for
  member under other IIDs; every composition object now answers both
  (`ICompositor` and `ICompositionObject`, which differ, get a vtable of
  their own), and `Microsoft.UI.Composition.Compositor` activates the
  same compositor.
- **New interface versions:** `ICompositor2` to `8`, `IVisual2` to `4`
  (relative offset and size adjustments drawn), `ICompositionObject2`
  to `5`.
- **Property sets:** every object's `Properties` and
  `CreatePropertySet`, with each value type's `Insert` and `TryGet`.
- **Animations:** key frame animations of scalars, vectors, colours,
  quaternions and Booleans run on the compositor's thread, with easing
  (linear, cubic Bezier, step, sine, circle, power); animation groups;
  scoped batches whose `Completed` comes on the making thread's
  `DispatcherQueue`; rectangle clips.
- **Not yet:** expression animations are accepted but not evaluated;
  lights, shadows, shapes, gradient brushes, effects and implicit
  animations stay `E_NOTIMPL`.
- Test: `comptest` checks the new interfaces in both namespaces, a
  property set, the relative adjustments and a rectangle clip in the
  pixels, an `Offset` animation in a scoped batch, an animation group
  fading a visual out and the easing functions.
