#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-linux-gnu-

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git fetch --tags
    git checkout ${KERNEL_VERSION}

    # TODO: Add your kernel build steps here
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper                   # clean the kernel source tree (removing previous builds)
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} -j$(nproc) defconfig       # generate a default kernel configuration for ARM64
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} -j$(nproc) all             # compile the kernel module and device tree blobs

    cp arch/${ARCH}/boot/Image ${OUTDIR}/                                       # copy the compiled kernel image to OUTDIR
fi

echo "Adding the Image in outdir"

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# TODO: Create necessary base directories
mkdir -p ${OUTDIR}/rootfs                                       # create the rootfs directory
cd ${OUTDIR}/rootfs                                             # enter the newly created rootfs directory
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var   # create the directory structure for a minimal standard Linux root filesystem
mkdir -p usr/bin usr/lib usr/sbin                               # see above
mkdir -p var/log                                                # see above
cd ..                                                           # leave rootfs and return to OUTDIR

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone https://busybox.net/busybox.git
    cd busybox
    git fetch --tags
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox
    make distclean      # clean the BusyBox source tree
    echo "CONFIG_STATIC=y" >> .config                                                               # enable static linking
    make defconfig      # generate a default BusyBox configuration
    #make menuconfig     # open the text-based UI to customize BusyBox (commented out, screen just freezes)
else
    cd busybox
fi

# TODO: Make and install busybox
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} LDFLAGS=-static                                # cross-compile (statically linked) BusyBox for ARM64
make CONFIG_PREFIX=${OUTDIR}/rootfs ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install         # install the BusyBox binaries into rootfs
sudo chmod +s ${OUTDIR}/rootfs/bin/busybox                                                      # set setuid bit

echo "Library dependencies"
${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "Shared library"

# TODO: Add library dependencies to rootfs
cp /usr/aarch64-linux-gnu/lib/ld-linux-aarch64.so.1 ${OUTDIR}/rootfs/lib/       # copy the dynamic linker
cp /usr/aarch64-linux-gnu/lib/libc.so.6 ${OUTDIR}/rootfs/lib64/                 # copy the ARM64-compatible libc shared library into rootfs/lib64, ensuring BusyBox can resolve dependencies.
cp /usr/aarch64-linux-gnu/lib/libm.so.6 ${OUTDIR}/rootfs/lib64/                 # copy the ARM64-compatible libc shared library into rootfs/lib64, ensuring BusyBox can resolve dependencies.
cp /usr/aarch64-linux-gnu/lib/libresolv.so.2 ${OUTDIR}/rootfs/lib64/            # copy the ARM64-compatible libc shared library into rootfs/lib64, ensuring BusyBox can resolve dependencies.
cp /usr/aarch64-linux-gnu/lib/libc.so.6 ${OUTDIR}/rootfs/lib/                   # copy the ARM64-compatible libc shared library into rootfs/lib, ensuring BusyBox can resolve dependencies.
cp /usr/aarch64-linux-gnu/lib/libm.so.6 ${OUTDIR}/rootfs/lib/                   # copy the ARM64-compatible libc shared library into rootfs/lib, ensuring BusyBox can resolve dependencies.
cp /usr/aarch64-linux-gnu/lib/libresolv.so.2 ${OUTDIR}/rootfs/lib/              # copy the ARM64-compatible libc shared library into rootfs/lib, ensuring BusyBox can resolve dependencies.

# TODO: Make device nodes
sudo mknod -m 666 ${OUTDIR}/rootfs/dev/null c 1 3       # creates a device node (character device with read/write permissions for all) in /dev/null which discards all data written to it
sudo mknod -m 666 ${OUTDIR}/rootfs/dev/console c 5 1    # creates a device node (character device with read/write permissions for all) in /dev/console which handles system console output

# TODO: Clean and build the writer utility
cd ${FINDER_APP_DIR}
make clean
make CROSS_COMPILE=${CROSS_COMPILE}

# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
cp ${FINDER_APP_DIR}/writer ${OUTDIR}/rootfs/home/
cp ${FINDER_APP_DIR}/finder.sh ${OUTDIR}/rootfs/home/
chmod +x ${OUTDIR}/rootfs/home/finder.sh
cp ${FINDER_APP_DIR}/finder-test.sh ${OUTDIR}/rootfs/home/
chmod +x ${OUTDIR}/rootfs/home/finder-test.sh
cp ${FINDER_APP_DIR}/autorun-qemu.sh ${OUTDIR}/rootfs/home/
cp ${FINDER_APP_DIR}/start-qemu-app.sh ${OUTDIR}/rootfs/home/
mkdir -p ${OUTDIR}/rootfs/home/conf
cp ${FINDER_APP_DIR}/conf/username.txt ${OUTDIR}/rootfs/home/conf/
cp ${FINDER_APP_DIR}/conf/assignment.txt ${OUTDIR}/rootfs/home/conf/

# TODO: Chown the root directory
cd ${OUTDIR}/rootfs
sudo chown -R root:root *
cd ..

# TODO: Create initramfs.cpio.gz
cd ${OUTDIR}/rootfs
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
cd ..
gzip -f "${OUTDIR}/initramfs.cpio"
