#!/bin/bash
module="foo"
device="foo"
mode="666"

# invoke insmod with all arguments we got
# and use a pathname, as newer modutils don't look in . by default
/sbin/insmod ./build/$module.ko $* || exit 1

# remove stale nodes
rm -f /dev/${device}[0-3]
major=$(awk "\$2==\"$module\" {print \$1}" /proc/devices)
[ -z "$major" ] && exit 1

mknod /dev/${device}0 c $major 0
mknod /dev/${device}1 c $major 1
mknod /dev/${device}2 c $major 2
mknod /dev/${device}3 c $major 3

# give appropriate group/permissions, and change the group.
# Not all distributions have staff, some have "wheel" instead.
group="staff"
grep -q '^staff:' /etc/group || group="wheel"
chgrp $group /dev/${device}[0-3]
chmod $mode /dev/${device}[0-3]
