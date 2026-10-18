// NovaOS: WinUIEdit.h, the Windows App SDK's (closed) header for its own
// copy of the windowless RichEdit (ITextServices2, ITextHost2 and the Text
// Object Model) that XAML's text boxes host.  Here, what the XAML core's
// headers need to compile: the Windows SDK's RichEdit, text services and
// TOM headers (MinGW-w64's) and the members those lack.  The text boxes
// themselves come later (roadmap step 32.4).
#pragma once
#include <richedit.h>
#include <richole.h>
#include <textserv.h>
#include <tom.h>
