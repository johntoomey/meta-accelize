#
# This file is the fpgatest recipe.
#

SUMMARY = "Simple fpgatest to use fpgamanager class"
SECTION = "PETALINUX/apps"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit fpgamanager_custom
inherit update-alternatives
FPGA_MNGR_RECONFIG_ENABLE = "1"

SRC_URI = "file://kv260-aibox-reid.dtsi \
           file://shell.json  \
           file://system.bit \
           file://fpgatest.xclbin \
           "

S = "${WORKDIR}"

do_install_append() {
	install -d ${D}${sysconfdir}/dfx-mgrd
	echo "${PN}" > ${D}${sysconfdir}/dfx-mgrd/${PN}
}

FILES_${PN} += "${sysconfdir}/dfx-mgrd/${PN}"

# Override the default package arch inherited from kv260-firmware.inc which
# is k26-kv. In order to add this package into the starter rootfs, the arch
# is changed to k26 instead. Do this only for this package as kv260-dp will
# be used as the default firmware to be loaded by dfx-mgrd during init.
COMPATIBLE_MACHINE_k26 = "${MACHINE}"
PACKAGE_ARCH = "${BOARD_ARCH}"

ALTERNATIVE_${PN} = "default_firmware"
ALTERNATIVE_TARGET[default_firmware] = "${sysconfdir}/dfx-mgrd/${PN}"
ALTERNATIVE_LINK_NAME[default_firmware] = "${sysconfdir}/dfx-mgrd/default_firmware"
ALTERNATIVE_PRIORITY[default_firmware] = "10"
