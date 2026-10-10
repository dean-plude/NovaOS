- Win32 console screen buffers: the console keeps each buffer's cells,
  cursor and text attributes, so `GetConsoleScreenBufferInfo` reports the
  cursor, and cell reads and writes, fills, scrolling, colours and
  independent buffers work (`consolecursortest`, x64 and x86); the first
  buffer follows the Terminal's size.  Still to do: verify a real
  console-screen API application, and read the cells from the Terminal's
  own libvterm screen instead of a copy kept by the console; Git still
  uses NovaOS's own `less`.
