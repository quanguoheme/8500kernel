/*
 *this file contain the function of rtc-bcm5892.c
 *
 */

#ifndef __ARM_CONSOLE_H__
#define __ARM_CONSOLE_H__
#include <mach/uncompress.h>

#define UART_FR_TXFF              0x20
#define IOP13XX_PMMR_PHYS_MEM_BASE	   0xffd80000UL
#define IOP13XX_UART1_PHYS  (IOP13XX_PMMR_PHYS_MEM_BASE | 0x00002340)
#define UART_BASE ((volatile u32 *)IOP13XX_UART1_PHYS)
#define URT_R_UARTFR_MEMADDR (UART_BASE + 0x18)
#define URT_R_UARTDR_MEMADDR (UART_BASE + 0x00)
#define CDS_STATUS_OK 0

#define print_log my_printf


int cls_uart_write (uint8_t *data, uint32_t data_len)
{
    volatile uint32_t i;
    volatile uint8_t fifo_full;

    for (i=0; i<data_len; i++, data++)
    {
	// wait if fifo is not full
#if 1	
        fifo_full = UART_FR_TXFF;
        while (fifo_full == UART_FR_TXFF)  
		{
           // fifo_full = (CPU_READ_SINGLE(URT_R_UARTFR_MEMADDR, AHB_BIT16));
           fifo_full =__raw_readl(IO_ADDRESS(URT_R_UARTFR_MEMADDR));
           fifo_full = fifo_full & UART_FR_TXFF;
        }
#endif        
    	//CPU_WRITE_SINGLE(URT_R_UARTDR_MEMADDR,*data,AHB_BIT16);
    	__raw_writel(*data,IO_ADDRESS(URT_R_UARTDR_MEMADDR));
    }

    return CDS_STATUS_OK;
}

int my_printf(const char *format, ...)
{
        va_list args;
		int rc;
		char cr = 0x0d;
		// char lf = 0x0a;
        
        va_start( args, format );
        rc = printk( 0, format, args );

		// some of the log_prints assume return is concatinated
		cls_uart_write ((uint8_t*)&cr, 1);
		return rc;
}









#endif















