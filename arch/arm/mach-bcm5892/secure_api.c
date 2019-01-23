/*
 * linux/arch/arm/mach-za9l/tti_proc.c
 *
 * Copyright (C) 2010 strong - lion, Inc.
 *
 * Author: lxyvslyr <lxyvslyr@yahoo.com.cn.>
 * Date  : $Date: 2010/12/23 06:25:59 $
 *
 * $Revision: 1.1.1.1 $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the  
 * GNU General Public License for more details. 
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */ 
 
#include <linux/module.h> 
#include <linux/kernel.h> 
#include <linux/init.h>
#include <linux/unistd.h>
#include <linux/proc_fs.h>
#include <linux/errno.h>
#include <linux/compile.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/clk.h>

#include <asm/uaccess.h>
#include <asm/io.h>

#include <mach/reg_gpio.h>
#include <stdarg.h>
#include <mach/secure_api.h>

#define SECURE_OFFSET 		0x9860
char *secure_dest ;

struct mbox secure_mb_ptr;

int call_secure_kernel(int api_id,int argc,...)
{
	va_list ap;
	int i = 0;

	va_start(ap, argc);

	if ((argc > MAXARGS) || ((api_id < 0) || (api_id > INVALID_ID))) 
	{
		printk("Invalid argument\n");
		return 1;
	}
	
	secure_mb_ptr.id = api_id;
   	secure_mb_ptr.args[0] = 0; 
   	secure_mb_ptr.num_args = argc;

	while(i < argc)
   	{
   		secure_mb_ptr.args[i] = va_arg(ap, unsigned int); 
	  	i++;
   	}

	__asm__(

			"PUSH   {r0-r12}\n\t"     		

#if 0
		  	"LDR		r0,=0xd00d0000\n\t"
			"MOV	r1,#'J'\n\t"
			"STR		r1,[r0,#0x0]\n\t"	

		  	"LDR		r0,=0xd00d0000\n\t"
			"MOV	r1,#'P'\n\t"
			"STR		r1,[r0,#0x0]\n\t"	

		  	"LDR		r0,=0xd00d0000\n\t"
			"MOV	r1,#'\n'\n\t"
			"STR		r1,[r0,#0x0]\n\t"	

		  	"LDR		r0,=0xd00d0000\n\t"
			"MOV	r1,#'\r'\n\t"
			"STR		r1,[r0,#0x0]\n\t"	
#endif			

 			"LDR		r0,=secure_mb_ptr\n\t"

			"LDR		r4,=secure_dest\n\t"
			"LDR		r4,[r4]\n\t"
			
			"BLX	 	r4\n\t"
  		

  			"POP   	{r0-r12}\n\t"
	);

	if (secure_mb_ptr.ret_valid)
		return secure_mb_ptr.return_value;
	else
		return -1;
}

extern char secure_kernel_start[];
extern char secure_kernel_end[];
static int __init init_secure_kernel(void)
{
	int ret = 0;        
	char *addr_start = &secure_kernel_start[0];
	char *addr_end = &secure_kernel_end[0];

	printk("Initializing secure_kernel ...\n");

	secure_dest = IO_ADDRESS(START_SCRATCH);

	secure_dest += SECURE_OFFSET;

	memcpy(secure_dest,addr_start, addr_end - addr_start );	
	
out:    
	return ret;
}

static void __exit cleanup_secure_kernel(void)
{
	return;
}

EXPORT_SYMBOL(secure_dest);

module_init(init_secure_kernel);
module_exit(cleanup_secure_kernel);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LJJ<lxyvslyr@yahoo.com.cn>");
MODULE_DESCRIPTION("The driver");

