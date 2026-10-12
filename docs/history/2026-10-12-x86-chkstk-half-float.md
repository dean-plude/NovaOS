## The stack probe and half-float helpers in 32-bit ntdll

A leftover of the Direct3D 12 game (After 0.1, step 24.4).  The game's
fix gave the 64-bit ntdll `__chkstk` and the half-precision conversions
that LLVM's JIT (Mesa's lavapipe and llvmpipe) links by name; the 32-bit
ntdll had neither, so 32-bit Mesa's JIT, and MSVC-built 32-bit programs
that import the probe, found nothing.

- **ntdll `_chkstk` (x86)**: Windows' 32-bit name for the probe.  It
  touches each page of the EAX bytes about to be taken from the top down
  and returns with ESP lowered by EAX, keeping ECX, as MSVC's x86 runtime
  does (the x64 one leaves RSP to its caller).
- **`__extendhfsf2`, `__extendhfdf2`, `__truncsfhf2`, `__truncdfhf2`
  (x86)**: the same conversions as the 64-bit ones, in LLVM's 32-bit
  convention (a half argument on the stack, a half result in XMM0, a float
  or double result in ST(0)).
- **`chkstktest`** (core suite, 64-bit and 32-bit): nothing tested the
  64-bit helpers before either.  The probe runs on a thread of its own with a 1 MB
  stack and writes the lowest byte it asked for; the
  conversions are checked on normals, subnormals, infinities, NaN, ties to
  even and a double that must round once.
