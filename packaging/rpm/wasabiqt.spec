#
# WasabiQT RPM spec.
#
# Builds against ${WASABI_SRC_DIR} which the spec's prep stage
# downloads via scripts/fetch-wasabi.sh — the source is NOT in the
# WasabiQT source tarball; it's fetched fresh on every build.
#
# Build with:  ./packaging/rpm/build-rpm.sh
#

Name:           wasabiqt
Version:        0.0.1
Release:        1%{?dist}
Summary:        Qt-native Wasabi/Maki skin engine

License:        MIT
URL:            https://github.com/kleberbaum/WasabiQT
Source0:        wasabiqt-%{version}.tar.gz

BuildRequires:  cmake >= 3.21
BuildRequires:  gcc-c++
BuildRequires:  qt6-qtbase-devel >= 6.6
BuildRequires:  qt6-qtmultimedia-devel
BuildRequires:  curl
BuildRequires:  p7zip
BuildRequires:  ninja-build

Requires:       qt6-qtbase >= 6.6
Requires:       qt6-qtmultimedia

%description
WasabiQT is a Qt6-native skin engine inspired by Winamp's Wasabi 1
and Wasabi 2 frameworks.  It loads classic Modern-skin .wal files,
runs their Maki bytecode, and renders through QPainter — making the
classic Winamp Modern skin ecosystem available to any Qt media
player (Audacious, WACUP, custom hosts).

WasabiQT itself contains no Winamp-licensed source code.  Building
this package downloads the user-supplied Wasabi source tree from
the public archive.org mirror at build time.  The downloaded tree
is consumed by the build but never packaged.

%prep
%setup -q

# Download user-supplied Wasabi source from archive.org
./scripts/fetch-wasabi.sh

%build
%cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DWASABIQT_BUILD_TESTS=OFF \
    -DWASABIQT_BUILD_EXAMPLES=ON

%cmake_build

%install
%cmake_install

%check
# Tests skipped at packaging time; they require runtime skin assets.

%files
%license LICENSE
%doc README.md BUILD.md
%{_libdir}/libwasabiqt.so.*
%{_includedir}/WasabiQt/

%package devel
Summary:        Development files for WasabiQT
Requires:       %{name}%{?_isa} = %{version}-%{release}

%description devel
Headers and CMake config for embedding WasabiQT in Qt media players.

%files devel
%{_libdir}/libwasabiqt.so
%{_includedir}/WasabiQt/

%changelog
* Wed May 07 2026 Florian Kleber <kleber@snek.at> - 0.0.1-1
- Initial bootstrap.  No skin loading yet — adapter scaffolding only.
