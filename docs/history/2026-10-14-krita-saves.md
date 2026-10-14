## Krita draws and saves an image

Roadmap step 27.3: Krita, installed from the App Store, draws and saves
an image.  OpenGL was already built in (Mesa's llvmpipe, OpenGL 4.5), so
Krita's canvas needed nothing installed first; what was left was saving.
It needed no change to NovaOS:

- **From the App Store.**  The nightly corpus test
  (`tests/appcorpus/885-krita.py`) now installs Krita with the Store's
  own button from the Store's download, which the Store unpacks into
  `C:\Programs\Krita`, instead of unpacking the zip itself.  The catalog
  now lists Krita 5.3.4 (it listed 5.2.6), the version the corpus has
  run since Krita first started, and its note no longer sends users to
  install Mesa 3D.
- **Draw and save.**  The test opens a new image with Ctrl+N, drags a
  stroke across the canvas with the default brush (the canvas pixels must
  change), and saves it with Ctrl+S through Krita's Save dialog as
  `C:\Apps\data\drawing.kra`.  7-Zip then lists the saved file, which
  must hold `maindoc.xml`, the layers and `mergedimage.png`.  The screen
  shows the drawing, so the test no longer compares it with a reference
  screenshot (`tests/reference/krita.png` is gone); the stroke and the
  saved file decide.

Still open: exporting to PNG or JPEG and opening a saved `.kra` again are
not checked.
