#!/usr/bin/env bash
#
# build-rpm.sh — build a WasabiQT RPM on Fedora / RHEL / openSUSE.
#
# Produces ./packaging/rpm/RPMS/<arch>/wasabiqt-<ver>-<rel>.rpm
# and a -devel sub-package alongside.
#

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
RPMBUILD_DIR="${REPO_ROOT}/packaging/rpm"
SPEC="${RPMBUILD_DIR}/wasabiqt.spec"

VERSION=$(grep '^Version:' "${SPEC}" | awk '{print $2}')
TARBALL="wasabiqt-${VERSION}.tar.gz"

# Tooling
need() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "build-rpm: missing '$1'.  Install:  sudo dnf install -y $2" >&2
        exit 1
    }
}
need rpmbuild rpm-build

# Set up rpmbuild tree under ./packaging/rpm/.
for d in BUILD BUILDROOT RPMS SOURCES SPECS SRPMS; do
    mkdir -p "${RPMBUILD_DIR}/${d}"
done

# Pack the WasabiQT repo (NOT the wasabi-src/) into the tarball.
# git ls-files ensures we never accidentally include build artefacts
# or the user's wasabi-src/ if they fetched it locally.
echo "build-rpm: packing source tarball"
( cd "${REPO_ROOT}" && git archive --format=tar.gz \
    --prefix="wasabiqt-${VERSION}/" \
    -o "${RPMBUILD_DIR}/SOURCES/${TARBALL}" \
    HEAD )

# Copy spec to SPECS/.
cp "${SPEC}" "${RPMBUILD_DIR}/SPECS/"

# Build.
echo "build-rpm: rpmbuild -ba"
rpmbuild -ba \
    --define "_topdir ${RPMBUILD_DIR}" \
    --define "_sourcedir ${RPMBUILD_DIR}/SOURCES" \
    "${RPMBUILD_DIR}/SPECS/wasabiqt.spec"

echo
echo "build-rpm: ✓ done"
ls -la "${RPMBUILD_DIR}/RPMS"/*/*.rpm 2>/dev/null || true
ls -la "${RPMBUILD_DIR}/SRPMS"/*.rpm 2>/dev/null || true
