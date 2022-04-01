#
# This file is the Accelize DRM library recipe.
#

SUMMARY = "Accelize DRM Library"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://${WORKDIR}/git/LICENSE;md5=e368f0931469a726e1017aaa567bd040"

SRC_URI = "gitsm://github.com/Accelize/drm.git;protocol=http;branch=master"
SRCREV = "${AUTOREV}"

#FIXME specific version but set to AUTOREV
PV = "2.5.4" 

DEPENDS += "curl jsoncpp"

S = "${WORKDIR}/git"

inherit pkgconfig cmake

FILES:${PN} += "${libdir}/*"
