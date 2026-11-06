#!/usr/bin/env bash
# Derive the nightly channel's PKGBUILD from the stable one.
#
#   scripts/make-nightly-pkgbuild.sh <build-id> <commit> [output-dir]
#   scripts/make-nightly-pkgbuild.sh 20260922.ac5601e ac5601e... packaging/arch/nightly
#
# Generated rather than committed so a change to the real PKGBUILD — a new dependency, a new
# cmake flag — cannot be forgotten here.
#
# Unlike the AppImage, Flatpak and installers, the two channels are NOT co-installable on Arch:
# both packages own /usr/bin/prism, and pacman will not have that. So the nightly takes a distinct
# package identity and declares the conflict outright, the same way an AUR -git package does. Its
# desktop entry, icon and AppStream id are still the nightly ones (PRISM_CHANNEL=nightly, see
# CMakeLists.txt), so a user can always tell which channel is installed.
set -euo pipefail

BUILD_ID="${1:?usage: make-nightly-pkgbuild.sh <build-id> <commit> [output-dir]}"
COMMIT="${2:?usage: make-nightly-pkgbuild.sh <build-id> <commit> [output-dir]}"
OUT_DIR="${3:-packaging/arch/nightly}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/packaging/arch/PKGBUILD"

BASE_VER="$(sed -n 's/^pkgver=//p' "$SRC")"
# pkgver accepts only alphanumerics, '.', '_' and '+' — no '-' and no ':' — so the build stamp is
# joined with dots and the sha carries the git-describe-style 'g' prefix.
NIGHTLY_VER="${BASE_VER}.${BUILD_ID/./.g}"

case "$OUT_DIR" in /*) DEST="$OUT_DIR" ;; *) DEST="$ROOT/$OUT_DIR" ;; esac
mkdir -p "$DEST"
python3 - "$SRC" "$DEST/PKGBUILD" "$BASE_VER" "$NIGHTLY_VER" "$COMMIT" "$BUILD_ID" <<'PY'
import sys

src, dst, base_version, version, commit, build_id = sys.argv[1:7]
text = open(src, encoding="utf-8").read()


def once(old, new):
    global text
    if old not in text:
        raise SystemExit(f"packaging/arch/PKGBUILD no longer contains: {old!r}")
    text = text.replace(old, new, 1)


once("pkgname=prism\n", "pkgname=prism-nightly\n")
once(f"pkgver={base_version}\n", f"pkgver={version}\n")
once(
    'pkgdesc="Qt6 live media mixer/switcher with FFmpeg, OpenGL and a dark Resolume-inspired UI"',
    'pkgdesc="Qt6 live media mixer/switcher with FFmpeg, OpenGL and a dark Resolume-inspired UI'
    ' (nightly build)"\n'
    "# Both packages own /usr/bin/prism, so they cannot be co-installed; say so rather than\n"
    "# letting pacman fail on a file conflict at install time.\n"
    "conflicts=('prism')\n"
    "provides=(\"prism=$pkgver\")",
)
once('_commit="${_commit:-v$pkgver}"', f'_commit="${{_commit:-{commit}}}"')
once(
    "        -DPRISM_WITH_SEGMENTATION=ON\n",
    "        -DPRISM_WITH_SEGMENTATION=ON \\\n"
    "        -DPRISM_CHANNEL=nightly \\\n"
    f"        -DPRISM_BUILD_ID={build_id}\n",
)

open(dst, "w", encoding="utf-8").write(text)
PY

echo "Wrote $DEST/PKGBUILD (pkgver=$NIGHTLY_VER, commit=$COMMIT)"
