Tessera: a minimal 3D model viewer
==================================

Install
-------
Drag Tessera.app onto the Applications shortcut in this window.

First launch
------------
This build is signed ad-hoc rather than with an Apple Developer ID, so macOS
Gatekeeper will refuse it the first time with "tessera cannot be opened because the
developer cannot be verified".

To allow it: right-click (or Control-click) Tessera.app, choose Open, then confirm.
You only need to do this once. Alternatively:

    xattr -dr com.apple.quarantine /Applications/Tessera.app

Using it
--------
Drop a model onto the window, or use File > Open. About 70 formats are
supported, including OBJ, STL, PLY, glTF/GLB, FBX, COLLADA and 3DS.

The full shortcut list is under Help > Keyboard shortcuts. In short: F frames
the model, W toggles wireframe, Tab hides the panels.

Command line
------------
The same binary is a converter and a headless renderer. To use it from a
terminal, link it onto your PATH:

    ln -s /Applications/Tessera.app/Contents/MacOS/Tessera /usr/local/bin/tessera

Then:

    tessera model.glb                          open the viewer
    tessera part.step -o part.stl --ascii      convert between formats
    tessera model.fbx -r thumb.png -s 512x512  render a PNG with no window
    tessera --help                             everything else
