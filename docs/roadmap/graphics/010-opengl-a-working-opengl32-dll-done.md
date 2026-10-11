- ~~**OpenGL**: a working `opengl32.dll`~~ Done: Mesa's llvmpipe, built
  in since [Mesa's OpenGL built in](HISTORY.md#mesas-opengl-built-in), is
  the system OpenGL (OpenGL 4.5, 64- and 32-bit); see
  [OpenGL](HISTORY.md#opengl-mesa-as-the-system-opengl32dll).  NovaOS's
  own `opengl32.dll` hands it to Mesa's virgl on a 3D virtio-gpu (see
  [virgl](HISTORY.md#opengl-on-the-hosts-gpu-virgl)).
