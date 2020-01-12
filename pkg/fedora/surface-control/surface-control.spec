Name:       surface-control
Version:    0.2.5
Release:    1%{?dist}
Summary:    Control various aspects of Microsoft Surface devices from the shell

License:    MIT
URL:        https://github.com/linux-surface/surface-control
Source:     %{url}/archive/v%{version}.zip

Requires:       dbus libgcc
BuildRequires:  rust cargo

%global debug_package %{nil}

%description
Control various aspects of Microsoft Surface devices on Linux from the shell.
Aims to provide a unified front-end to the various sysfs-attributes and special
devices.

%prep
%autosetup -n %{name}-%{version}

%build
export CARGO_TARGET_DIR="$PWD/target"
export CARGO_INCREMENTAL=0

cargo build --release --locked
strip --strip-all "target/release/surface"

%install
rm -rf %{buildroot}
install -D -m755 "target/release/surface" "%{buildroot}/usr/bin/surface"
install -D -m644 "target/surface.bash" "%{buildroot}/usr/share/bash-completion/completions/surface"
install -D -m644 "target/_surface" "%{buildroot}/usr/share/zsh/site-functions/_surface"
install -D -m644 "target/surface.fish" "%{buildroot}/usr/share/fish/completions/surface.fish"

%files
%license LICENSE
/usr/bin/surface
/usr/share/bash-completion/completions/surface
/usr/share/zsh/site-functions/_surface
/usr/share/fish/completions/surface.fish

%changelog
* Sun Dec 01 2019 Dorian Stoll <dorian.stoll@tmsp.io>
- Update to version 0.2.5

* Fri Sep 27 2019 Dorian Stoll <dorian.stoll@tmsp.io>
- Update packaging

* Sat Sep 14 2019 Dorian Stoll <dorian.stoll@tmsp.io>
- Update to 0.2.4

* Fri May 17 2019 Dorian Stoll <dorian.stoll@tmsp.io>
- Initial version
