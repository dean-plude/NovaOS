## Windows Calculator's launch, twenty-third slice: the Pivot

The twenty-third part of item 3 of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)),
step 32.6 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)).  Calculator's main page stopped on
its `Pivot`, a class of `Microsoft.UI.Xaml.Phone.dll`.  That library is
open source after all: it is WinUI's phone library (MIT), with its
generated type metadata in the same tree.  NovaOS now vendors it and
builds the `Pivot`, `PivotItem`, their header and panel parts, the
slide-in effect, the Pivot's state machine and its automation peers into
the framework, beside the navigation transitions it already had.  The
library's type tables (on Windows, resources in the DLL) are built in as
data, so the framework's `XamlLibMetadataProvider` reads them and the
parser finds a `Pivot` named in markup as it does on Windows.  NovaOS's
WRL now deletes an object only in `Release`, as WRL does, and ole32 gained
`RoClearError`.

- **Tests:** `wuxamltest` gets the `Pivot`'s and `PivotItem`'s statics and
  their `SelectedIndex` and `Header` dependency properties, and loads
  markup naming a `Pivot`, which the parser resolves through the phone
  library's type tables.
- **Found:** making the `Pivot` stops at its base class, the framework's
  `ItemsControl` (with its item collection, container generator, items
  presenter and `Selector`), which NovaOS does not build yet.  That is the
  next part.  The page does not draw yet.
