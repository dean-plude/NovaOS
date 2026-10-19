## GPU preemption and timeout recovery

A render miniport's GPU ran each context's DMA buffers in order, but one
buffer that never finished held the engine for everyone, and a GPU fault
was only logged: the buffer was dropped and its context carried on as if
nothing had happened.

Now, as the fifth part of step 3 of the Intel GPU design
([design/intel-gpu.md](design/intel-gpu.md)), the scheduler watches each
engine's clock.  A buffer that has run 200 ms while another context's work
waits (or 2 s with none waiting) is preempted through the driver
(`DxgkDdiPreemptCommand`, when its `PreemptionCaps` allow it); when the
driver's interrupt reports `DMA_PREEMPTED`, the buffers it did not finish
go back to their contexts' queues and the preempted context waits its turn
behind the others.  An engine that does not stop within Windows' timeout
of 2 s, or that reports a fault, is reset (`DxgkDdiResetEngine`, or
`DxgkDdiResetFromTimeout` and `RestartFromTimeout` for a driver without
per-engine reset): the context whose buffer hung or faulted is lost, its
later calls fail with `STATUS_DEVICE_REMOVED` and the fences it would
have signalled are raised to the maximum so nothing waits on it forever,
while the other contexts' unfinished buffers are submitted again.
Destroying a context whose buffer is still running preempts it instead of
waiting.

The test driver `wddmtest.sys` can now be preempted between its commands
(a preempted buffer resumes where it stopped), reset per engine, and has a
`SPIN` command that runs until it is preempted or until the engine is
reset.  `kmttest wddm` uses it to show a spinning context preempted for
another one, and an unstoppable spin reset with its context lost while the
other context keeps running.
