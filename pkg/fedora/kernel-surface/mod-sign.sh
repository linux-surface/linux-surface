#! /bin/bash

# The modules_sign target checks for corresponding .o files for every .ko that
# is signed. This doesn't work for package builds which re-use the same build
# directory for every flavour, and the .config may change between flavours.
# So instead of using this script to just sign lib/modules/$KernelVer/extra,
# sign all .ko in the buildroot.

# This essentially duplicates the 'modules_sign' Kbuild target and runs the
# same commands for those modules.

MODSECKEY=$1
MODPUBKEY=$2

moddir=$3

modules=`find $moddir -name *.ko`

for mod in $modules
do
    dir=`dirname $mod`
    file=`basename $mod`

    ./scripts/sign-file sha256 ${MODSECKEY} ${MODPUBKEY} ${dir}/${file}
    rm -f ${dir}/${file}.{sig,dig}
done

RANDOMMOD=$(find $moddir -type f -name '*.ko' | sort -R | head -n 1)
if [ "~Module signature appended~" != "$(tail -c 28 $RANDOMMOD)" ]; then
    echo "*****************************"
    echo "*** Modules are unsigned! ***"
    echo "*****************************"
    exit 1
fi

exit 0
