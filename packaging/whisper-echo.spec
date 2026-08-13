Name:           whisper-echo
Version:        0.1.0
Release:        1%{?dist}
Summary:        Real-time streaming speech-to-text with Whisper and VAD
License:        MIT
URL:            https://github.com/bnielsen1965/whisper-echo
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.16
BuildRequires:  gcc-c++
BuildRequires:  sdl2-devel
BuildRequires:  git

Requires:       sdl2 >= 2.0.0

%description
whisper-echo captures live audio, runs VAD and Whisper transcription in real time,
with optional uinput typing and voice commands.

%prep
%autosetup -n %{name}-%{version}

%build
cmake -B build -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release .
cmake --build build

%install
cmake --install build --prefix %{buildroot}%{_prefix}

%files
%license vendor/whisper.cpp/LICENSE
%doc README.md
%{_bindir}/whisper-echo
%{_datadir}/whisper-echo/command.json
%{_datadir}/whisper-echo/setup_uinput.sh
%{_datadir}/licenses/whisper-echo/whisper.cpp-LICENSE

%changelog
* Thu Aug 12 2026 Bryan Nielsen <bnielsen1965@gmail.com> - 0.1.0-1
- Initial RPM packaging
