#!/usr/bin/bash

kernelrelease="${1}"
suffix="${2:+-${2}}"

distribution="unstable"
debarch="amd64"
debcompat="10"
sourcename="linux-surface${suffix}"
maintainer="surfacebot <surfacebot@users.noreply.github.com>"

image_pkgname="linux-image-surface${suffix}"
image_pkgname_actual="linux-image-${kernelrelease}"

headers_pkgname="linux-headers-surface${suffix}"
headers_pkgname_actual="linux-headers-${kernelrelease}"

recommends=""
if [ ! "$suffix" = "-lts" ]; then
  recommends="iptsd"
fi

mkdir -p "debian/source"
echo "1.0" > "debian/source/format"
echo "${debarch}" > "debian/arch"
echo "${debcompat}" > "debian/compat"

cat <<EOF > "debian/changelog"
${sourcename} (${kernelrelease}) ${distribution}; urgency=medium

  * Linux kernel for Microsoft Surface devices.

 -- $maintainer  $(date -R)
EOF

cat <<EOF > "debian/control"
Source: ${sourcename}
Section: kernel
Priority: optional
Maintainer: ${maintainer}
Homepage: https://github.com/linux-surface/linux-surface

Package: ${image_pkgname}
Architecture: ${debarch}
Depends: ${image_pkgname_actual} (= ${kernelrelease})
Recommends: ${recommends}
Description:
  Meta-package for linux-surface kernel images.

Package: ${headers_pkgname}
Architecture: ${debarch}
Depends: ${headers_pkgname_actual} (= ${kernelrelease})
Description:
  Meta-package for linux-surface headers.
EOF

cat <<EOF > "debian/rules"
#!/usr/bin/make -f
export DH_VERBOSE = 1

# The DPKG in Ubuntu 22.04 defaults to using ZSTD, which is not yet supported
# by the DPKG in Debian 11
override_dh_builddeb:
	dh_builddeb -- -Zxz

%:
	dh \$@
EOF
chmod +x "debian/rules"
