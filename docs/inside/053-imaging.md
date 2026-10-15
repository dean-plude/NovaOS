- **Images**: the Windows Imaging Component (`windowscodecs.dll`) decodes
  PNG, JPEG, BMP, GIF (every frame) and ICO with the bundled stb_image,
  converts between its ~50 pixel formats, scales, clips, flips, rotates,
  builds palettes and bitmaps you can lock, and lists its codecs and
  formats, the way WPF and other programs use it.  Encoders and metadata
  readers are not there yet (`tools/wictest`).
