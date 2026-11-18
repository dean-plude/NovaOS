- ~~.NET: an unhandled managed exception prints "Stack overflow." instead of
  the exception and its stack trace (with NLS as well as ICU).~~ Done
  (`stacktest`; the app corpus's .NET entry): thread stacks had no guard
  page, which the .NET runtime looks for before it prints the exception.
