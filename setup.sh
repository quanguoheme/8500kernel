#!/bin/sh
KEY_TOOL_PATH=/home/ljj/work/pos/scl8500/keys
DEST_PATH=/tftpboot/

${KEY_TOOL_PATH}/sign ${KEY_TOOL_PATH}/sys_sign.pem -sign arch/arm/boot/uImage -type 0
cp arch/arm/boot/uImage.sign ${DEST_PATH}
