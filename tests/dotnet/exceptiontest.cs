// exceptiontest: .NET exceptions on NovaOS.  A caught exception with its
// stack trace, then (with "unhandled") one nobody catches, which .NET
// reports as "Unhandled exception. System.InvalidOperationException: ..."
// with its stack trace before the process ends with 0xE0434352.
using System;
using System.Runtime.CompilerServices;

static class Program
{
    [MethodImpl(MethodImplOptions.NoInlining)]
    static void Thrower(string what) => throw new InvalidOperationException(what);

    static int Main(string[] args)
    {
        try {
            Thrower("caught one");
        } catch (InvalidOperationException e) {
            Console.WriteLine("caught: " + e.Message);
            Console.WriteLine("trace has Thrower: " + (e.StackTrace != null && e.StackTrace.Contains("Thrower")));
        }
        if (args.Length > 0 && args[0] == "unhandled") {
            Console.WriteLine("throwing an unhandled exception");
            Console.Out.Flush();
            Thrower("exceptiontest unhandled");
        }
        Console.WriteLine("exceptiontest: ok");
        return 0;
    }
}
