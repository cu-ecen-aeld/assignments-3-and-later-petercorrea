#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e # Exit on error
set -u # Exit on undefined variable usage

# Setting variables
OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.1.10
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

# Check if an output directory is provided, use default if not
if [ $# -lt 1 ]; then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

# Create necessary output and rootfs directories
mkdir -p ${OUTDIR}
ROOT_FS=${OUTDIR}/rootfs

# Delete existing rootfs directory if it exists
if [ -d "${ROOT_FS}" ]; then
	echo "Deleting rootfs directory at ${ROOT_FS} and starting over"
    sudo rm -rf ${ROOT_FS}
fi

mkdir -p ${ROOT_FS}

# Clone the kernel repository
cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi

# Build the kernel 
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # TODO: Add your kernel build steps here
    # Clear old build and configure the kernel
    echo "i am: $(whoami)"
    make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- mrproper
    make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- defconfig

    # Build the kernel
    make -j8 ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- all
fi

# Copy the compiled kernel image
echo "Adding the Image in outdir"
cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}

# Prepare root filesystem
echo "Creating the staging directory for the root filesystem"
cd "$ROOT_FS"
mkdir bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir usr/bin usr/lib usr/sbin
mkdir var/log

# Clone, configure, and build BusyBox
echo "Clone, configure, and build BusyBox"
cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]; then
    git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox
    make distclean
    make defconfig
fi

# TODO: Make and install busybox
# Compile and install BusyBox
# arm64 /tmp/aesd-autograder/rootfs aarch64-none-linux-gnu-
echo "Compile and install BusyBox"
cd ${OUTDIR}/busybox
echo ${ARCH} ${ROOT_FS} ${CROSS_COMPILE}
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
make CONFIG_PREFIX=${ROOT_FS} ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install

# Display library dependencies
# echo "Library dependencies"
# ${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
# ${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"

# TODO: Add library dependencies to rootfs
# Copy library dependencies to rootfs
echo "Copy library dependencies to rootfs"
cd "$OUTDIR"

sysroot=$(${CROSS_COMPILE}gcc -print-sysroot)

lib+=(`${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "program interpreter" | grep -oP "lib/.+\.so.\d+"`)
cp $(find $sysroot -path */$lib) ${OUTDIR}/rootfs/${lib}

libs+=(`${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "Shared library" | grep -oP "(?<=\[).+\.so.\d+(?=\])"`)
for lib in "${libs[@]}"
do
    echo $lib
    cp $(find $sysroot -path */$lib) ${OUTDIR}/rootfs/lib64/$(basename ${lib})
done

cd ${OUTDIR}/rootfs

# TODO: Make device nodes
# Create device nodes
echo "Create device nodes"
sudo mknod -m 666 dev/null c 1 3
sudo mknod -m 666 dev/tty c 5 1

# TODO: Clean and build the writer utility
# Clean and build the writer utility
echo "Clean and build the writer utility"
cd ${FINDER_APP_DIR}
make clean
make CROSS_COMPILE=${CROSS_COMPILE}

# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
# Copy scripts and executables to target rootfs
echo "Copy scripts and executables to target rootfs"
mkdir -p $ROOT_FS/home/conf $ROOT_FS/conf
cp writer $ROOT_FS/home
cp finder.sh $ROOT_FS/home
cp finder-test.sh $ROOT_FS/home
cp autorun-qemu.sh $ROOT_FS/home
cp conf/username.txt $ROOT_FS/home/conf/username.txt
cp conf/assignment.txt $ROOT_FS/conf/assignment.txt

# TODO: Chown the root directory
# Change ownership to root user and group
echo "Change ownership to root user and group"
sudo chown -R root:root $ROOT_FS

# TODO: Create initramfs.cpio.gz
# Create initramfs
echo "Create initramfs"
cd $ROOT_FS
find . | cpio -H newc -ov --owner root:root > $OUTDIR/initramfs.cpio
gzip -f $OUTDIR/initramfs.cpio

# Remove temporary BusyBox binary directory
# rm -rf $OUTDIR/busybox/bin