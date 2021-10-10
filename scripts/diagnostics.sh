#!/usr/bin/env bash
set -e


all_components=(
    "dmi"
    "dmesg"
    "acpidump"
    "acpi"
    "platform"
    "serial"
    "sam"
    "kconfig"
    "lspci"
)


# commands-line interface
print_help () {
    pname="$(basename "${0}")"

    echo "${pname} - Linux Surface diagnostics tool"
    echo ""
    echo "Collects diagnostics information for Microsoft Surface devices running Linux."
    echo ""
    echo "USAGE:"
    echo "  ${pname} [FLAGS...] COMPONENTS..."
    echo ""
    echo "FLAGS:"
    echo "  -h, --help          Prints this help message"
    echo "  -o, --output FILE   The gzip archive to write to [default: diagnostics.tar.gz]"
    echo ""
    echo "COMPONENTS:"
    echo "  Space-separated list of components or 'full' for all components."
    echo ""
    echo "  Available components are:"
    for c in "${all_components[@]}"; do
        echo "    ${c}"
    done
    echo ""
    echo "Examples:"
    echo "  ${pname} --help        Print this help message"
    echo "  ${pname} full          Collect full suite of diagnostics"
    echo "  ${pname} dmi acpidump  Collect only DMI information and ACPI dump"
}

components=()
archive="diagnostics.tar.gz"

while [[ ${#} -gt 0 ]]; do
    key="${1}"

    case "${key}" in
        -h|--help)
            print_help
            exit
            ;;
        -o|--output)
            if [[ ${#} -lt 2 ]]; then
                echo "Error: Missing argument for option '${1}'"
                echo "       Run with '--help' for more information."
                exit 1
            fi

            archive="${2}"

            if [[ "${archive}" != *".tar.gz" ]]; then
                echo "Error: Invalid file ending for output archive, should be '.tar.gz'."
                exit 1
            fi

            shift 2
            ;;
        *)
            components+=("${1}")
            shift
            ;;
    esac
done

# ensure some component is selected
if [[ ${#components[@]} -eq 0 ]]; then
    echo "Error: No component selected"
    echo "       Run with '--help' for more information."
    exit 1
fi

# handle special component names
if [[ " ${components[*]} " =~ " full " ]]; then
    components=("${all_components[@]}")
fi

# validate component list
for c in "${components[@]}"; do
    if [[ ! " ${all_components[*]} " =~ " ${c} " ]]; then
        echo "Error: Unknown component '${c}'"
        echo "       Run with '--help' for more information."
        exit 1
    fi
done;


# set up temporary directory
tmpdir=""

on_exit () {
    # remove temporary directory if set
    if [[ -n "${tmpdir}" ]]; then
        rm -rf "${tmpdir}"
    fi
}
trap on_exit EXIT

tmpdir="$(mktemp -d -t "surface-diagnostics.XXXXXX")"


# collect information
timestamp=$(date --iso-8601=seconds)

collect_sysfs () {
    subsystem="${1}"

    if [[ -d "/sys/bus/${subsystem}/" ]]; then
        tree -l -L 3 "/sys/bus/${subsystem}/drivers/" > "${tmpdir}/sys-bus-${subsystem}-drivers.txt"
        tree -l -L 3 "/sys/bus/${subsystem}/devices/" > "${tmpdir}/sys-bus-${subsystem}-devices.txt"
    else
        echo "<subsystem not available>" > "${tmpdir}/sys-bus-${subsystem}-drivers.txt"
        echo "<subsystem not available>" > "${tmpdir}/sys-bus-${subsystem}-devices.txt"
    fi
}


# collect timestamp and uname
echo " ==> collecting information..."
echo "${timestamp}" > "${tmpdir}/timestamp"

echo "   - uname"
uname -a > "${tmpdir}/uname"

# collect DMI if specified
if [[ " ${components[*]} " =~ " dmi " ]]; then
    echo "   - DMI system information"
    sudo dmidecode -t system > "${tmpdir}/dmi-system.txt"
fi

# collect dmesg log if specified
if [[ " ${components[*]} " =~ " dmesg " ]]; then
    echo "   - dmesg log"
    sudo dmesg > "${tmpdir}/dmesg.log"
fi

# collect acpidump if specified
if [[ " ${components[*]} " =~ " acpidump " ]]; then
    echo "   - ACPI dump"
    sudo acpidump | perl -pe 'BEGIN{undef $/;} s|MSDM.*?\n\n||sgm' > "${tmpdir}/acpidump.txt"
fi

# collect acpi sysfs entries and device paths if specified
if [[ " ${components[*]} " =~ " acpi " ]]; then
    echo "   - ACPI device information and paths (sysfs)"
    collect_sysfs "acpi"
    grep ".*" "/sys/bus/acpi/devices/"*"/path" > "${tmpdir}/sys-bus-acpi-devices.paths.txt"
fi

# collect platform sysfs entries
if [[ " ${components[*]} " =~ " platform " ]]; then
    echo "   - platform device information (sysfs)"
    collect_sysfs "platform"
fi

# collect serial sysfs entries
if [[ " ${components[*]} " =~ " serial " ]]; then
    echo "   - serial/UART device information (sysfs)"
    collect_sysfs "serial"
fi

# collect SAM information
if [[ " ${components[*]} " =~ " sam " ]]; then
    echo "   - surface aggregator module information (sysfs)"
    collect_sysfs "surface_aggregator"

    # get SAM firmware version
    out="${tmpdir}/sam-firmware-version"

    if [[ ! -d "/sys/bus/acpi/devices/MSHW0084:00/" ]]; then
        echo "<MSHW0084:00 does not exist>" > "${out}"
    elif [[ ! -d "/sys/bus/acpi/devices/MSHW0084:00/physical_node" ]]; then
        echo "<MSHW0084:00 does not have a physical node>" > "${out}"
    elif [[ ! -d "/sys/bus/acpi/devices/MSHW0084:00/physical_node/sam" ]]; then
        echo "<MSHW0084:00 physical node does not have SAM attributes>" > "${out}"
    else
        cat "/sys/bus/acpi/devices/MSHW0084:00/physical_node/sam/firmware_version" > "${out}"
    fi
fi

# collect kernel config
if [[ " ${components[*]} " =~ " kconfig " ]]; then
    echo "   - kernel configuration"

    # attempt to load module for accessing current kernel config 
    if modinfo configs > /dev/null 2>&1; then
        sudo modprobe configs
    fi

    # try to get current config from /proc, if available
    if [[ -f "/proc/config.gz" ]]; then
        zcat "/proc/config.gz" > "${tmpdir}/kernel-$(uname -r).conf"
    fi

    # try to get current config from /boot, if available
    if [[ -f "/boot/config" ]]; then
        cp "/boot/config" "${tmpdir}/kernel-$(uname -r).conf"
    fi

    # try to get any config from /boot
    find "/boot" -name 'config-*' -print0 | while IFS= read -r -d '' file; do 
        name="$(basename "${file}")"
        cp "${file}" "${tmpdir}/kernel-${name#"config-"}.conf"
    done;
fi

# collect lspci
if [[ " ${components[*]} " =~ " lspci " ]]; then
    echo "   - lspci"

    sudo lspci -nn -vvv > "${tmpdir}/lspci.txt"
    sudo lspci -nn -v -t > "${tmpdir}/lspci-tree.txt"
fi


# bundle to archive
echo " ==> generating archive..."

tar -czf "./${archive}" -C "${tmpdir}" .

echo " ==> done"
echo ""
echo "Please post the generated '${archive}' file."
