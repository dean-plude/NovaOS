/*
 * novafw.dll — the appx self-test's framework package's library
 * (tools/appx/mktest.py: NovaOS.TestFramework): a program of a package
 * that depends on the framework finds it by name, as Windows' loader
 * finds a framework's DLLs in the package graph.  Freestanding, as
 * greeter.c is.
 */
typedef unsigned short WCHAR;

static const WCHAR greeting[] = u"Hello from a framework package";

__declspec(dllexport) const WCHAR *NovaFrameworkGreeting(void) { return greeting; }
