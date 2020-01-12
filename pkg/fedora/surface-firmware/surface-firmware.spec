Name:       surface-firmware
Version:    20191004
Release:    1%{?dist}
Summary:    Firmware for Microsoft Surface devices

%global commit a6c6b97da238af28dfb5fea4cd71c69f61d8d24e

License:    proprietary
BuildArch:  noarch
URL:        https://github.com/linux-surface/linux-surface
Source:     %{url}/archive/%{commit}.zip

%description
This package provides firmware files required Microsoft Surface devices.

%prep
%autosetup -n linux-surface-%{commit}

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}/usr/lib
cp -r firmware %{buildroot}/usr/lib

%files
/usr/lib/firmware/intel/ipts

%changelog
* Wed Oct 02 2019 Dorian Stoll <dorian.stoll@tmsp.io>
- Actually fix the HID descriptor on Surface Laptop instead of replacing it

* Wed Oct 02 2019 Dorian Stoll <dorian.stoll@tmsp.io>
- Update firmware to fix touch input for Surface Laptop

* Fri Sep 27 2019 Dorian Stoll <dorian.stoll@tmsp.io>
- Initial version
