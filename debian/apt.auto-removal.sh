#!/bin/sh
set -e

# Author: Steve Langasek <steve.langasek@canonical.com>
#
# Mark as not-for-autoremoval those kernel packages that are:
#  - the currently booted version
#  - the kernel version we've been called for
#  - the latest kernel version (determined using rules copied from the grub
#    package for deciding which kernel to boot)
#  - the second-latest kernel version, if the booted kernel version is
#    already the latest and this script is called for that same version,
#    to ensure a fallback remains available in the event the newly-installed
#    kernel at this ABI fails to boot
# In the common case, this results in exactly two kernels saved, but it can
# result in three kernels being saved.  It's better to err on the side of
# saving too many kernels than saving too few.
#
# We generate this list and save it to /etc/apt/apt.conf.d instead of marking
# packages in the database because this runs from a postinst script, and apt
# will overwrite the db when it exits.


eval $(apt-config shell APT_CONF_D Dir::Etc::parts/d)
test -n "${APT_CONF_D}" || APT_CONF_D="/etc/apt/apt.conf.d"
config_file=${APT_CONF_D}/01autoremove-kernels

eval $(apt-config shell DPKG Dir::bin::dpkg/f)
test -n "$DPKG" || DPKG="/usr/bin/dpkg"

installed_version="$1"
running_version="$(uname -r)"


version_test_gt ()
{
	local version_test_gt_sedexp="s/[._-]\(pre\|rc\|test\|git\|old\|trunk\)/~\1/g"
	local version_a="`echo "$1" | sed -e "$version_test_gt_sedexp"`"
	local version_b="`echo "$2" | sed -e "$version_test_gt_sedexp"`"
	$DPKG --compare-versions "$version_a" gt "$version_b"
	return "$?"
}

list="$(${DPKG} -l | awk '/^ii[ ]+(linux|kfreebsd|gnumach)-image-[0-9]*/ && $2 !~ /-dbg$/ { print $2 }' | sed -e 's#\(linux\|kfreebsd\|gnumach\)-image-##')"

latest_version=""
previous_version=""
for i in $list; do
	if version_test_gt "$i" "$latest_version"; then
		previous_version="$latest_version"
		latest_version="$i"
	elif version_test_gt "$i" "$previous_version"; then
		previous_version="$i"
	fi
done

if [ "$latest_version" != "$installed_version" ] \
   || [ "$latest_version" != "$running_version" ] \
   || [ "$installed_version" != "$running_version" ]
then
	# We have at least two kernels that we have reason to think the
	# user wants, so don't save the second-newest version.
	previous_version=
fi

kernels=$(sort -u <<EOF
$latest_version
$installed_version
$running_version
$previous_version
EOF
)

generateconfig() {
	cat <<EOF
// DO NOT EDIT! File autogenerated by $0
APT::NeverAutoRemove
{
EOF
	apt-config dump --no-empty --format '%v%n' 'APT::VersionedKernelPackages' | while read package; do
		for kernel in $kernels; do
			echo "   \"^${package}-${kernel}$\";"
		done
	done
	echo '};'
}
generateconfig > "${config_file}.dpkg-new"
mv "${config_file}.dpkg-new" "$config_file"
