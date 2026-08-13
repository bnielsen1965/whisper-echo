Name:           whisper-echo
Version:        0.1.0
Release:        1%{?dist}
Summary:        Real-time streaming speech-to-text with Whisper and VAD
License:        MIT
URL:            https://github.com/bnielsen1965/whisper-echo
Source0:        %{name}-%{version}.tar.gz
Source1:        whisper.cpp-%{version}.tar.gz
BuildRequires:  cmake >= 3.16, gcc-c++, make, SDL2-devel, vulkan-devel
Requires:       sdl2 >= 2.0.0
BuildArch:      x86_64

%description
Real-time streaming speech-to-text with Whisper and VAD.

%prep
%autosetup -n %{name}-%{version}
# Ensure vendor/whisper.cpp is present. If Source1 is used, extract it.
# For git builds with submodules, vendor/whisper.cpp should already be present.
# If building from a release tarball without submodules, uncomment:
# %setup -c -T
# tar -xzf %{SOURCE1} -C vendor/

%build
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=%{_prefix} \
  -DCMAKE_INSTALL_LIBDIR=%{_lib}
cmake --build build -j%{_smp_ncpus}

%install
rm -rf %{buildroot}
cmake --install build --prefix %{buildroot}%{_prefix}

%files
%license vendor/whisper.cpp/LICENSE
%doc docs/uinput.md
%{_bindir}/whisper-echo
%{_datadir}/whisper-echo/
%{_mandir}/man1/whisper-echo.1.gz

%files devel
%{_includedir}/
%{_libdir}/libggml*.so
%{_libdir}/libwhisper*.so
%{_libdir}/libparakeet*.so
%{_libdir}/pkgconfig/*.pc
%{_libdir}/cmake/

%changelog
* Tue Aug 12 2026 Bryan Nielsen <bnielsen1965@gmail.com> - 0.1.0-1
- Initial RPM spec with devel split and bundled whisper.cpp
