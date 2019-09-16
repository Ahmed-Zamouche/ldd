#!/bin/bash

module="foo"


text=$(cat  /sys/module/$module/sections/.text)
data=$(cat  /sys/module/$module/sections/.data)
bss=$(cat  /sys/module/$module/sections/.bss)


echo "add-symbol-file ./foo.ko $text -s .data $data -s .bss $bss"

sudo gdb vmlinux /proc/kcore