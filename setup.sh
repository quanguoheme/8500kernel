#!/bin/sh
cd  /home/ljj/work/pos/scl8500/kernel/linux-2.6.32.9/arch/arm/boot

./sign sys_sign.pem -sign uImage -type 0
cp uImage.sign /tftpboot/