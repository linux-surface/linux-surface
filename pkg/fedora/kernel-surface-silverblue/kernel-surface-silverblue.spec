# Dummy package to make kernel-surface installable on Fedora Silverblue

Name: kernel
Version: 20201215
Release: 1
Summary: The linux-surface kernel for Fedora Silverblue

License: GPLv2

Provides: kernel = %{version}-%{release}
Provides: kernel-core = %{version}-%{release}
Provides: kernel-modules = %{version}-%{release}
Provides: kernel-modules-extra = %{version}-%{release}

Provides: kernel-%{_arch} = %{version}-%{release}
Provides: kernel-core-%{_arch} = %{version}-%{release}
Provides: kernel-modules-%{_arch} = %{version}-%{release}
Provides: kernel-modules-extra-%{_arch} = %{version}-%{release}

Provides: kernel%{_isa} = %{version}-%{release}
Provides: kernel-core%{_isa} = %{version}-%{release}
Provides: kernel-modules%{_isa} = %{version}-%{release}
Provides: kernel-modules-extra%{_isa} = %{version}-%{release}

Requires: kernel-surface

%description
The linux-surface kernel for Fedora Silverblue

%prep

%build

%install

%files
