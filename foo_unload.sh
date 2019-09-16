#!/bin/bash
module="foo"
device="foo"

# invoke insmod with all arguments we got
# and use a pathname, as newer modutils don't look in . by default
/sbin/rmmod -f $module || exit 1

# remove stale nodes
rm -f /dev/${device}[0-3]
