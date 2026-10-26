## Windows.UI.Composition, and CoreWindow's pointer events

Second step of `Windows.UI.Xaml` on NovaOS (Phase 32,
[design](design/windows-ui-xaml.md)): the compositor XAML draws with.

- **`Windows.UI.Composition`** in `dcomp.dll`, as on Windows (the
  registry's `ActivatableClassId` key for
  `Windows.UI.Composition.Compositor` names it).  The `Compositor` makes
  container and sprite visuals (offset, size, anchor and centre points,
  scale, rotation, the transform matrix's 2-D part, opacity, visibility,
  inset clips, and the children collection with `InsertAtTop`,
  `InsertAtBottom`, `InsertAbove`, `InsertBelow`, `Remove` and
  `RemoveAll`), colour brushes and surface brushes (stretch `None`,
  `Fill`, `Uniform`, `UniformToFill`, alignment ratios), and targets on a
  window (`ICompositorDesktopInterop::CreateDesktopWindowTarget`) or on
  the thread's CoreWindow (`CreateTargetForCurrentView`).  Through
  `ICompositorInterop` it makes graphics devices on a Direct3D 11 device,
  whose drawing surfaces programs draw into with `BeginDraw`/`EndDraw`
  (`ICompositionDrawingSurfaceInterop`, reading the pixels back as
  DirectComposition's surfaces do), and surfaces showing a composition
  swap chain.  Changes show without a commit: the compositor's thread
  draws each target's tree in software and copies it into the window.
- **CoreWindow's pointer events.**  `PointerEntered`, `PointerMoved`,
  `PointerPressed`, `PointerReleased`, `PointerWheelChanged`,
  `PointerExited` and `PointerCaptureLost` are raised from the mouse's
  messages, with a `Windows.UI.Input.PointerPoint` (position in DIPs,
  pointer 1, a mouse `PointerDevice`, timestamp, contact) and its
  `PointerPointProperties` (the buttons held, the `PointerUpdateKind`, the
  wheel's turn).

New core self-test `comptest` (64- and 32-bit); `coreapptest` checks the
pointer events and a composition target on its CoreWindow.  Not yet:
animations (accepted, not run), expression animations, property sets,
effects, batches, 3-D transforms, clips other than insets, bilinear
sampling, drawing surfaces on a Direct2D device, and touch and pen
pointers.
