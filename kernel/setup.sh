#!/bin/sh
set -eu

GKI_ROOT=$(pwd)

display_usage() {
    echo "Usage: $0 [--cleanup | <commit-or-tag>]"
    echo "  --cleanup:              Cleans up previous modifications made by the script."
    echo "  <commit-or-tag>:        Sets up or updates vpnhide to specified tag or commit."
    echo "  -h, --help:             Displays this usage information."
    echo "  (no args):              Sets up or updates vpnhide to the latest tagged version."
}

initialize_variables() {
    if test -d "$GKI_ROOT/common/drivers"; then
        DRIVER_DIR="$GKI_ROOT/common/drivers"
    elif test -d "$GKI_ROOT/drivers"; then
        DRIVER_DIR="$GKI_ROOT/drivers"
    else
        echo '[ERROR] "drivers/" directory not found.'
        exit 127
    fi

    DRIVER_MAKEFILE=$DRIVER_DIR/Makefile
    DRIVER_KCONFIG=$DRIVER_DIR/Kconfig
}

perform_cleanup() {
    echo "[+] Cleaning up..."
    [ -L "$DRIVER_DIR/vpnhide" ] && rm "$DRIVER_DIR/vpnhide" && echo "[-] Symlink removed."
    grep -q "vpnhide" "$DRIVER_MAKEFILE" && sed -i '/vpnhide/d' "$DRIVER_MAKEFILE" && echo "[-] Makefile reverted."
    grep -q "drivers/vpnhide/Kconfig" "$DRIVER_KCONFIG" && sed -i '/drivers\/vpnhide\/Kconfig/d' "$DRIVER_KCONFIG" && echo "[-] Kconfig reverted."
    if [ -d "$GKI_ROOT/vpnhide-driver" ]; then
        rm -rf "$GKI_ROOT/vpnhide-driver" && echo "[-] vpnhide-driver directory deleted."
    fi
}

setup_vpnhide() {
    echo "[+] Setting up vpnhide..."
    test -d "$GKI_ROOT/vpnhide-driver" || git clone https://github.com/cyberc3dr/vpnhide-driver "$GKI_ROOT/vpnhide-driver" && echo "[+] Repository cloned."
    cd "$GKI_ROOT/vpnhide-driver"
    git stash && echo "[-] Stashed current changes."
    if [ "$(git status | grep -Po 'v\d+(\.\d+)*' | head -n1)" ]; then
        git checkout main && echo "[-] Switched to main branch."
    fi
    git pull && echo "[+] Repository updated."
    if [ -z "${1-}" ]; then
        git checkout "$(git describe --abbrev=0 --tags)" && echo "[-] Checked out latest tag."
    else
        git checkout "$1" && echo "[-] Checked out $1." || echo "[-] Checkout default branch"
    fi
    cd "$DRIVER_DIR"
    ln -sf "$(realpath --relative-to="$DRIVER_DIR" "$GKI_ROOT/vpnhide-driver/kernel")" "vpnhide" && echo "[+] Symlink created."

    grep -q "vpnhide" "$DRIVER_MAKEFILE" || printf "\nobj-\$(CONFIG_VPNHIDE) += vpnhide/\n" >> "$DRIVER_MAKEFILE" && echo "[+] Modified Makefile."
    grep -q "source \"drivers/vpnhide/Kconfig\"" "$DRIVER_KCONFIG" || sed -i "/endmenu/i\\source \"drivers/vpnhide/Kconfig\"" "$DRIVER_KCONFIG" && echo "[+] Modified Kconfig."
    echo '[+] Done.'
}

if [ "$#" -eq 0 ]; then
    initialize_variables
    setup_vpnhide
elif [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    display_usage
elif [ "$1" = "--cleanup" ]; then
    initialize_variables
    perform_cleanup
else
    initialize_variables
    setup_vpnhide "$@"
fi
