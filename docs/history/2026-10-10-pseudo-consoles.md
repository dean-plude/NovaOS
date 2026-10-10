## Pseudo consoles

`CreatePseudoConsole`, `ResizePseudoConsole` and `ClosePseudoConsole` work:
a pseudo console is a console of its own in the kernel, sized as asked; a
child created with `PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE` in its
`STARTUPINFOEX` attaches to it instead of a Terminal, and two kernel32
threads carry the host's input pipe to the console's input and the
console's output to the host's output pipe.  The output thread sleeps until
the console has output or is closed (the pseudo console's object is
signaled then), so an idle pseudo console costs no wake-ups.
`ClosePseudoConsole` ends the child's input and stops the output thread
once it has drained what was written.  `pipetest` covers a child's output
and input through a pseudo console and resizing it.
