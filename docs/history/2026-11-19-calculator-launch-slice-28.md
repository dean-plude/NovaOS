## Windows Calculator's launch, twenty-eighth slice: gradients

The twenty-eighth part of item 3 of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)),
step 32.6 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)).  Calculator's page paints with
`LinearGradientBrush`es, and NovaOS's XAML framework did not build them,
so the page load stopped there.  The framework's `LinearGradientBrush`,
its `GradientStop`s and their collection are now built from Microsoft's
sources (microsoft-ui-xaml, MIT), and NovaOS's compositor (`dcomp.dll`)
draws them: it now makes the composition linear gradient brush and colour
stops the framework draws a gradient with (start and end points, relative
or absolute, the brush's transform, and clamped, wrapped or mirrored
beyond its ends).

- **Tests:** `wuxamltest` loads a `LinearGradientBrush` with two
  `GradientStop`s from markup and reads its `EndPoint` and its stops back
  through Windows' interfaces; `comptest` draws a red to blue gradient
  across a sprite, then mirrored past half way, and checks the window's
  pixels.
- **Found:** Calculator's page gets past its gradients and stops next at
  an `ItemsStackPanel`, the panel its lists lay their items out in, which
  NovaOS does not build yet.  That is the next part.  The page does not
  draw yet.
