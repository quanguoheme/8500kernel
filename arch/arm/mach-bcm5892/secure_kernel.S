	.align 3
	.globl	secure_kernel_start
secure_kernel_start:
	.incbin	"arch/arm/mach-bcm5892/secure_kernel.bin"
	.globl	secure_kernel_end
secure_kernel_end:
