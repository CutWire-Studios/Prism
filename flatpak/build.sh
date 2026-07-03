#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MANIFEST="${ROOT}/flatpak/org.cutwire.Prism.yml"
BUILD_DIR="${ROOT}/flatpak/build-dir"
REPO_DIR="${ROOT}/flatpak/repo"
APP_ID="org.cutwire.Prism"

usage() {
    cat <<EOF
Usage: $(basename "$0") [build|run|install|export|clean]

  build    Build the Flatpak (default)
  run      Build and run the Flatpak locally
  install  Build and install the Flatpak locally
  export   Build and export a .flatpak bundle
  clean    Remove local Flatpak build artifacts
EOF
}

ensure_tools() {
    command -v flatpak-builder >/dev/null || {
        echo "flatpak-builder is required" >&2
        exit 1
    }
}

build() {
    ensure_tools
    flatpak-builder \
        --user \
        --force-clean \
        --install-deps-from=flathub \
        --repo="${REPO_DIR}" \
        "${BUILD_DIR}" \
        "${MANIFEST}"
}

run_app() {
    build
    flatpak --user remote-add --if-not-exists prism-local "file://${REPO_DIR}"
    flatpak --user install --or-update prism-local "${APP_ID}" -y
    flatpak run "${APP_ID}" "$@"
}

install_app() {
    build
    flatpak --user remote-add --if-not-exists prism-local "file://${REPO_DIR}"
    flatpak --user install --or-update prism-local "${APP_ID}" -y
}

export_bundle() {
    build
    mkdir -p "${ROOT}/flatpak/dist"
    flatpak build-bundle "${REPO_DIR}" "${ROOT}/flatpak/dist/${APP_ID}.flatpak" "${APP_ID}"
    echo "Bundle written to ${ROOT}/flatpak/dist/${APP_ID}.flatpak"
}

clean() {
    rm -rf "${BUILD_DIR}" "${REPO_DIR}" "${ROOT}/flatpak/dist"
}

cmd="${1:-build}"
shift || true

case "${cmd}" in
    build) build ;;
    run) run_app "$@" ;;
    install) install_app ;;
    export) export_bundle ;;
    clean) clean ;;
    -h|--help|help) usage ;;
    *)
        echo "Unknown command: ${cmd}" >&2
        usage
        exit 1
        ;;
esac
