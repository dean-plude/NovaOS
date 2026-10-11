## Drawing into another process's child window

Steam's "Sign in to Steam" window opened with black contents.  Steam's
browser composer makes an SDL window and puts Chromium's window inside it
as a child window; Chromium, drawing in software, draws its frames from
its GPU process with `GetDC` and `BitBlt` on that child window.  NovaOS
let another process draw only into top-level windows (the ones the
desktop knows), so `GetDC` on the child failed and nothing was drawn.
Now `GetDC`, `GetWindowDC` and `GetDCEx` on another process's child
window ask that process where the child sits in its top-level window and
which part of it can show, and the drawing lands there, clipped to the
child.  The sign-in form (account name, password, Remember me, Sign in)
now draws.  `xpwin` in the core suite draws from a third process into a
child window of a framed window of another process and checks the
pixels inside and outside it.  Steam's connection to its servers still
fails in the test sandbox, which reaches the internet only through an
HTTPS proxy; signing in is a hand check.
