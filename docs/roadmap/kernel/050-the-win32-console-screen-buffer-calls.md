- Win32 console screen buffers: the console keeps each buffer's cells,
  cursor and text attributes, so `GetConsoleScreenBufferInfo` reports the
  cursor, and cell reads and writes, fills, scrolling, colours and
  independent buffers work (`consolecursortest`, x64 and x86); the first
  buffer follows the Terminal's size.  libvterm, the Terminal's own VT
  emulator, edits the cells from what programs write, so a full-screen
  program leaves the same cells in a console window and through a pseudo
  console (`ptyscreentest`).  Still to do: verify a real console-screen
  API application; Git still uses NovaOS's own `less`.
