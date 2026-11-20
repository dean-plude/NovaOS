## Windows Calculator's launch, thirtieth slice: the link buttons

The thirtieth part of item 3 of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)),
step 32.6 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)).  Calculator's page shows links as
`HyperlinkButton`s, and NovaOS's XAML framework did not build them, so
the page load stopped there.  The framework's `HyperlinkButton` is now
built from Microsoft's sources (microsoft-ui-xaml, MIT), with its
automation peer, the launcher a clicked link's `NavigateUri` opens
through (`Windows.System.Launcher`), and the core's `CHyperlink`, which
reads the `HyperlinkUnderlineVisible` theme resource that says whether a
link's text is underlined.  The one NovaOS change: the launcher names
`IAsyncInfo` bare, as the Windows SDK lets it, so NovaOS's
`nova_async_info.h` goes first in it, as for the list's incremental
loading.

- **Tests:** `wuxamltest` loads a `HyperlinkButton` from markup and reads
  its `NavigateUri` back through Windows' interfaces.
- **Found:** Calculator's page gets past its link buttons and stops next
  at the core's text element collection (`CTextElementCollection`, under
  the inline collection of a `Span` or `Hyperlink` in a text block),
  which NovaOS does not build yet.  That is the next part.  The page does
  not draw yet.
