# Releasing

## What goes in a release, and what doesn't

**Attach the `.dmg`. Nothing else.**

| Artefact | Upload it? | Why |
| --- | --- | --- |
| `Tessera-X.Y.Z-macOS-universal.dmg` | **Yes** | The whole product, one file |
| `SHA256SUMS.txt` | **Yes** | Lets people verify the download |
| A `.zip` of the `.dmg` | **No** | See below |
| Source `.zip` / `.tar.gz` | **No** | GitHub generates these from the tag automatically |
| Loose binaries, `.app` folders | **No** | An unbundled `.app` loses its signature when zipped by some tools |

### Why not zip the dmg

Two reasons, and the first is the real one:

1. **A `.dmg` is already a compressed archive.** We build it with `UDZO`
   (zlib-compressed, read-only). Zipping it again gains essentially nothing;
   you are compressing compressed data.
2. **It adds a step for every user.** macOS mounts a `.dmg` on double-click.
   Wrapping it in a zip means unarchive, *then* mount, *then* drag. Three steps
   where there were two.

A zip is the right choice when you are shipping a bare `.app` rather than a
disk image, because a plain folder cannot be downloaded as one file. We ship a
disk image, so it is not.

### One file, not one per architecture

The release build is universal (`arm64` + `x86_64`), so a single `.dmg` covers
every Mac. That is worth the extra build time, because a user who has to guess which
of two downloads matches their machine will sometimes guess wrong.

## Procedure

### 1. Bump the version

In `CMakeLists.txt`:

```cmake
project(tessera VERSION 0.2.0 ...)
```

Commit it on its own so the tag points at something meaningful.

```bash
git commit -am "Release 0.2.0"
```

### 2. Build the installer

```bash
cmake --preset macos-dmg
cmake --build --preset macos-dmg
cd build/macos-dmg && cpack
```

A cold build takes about ten minutes, because Assimp is compiled from source, twice,
once per architecture. Incremental rebuilds are seconds.

### 3. Verify before you publish

Do not skip this. It takes a minute and catches the embarrassing failures.

```bash
DMG=build/macos-dmg/tessera-0.2.0-macOS-universal.dmg
MP=$(hdiutil attach "$DMG" -nobrowse -readonly | sed -n 's|.*\(/Volumes/.*\)|\1|p' | tail -1)

lipo -archs "$MP/Tessera.app/Contents/MacOS/Tessera"        # expect: x86_64 arm64
otool -L "$MP/Tessera.app/Contents/MacOS/Tessera" \
  | grep -v '/usr/lib/\|/System/'                           # expect: nothing
"$MP/Tessera.app/Contents/MacOS/Tessera" --version          # expect: the new version
codesign -dv "$MP/Tessera.app" 2>&1 | grep Signature        # expect: adhoc

hdiutil detach "$MP"
```

The `otool` check is the important one: any Homebrew path in that output means
the build picked up a system library and the `.dmg` will fail on a machine that
does not have it.

### 4. Checksums

```bash
cd build/macos-dmg
shasum -a 256 tessera-0.2.0-macOS-universal.dmg > SHA256SUMS.txt
```

### 5. Tag and publish

```bash
git tag -a v0.2.0 -m "Tessera 0.2.0"
git push origin main --tags

gh release create v0.2.0 \
  build/macos-dmg/tessera-0.2.0-macOS-universal.dmg \
  build/macos-dmg/SHA256SUMS.txt \
  --title "Tessera 0.2.0" \
  --notes-file RELEASE_NOTES.md \
  --prerelease          # drop this once you reach 1.0
```

Tag names get a `v` prefix (`v0.2.0`); the CMake version does not (`0.2.0`).
That is the common convention and worth staying consistent about.

While the version is below 1.0, `--prerelease` is honest signalling: the API,
the CLI flags and the file layout may still move.

## Release notes

Every release must repeat the Gatekeeper instructions. Someone downloading for
the first time hits that wall immediately, and if the notes do not explain it
they will assume the app is broken.

A workable template:

```markdown
## Install

Download the `.dmg`, open it, drag Tessera to Applications.

**First launch:** this build is signed ad-hoc rather than with an Apple
Developer ID, so macOS will refuse it once with "the developer cannot be
verified". Right-click the app → Open → confirm. Once only.

Universal binary. Runs natively on Apple silicon and Intel. Requires macOS 13.3
or later.

## Changes

- ...

## Verify your download

    shasum -a 256 -c SHA256SUMS.txt
```

## Removing the Gatekeeper friction

The warning is a consequence of ad-hoc signing, not something a different
packaging choice can fix. Removing it needs an Apple Developer Program
membership and a **Developer ID Application** certificate, then:

1. Sign with the hardened runtime and a secure timestamp. The current ad-hoc
   step in `cmake/Packaging.cmake` uses `--timestamp=none`, which Apple rejects:
   ```bash
   codesign --force --options runtime --timestamp \
     --sign "Developer ID Application: NAME (TEAMID)" Tessera.app
   ```
2. Sign the `.dmg` with the same identity.
3. Notarise it: `xcrun notarytool submit … --keychain-profile <name> --wait`
4. Staple the ticket: `xcrun stapler staple <dmg>`

After that `spctl -a -vv` returns `accepted / source=Notarized Developer ID` and
the app opens with a normal double-click.

Store the notarisation credential in your keychain once, with
`xcrun notarytool store-credentials`, and reference it by profile name. Never
put an Apple ID password or API key in the repository or in a build script.
