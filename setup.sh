#!/bin/sh
PADDING_TOOL=/home/ljj/work/pos/scl8500/tool/pading
KEY_TOOL_PATH=/home/ljj/work/pos/scl8500/keys
DEST_PATH=/tftpboot/

${KEY_TOOL_PATH}/sign ${KEY_TOOL_PATH}/sys_sign.pem -sign arch/arm/boot/uImage -type 0
${PADDING_TOOL} arch/arm/boot/uImage
${PADDING_TOOL} arch/arm/boot/uImage.sign

cp arch/arm/boot/uImage.sign.paded ${DEST_PATH}
cp arch/arm/boot/uImage.paded ${DEST_PATH}
