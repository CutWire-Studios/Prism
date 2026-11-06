#!/usr/bin/env python3
"""Derive the nightly channel's Flatpak manifest from the local (working-tree) one.

    scripts/make-nightly-flatpak-manifest.py <build-id> [output]

Generated rather than committed: the real manifest carries the pinned Skia commit, the JUCE
archive and the whole build recipe, and a second copy would go stale the first time one of those
moved.

Only three things differ. The app id, because installing the nightly bundle under
org.cutwire.Prism would replace the user's Flathub install; the icon rename that goes with it; and
PRISM_CHANNEL, which is what makes CMake install the .desktop, icon and AppStream files under the
nightly id. flatpak's build-export drops anything in /app/share not named after the app id, so
without that the bundle would export no launcher entry at all.

Text edits, not a YAML round-trip: PyYAML is not guaranteed on a runner, and rewriting the file
through a parser would strip every comment in it.
"""

import sys

SRC = "flatpak/org.cutwire.Prism.yml"
DEFAULT_OUT = "flatpak/org.cutwire.Prism.Nightly.yml"
APP_ID = "org.cutwire.Prism.Nightly"

# The last flag of the manifest's multi-line `cmake -S . -B build` command; the channel flags are
# appended after it with the same continuation indent.
ANCHOR = "        -DFETCHCONTENT_SOURCE_DIR_LIBDATACHANNEL=/run/build/Prism/vendor/libdatachannel\n"


def main() -> int:
    if not 2 <= len(sys.argv) <= 3:
        print(f"usage: {sys.argv[0]} <build-id> [output]", file=sys.stderr)
        return 2
    build_id = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) == 3 else DEFAULT_OUT

    with open(SRC, encoding="utf-8") as f:
        text = f.read()

    for old in ("app-id: org.cutwire.Prism\n", "rename-icon: org.cutwire.Prism\n"):
        if old not in text:
            print(f"{SRC}: no {old.strip()!r} line to rewrite", file=sys.stderr)
            return 1
        text = text.replace(old, old.replace("org.cutwire.Prism", APP_ID), 1)

    if ANCHOR not in text:
        print(f"{SRC}: cmake flag list no longer ends with {ANCHOR.strip()}", file=sys.stderr)
        return 1
    text = text.replace(
        ANCHOR,
        ANCHOR + "        -DPRISM_CHANNEL=nightly\n" + f"        -DPRISM_BUILD_ID={build_id}\n",
        1,
    )

    with open(out, "w", encoding="utf-8") as f:
        f.write(text)
    print(f"Wrote {out} ({APP_ID}, build {build_id})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
