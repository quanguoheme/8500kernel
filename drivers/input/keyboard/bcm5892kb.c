/*
 * $Id: sam1416kb.c,v 1.1.1.1 2007/04/04 06:26:43 tony Exp $
 *
 *  Copyright (c) 2010 strong-lion
 *    	Lijianjun   2010-09
 *
 */   
/*
 *  keyboard driver for Linux/sam2416
 */

/*
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Simunkova 1594, Prague 8, 182 00 Czech Republic
 */
 
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/errno.h>
#include <linux/pm.h>

#include <linux/workqueue.h>
#include <linux/strong_lion_def.h> 

#include <asm/io.h>
#include <mach/keyboard.h>

#include <mach/bcm5892_reg.h>

#include <mach/hardware.h>		//self	
#include <mach/reg_gpio.h>		//self

#include <linux/unistd.h>

extern struct proc_dir_entry *stronglion_root;

#define SCAN_INTERVAL		5   //the interval time to scan the keyboard. 5 jiffer
static unsigned long kb_count=0;    //record the number of scaning the keyboard since starting the system

static int sleep_interval = 0;         //the escape time before we enter the sleep mode
//static int sleep_interval = 5;    	 //for test
static unsigned long kb_enter_sleep_counter = 0; 
static int kb_would_sleep_flag = 0 ;
struct task_struct *kb_client = NULL;
int kb_client_pid = -1;

//the fellow three parameter recode the information of  power_switch key 
static int power_switch_flag=0;
static int power_count=0;
#define POWER_CONFIRM_DELAY	((2*HZ)/SCAN_INTERVAL)	//2s is the delay time for confirming to power off
#define POWER_DEAD_DELAY	((4*HZ)/SCAN_INTERVAL)  //4s is the delay time to power off the system
#define POWER_SIG	SIGURG
static int wait_to_dead = 0;

#define KEY_MAX_ROWS		4
#define KEY_MAX_COLS		5
#define FIRST_ROWS_POS		3      //the first row postion at gpio_g
#define FIRST_C_COLS_POS	20     //the first cloumn postion at gpio_c
#define FIRST_D_COLS_POS	24     //the first cloumn postion at gpio_d

//scan key ,the power key excluded
#if 0
#if (defined CONFIG_POS_8200)
#define KEY_NUM	(KEY_MAX_ROWS * KEY_MAX_COLS-2 )
#elif (defined CONFIG_POS_8300) || (defined CONFIG_POS_8300C)
#define KEY_NUM	(KEY_MAX_ROWS * KEY_MAX_COLS-1 )	
#endif
#endif
#define KEY_NUM	(KEY_MAX_ROWS * KEY_MAX_COLS-2 )	

#define ROW_PINS_BASE   (HW_GPIO0_PIN_MAX + 16)
#define NUM_ROWS		4
#define COL_PINS_BASE   (HW_GPIO0_PIN_MAX + 20)
#define NUM_COLS		4



#if 1
static unsigned char sam2416_keycode[30] = {
	[0]	 = KK_1,
	[1]	 = KK_2,
	[2]	 = KK_3,
	[3]	 = KK_4,
	[4]	 = KK_5,
	
	[5]	 = KK_6,
	[6]	 = KK_7,
	[7]	 = KK_8,
	[8]	 = KK_9,
	[9]	 = KK_0,
	
	[10]	 = KK_A,
	[11]	 = KK_B,
	[12]	 = KK_C,
	[13]	 = KK_D,
	[14]     = KK_E,
	
	[15]	 = KK_F,
	[16]	 = KK_G,
	[17]	 = KK_H,
	[18]	 = KK_I,
	[19]	 = KK_J,
//	[19]	 = KK_ENTR,

	[20]	 = KK_K,
	[21]	 = KK_L,
	[22]	 = KK_M,
	[23]	 = KK_N,
	[24]	 = KK_O,
	
	[25]	 = KK_P,
	[26]	 = KK_Q,
	[27]	 = KK_R,
	[28]	 = KK_S,
	[29]	 = KK_T,
};
#endif

#if 0   
//if we want the key as the console ,we use the fellow define
static unsigned char sam2416_keycode[30] = {
	[0]	 = KK_A,
	[1]	 = KK_ENTR,
	[2]	 = KK_3,
	[3]	 = KK_2,
	[4]	 = KK_1,
	
	[5]	 = KK_H,
	[6]	 = KK_D,
	[7]	 = KK_E,
	[8]	 = KK_0,
	[9]	 = KK_D,
	
	[10]	 = KK_M,
	[11]	 = KK_L,
	[12]	 = KK_K,
	[13]	 = KK_J,
	[14]     = KK_I,
	
	[15]	 = KK_I,
	[16]	 = KK_H,
	[17]	 = KK_G,
	[18]	 = KK_F,
	[19]	 = KK_E,
//	[19]	 = KK_ENTR,

	[20]	 = KK_K,
	[21]	 = KK_D,
	[22]	 = KK_C,
	[23]	 = KK_B,
	[24]	 = KK_A,
	
	[25]	 = KK_P,
	[26]	 = KK_Q,
	[27]	 = KK_R,
	[28]	 = KK_S,
	[29]	 = KK_T,
};
#endif

static unsigned int last_status_map;                                    //the maxium number of key is 32
static unsigned int status_map_mask = 0xffffffff;

#define ROW_VALUE_MASK_C0			0xf0000                         //the row mask when col is 0
#define FIRST_VALID_ROW_BIT_C0 	16                             //the first valid bit in row value when col number is 0
#define VALID_ROW_BITS_C0			KEY_MAX_ROWS	     //the number of useful bit when col number is 0

#define ROW_VALUE_MASK_C1			0xf0000                         //the row mask when col is 1
#define FIRST_VALID_ROW_BIT_C1 	16                             //the first valid bit in row value when col number is 1
#define VALID_ROW_BITS_C1			KEY_MAX_ROWS	     //the number of useful bit when col number is 1

#define ROW_VALUE_MASK_C2			0xf0000                         //the row mask when col is 2
#define FIRST_VALID_ROW_BIT_C2 	16                             //the first valid bit in row value when col number is 2
#define VALID_ROW_BITS_C2			KEY_MAX_ROWS	     //the number of useful bit when col number is 2

#define ROW_VALUE_MASK_C3			0xf0000                         //the row mask when col is 3
#define FIRST_VALID_ROW_BIT_C3 	16                             //the first valid bit in row value when col number is 3
#define VALID_ROW_BITS_C3			KEY_MAX_ROWS  		//the number of useful bit when col number is 3


#define ROW_VALUE_MASK_C4			0x30000                         //the row mask when col is 4
#define FIRST_VALID_ROW_BIT_C4 	16                             //the first valid bit in row value when col number is 4
#define VALID_ROW_BITS_C4			(KEY_MAX_ROWS-2)   //the number of useful bit when col number is 4


#define ROW_VALUE_MASK_DEFAULT	0xf0000                          //when all the colum output low,the valid row value

#define COL_C_OUTPUT_MASK			0xf00000				//valid col value 
#define COL_D_OUTPUT_MASK			0x1000000				 //valid col value 
#define COL_B_OUTPUT_MASK			0x1f00000				 //valid col value 


#define ON_OFF_SWITCH_BIT			1					//the postion of bit for on/off key in GIOG

static unsigned int row_value_mask[5] = {
	ROW_VALUE_MASK_C0,
	ROW_VALUE_MASK_C1,
	ROW_VALUE_MASK_C2,
	ROW_VALUE_MASK_C3,
	ROW_VALUE_MASK_C4,
};

static unsigned int first_valid_bit[5] = {
	FIRST_VALID_ROW_BIT_C0,
	FIRST_VALID_ROW_BIT_C1,
	FIRST_VALID_ROW_BIT_C2,
	FIRST_VALID_ROW_BIT_C3,
	FIRST_VALID_ROW_BIT_C4,
};

static unsigned int valid_bits[5] = {
	VALID_ROW_BITS_C0,
	VALID_ROW_BITS_C1,
	VALID_ROW_BITS_C2,
	VALID_ROW_BITS_C3,
	VALID_ROW_BITS_C4,
};

//when no key pressed ,the value of last_status_map
#define IDEAL_KEY_STATUS  ( row_value_mask[0] >> first_valid_bit[0]                      | \
	  ((row_value_mask[1] >> first_valid_bit[1]) << valid_bits[0])                             | \
	  ((row_value_mask[2] >> first_valid_bit[2]) << (valid_bits[0] + valid_bits[1]))     | \
	  ((row_value_mask[3] >> first_valid_bit[3]) << (valid_bits[0] +valid_bits[1] + valid_bits[2] )) | \
	  ((row_value_mask[4] >> first_valid_bit[4]) << (valid_bits[0] +valid_bits[1] + valid_bits[2] + valid_bits[3])) )

static struct input_dev *sam2416kb_dev;
static struct timer_list my_timer;

static enum {key_up ,key_down};

static int on_off_pressed=0;
static int on_off_counter=0;

#define ON_OFF_MIN_DELAY	3	/*in unite second*/

static char kb_proc_buf[10];
static struct proc_dir_entry *shut_down_proc;

static struct proc_dir_entry *sleep_interval_proc;

static void raise_mytimer(int interval);

extern int DC_status;

DECLARE_WORK(sys_sleep_worker,NULL);

static void prepare_power_off(void)
{
	struct siginfo sinfo;    
	struct task_struct *task;
	
	printk("prepare_power_off\n");  

#if 0	
	sinfo.si_ptr = (void *)kmalloc(sizeof(struct pos_signal),GFP_ATOMIC);
	if (!sinfo.si_ptr)
	{
        	printk("poweroff kmalloc faild\n");
		return ;
	}
#endif	
    
	for_each_process(task) {
//		printk("%s[%d]\n",task->comm,task->pid);
//		if (task->pid != 1)
		send_sig_info(POWER_SIG, &sinfo, task); 
	}
	
	power_switch_flag = 0;
	power_count = kb_count;
	wait_to_dead = 1;
	
	return ;
}
#if 1

static void all_gpio_to_inputs_with_pull(uint8_t up_down)
{
	int i;

	for (i = 0; i < NUM_ROWS; i++) {
		reg_gpio_iotr_set_pin_type(ROW_PINS_BASE+i, GPIO_PIN_TYPE_INPUT);
		reg_gpio_set_pull_up_down_enable(ROW_PINS_BASE+i);
		reg_gpio_set_pull_up_down(ROW_PINS_BASE+i, up_down);
	}

	for (i = 0; i < NUM_COLS; i++) {
		reg_gpio_iotr_set_pin_type(COL_PINS_BASE+i, GPIO_PIN_TYPE_INPUT);
		reg_gpio_set_pull_up_down_enable(COL_PINS_BASE+i);
		reg_gpio_set_pull_up_down(COL_PINS_BASE+i, up_down);
	}
}

static void do_power_off(void)
{

	printk("shut down the system\n");

	reg_gpio_iotr_set_pin_type(HW_GPIO0_PIN_MAX+29, GPIO_PIN_TYPE_OUTPUT);
	reg_gpio_set_pull_up_down_disable(HW_GPIO0_PIN_MAX+29);	
	reg_gpio_set_pin(HW_GPIO0_PIN_MAX+29, 0);

	udelay(10);

	//produce a clock ,then release the power hold
	reg_gpio_iotr_set_pin_type(HW_GPIO0_PIN_MAX+30, GPIO_PIN_TYPE_OUTPUT);
	reg_gpio_set_pull_up_down_disable(HW_GPIO0_PIN_MAX+30);	
	reg_gpio_set_pin(HW_GPIO0_PIN_MAX+30, 0);

	udelay(10);

	reg_gpio_set_pin(HW_GPIO0_PIN_MAX+30, 1);

	udelay(10);

	reg_gpio_set_pin(HW_GPIO0_PIN_MAX+30, 0);

	//oh ,waiting to dead.............
	while(1);
}


//the whole system enter sleep 
static void kb_sleep(void)
{
	int err;
	struct siginfo sinfo;
	struct task_struct *task;

	printk("no any key being pressed for a long time,go to sleep..\n");

	kb_enter_sleep_counter = 0;   //recover the initial value
	kb_would_sleep_flag = 0;

	if ((!kb_client)  && (kb_client_pid == -1))
		return ;
	
	for_each_process(task)
	{
		if ((task->pid == kb_client_pid) && ( task == kb_client ))
			goto cont;
	}

	return;
cont:

#if 0	
	sinfo.si_ptr = (void *)kmalloc(sizeof(struct pos_signal),GFP_ATOMIC);
	if (!sinfo.si_ptr)
	{
        	printk("kmalloc faild\n");
		return;
	}
#endif	
	
	send_sig_info(SIGUNUSED, &sinfo,kb_client);   
#if 0
	err = pm_suspend(PM_SUSPEND_MEM);
	if (err < 0)
		printk("keyboard cann't make the system go to sleep\n");
#endif

	return ;
}

#define SCAN_DELAY		20

static void scan_keyboard(void)
{
	int row_temp;
	unsigned status_temp = 0;
	int i;
	int temp ,old_c_value, old_d_value;
	int next_pos = 0;

	int GPIO_value;

	kb_count++;

	if (wait_to_dead == 1)
	{         
		//now,waiting to shutdown
		if ( kb_count - power_count  >  POWER_DEAD_DELAY)
		{
			do_power_off();
			return ;  //return directly.don't scan the keyboard again
		}	
		else
		{
			goto out1;
		}	
	} 

#if 0	
	if ((power_switch_flag == 1) && (kb_count - on_off_counter >  POWER_CONFIRM_DELAY))
	{
		//now ,we will prepare to shut_down
		prepare_power_off();
		goto out1;        //for next key board scan,since then 
	}	
#endif	

#if 0
	//on/off key pressed
	if ( !(__raw_readl(IO_ADDRESS(GIO1_REG_BASE_ADDR+0x800)) & (1<<ON_OFF_SWITCH_BIT)))	
	{
		if (on_off_pressed)
		{
			if  ( (kb_count - on_off_counter) > ON_OFF_MIN_DELAY * HZ /SCAN_INTERVAL)
			{
				printk("prepare to power off!\n");
				power_switch_flag = 1;

			//	prepare_power_off();
			}
		}
		else
		{

			on_off_pressed = 1;
			on_off_counter = kb_count;
		}

		goto out1;
	}
	else //released
	{	
		if (on_off_pressed)
		{
			on_off_pressed = 0;
			on_off_counter = 0;
			input_report_key(sam2416kb_dev,sam2416_keycode[KEY_NUM], key_down);
			input_sync(sam2416kb_dev);

			input_report_key(sam2416kb_dev,sam2416_keycode[KEY_NUM], key_up);
			input_sync(sam2416kb_dev);		
		}
	}
#endif	

	//old col  val
	old_d_value =  __raw_readl(IO_ADDRESS(GIO1_REG_BASE_ADDR+0x800)) | COL_B_OUTPUT_MASK;
	__raw_writel(old_d_value & ~(COL_B_OUTPUT_MASK), IO_ADDRESS(GIO1_REG_BASE_ADDR+0x804));

	udelay(5) ;

	temp =  __raw_readl(IO_ADDRESS(GIO1_REG_BASE_ADDR+0x800)) & ROW_VALUE_MASK_DEFAULT;

	/*
	* this is the status during most time,we return right now . 
	* this can save much time in most case. 
	*/
	if ( (last_status_map == IDEAL_KEY_STATUS) && (temp == ROW_VALUE_MASK_DEFAULT) )
	{
		//this means we have enabled the sleep function and there is no DC power
//		printk("DC_status : %d\n",DC_status);
//		if ((sleep_interval != 0) && (kb_would_sleep_flag == 0)) 
		if ((sleep_interval != 0) && (kb_would_sleep_flag == 0) && (DC_status == NO_PROVIED) ) 
		{	
			kb_enter_sleep_counter++;
			if ( kb_enter_sleep_counter > (sleep_interval*HZ)/SCAN_INTERVAL)
			{		
				PREPARE_WORK(&sys_sleep_worker, kb_sleep);
				schedule_work(&sys_sleep_worker);
				kb_would_sleep_flag = 1;
			}	
		}	
        	goto out; 
	}		


	//clear all the flag revlant to sleep
	kb_enter_sleep_counter = 0;
	kb_would_sleep_flag = 0;

//	printk("last_status_map:0x%x, temp :0x%x\n",last_status_map,temp);

	__raw_writel(old_d_value, IO_ADDRESS(GIO1_REG_BASE_ADDR+0x804));	//revcover the value of register ,disable the gpio output

	for (i=0; i<KEY_MAX_COLS; i++)  
	{       
	 	//enalbe only one col line ouput 
	 	__raw_writel(old_d_value & ~(1<<(i+FIRST_C_COLS_POS)), IO_ADDRESS(GIO1_REG_BASE_ADDR+0x804));                
	
		udelay(5);

		row_temp = __raw_readl(IO_ADDRESS(GIO1_REG_BASE_ADDR+0x800))  & row_value_mask[i];
	
//		printk("col: %d\n", i);
//		printk("row_temp is 0x%x\n",row_temp);
		status_temp |= ((row_temp >> first_valid_bit[i]) << next_pos); 

		next_pos += valid_bits[i];
	}

	//if no key status changed   
	if ( status_temp  ==  last_status_map )
		goto out;
        
//	printk("last_s:0x%x,now_s:0x%x\n",last_status_map,status_temp);	
    //if some key status is changed   
	for(i=0; i<KEY_NUM; i++)
	{
		temp = (status_temp & (1<<i));
		if ((temp) == (last_status_map & (1<<i)))   //this key status hasn't changed
			continue;
	   
	   //this key status has changed
//	   printk("row:%d,col:%d\n",i%KEY_MAX_ROWS,i/KEY_MAX_ROWS);
//		input_regs(sam2416kb_dev, NULL);
		input_report_key(sam2416kb_dev,sam2416_keycode[i], (temp == 0) ? key_down : key_up);
		input_sync(sam2416kb_dev);
	}    
    
	last_status_map = status_temp;
    
out:    
//	__raw_writel(old_c_value, IO_ADDRESS(GIO1_REG_BASE_ADDR+0x804));    //revcover the value of register ,disable the gpio output	
	__raw_writel(old_d_value, IO_ADDRESS(GIO1_REG_BASE_ADDR+0x804));    //revcover the value of register ,disable the gpio output

//	__raw_writel(old_c_value, S3C2410_GPCDAT);    //revcover the value of register ,disable the gpio output	
//	__raw_writel(old_d_value, S3C2410_GPDDAT);    //revcover the value of register ,disable the gpio output	

out1:		
    //some key status changed
	raise_mytimer(SCAN_INTERVAL);   //for the  next scan of the keyboard
}

static void raise_mytimer(int interval)
{
	init_timer(&my_timer);
     
	my_timer.expires = jiffies + interval;     
	my_timer.function = (void *)scan_keyboard;
     
	add_timer(&my_timer);
}

#endif

#if 1
static void hardware_init(void)
{
	int i;

	//on/off switch
	reg_gpio_iotr_set_pin_type(HW_GPIO0_PIN_MAX+1, GPIO_PIN_TYPE_INPUT);
	reg_gpio_set_pull_up_down_disable(HW_GPIO0_PIN_MAX+1);

	//row
	for(i=16;i<20;i++)
	{
		reg_gpio_iotr_set_pin_type(HW_GPIO0_PIN_MAX+i, GPIO_PIN_TYPE_INPUT);
		reg_gpio_set_pull_up_down_disable(HW_GPIO0_PIN_MAX+i);
	}

	//col
	for(i=20;i<25;i++)
	{
		reg_gpio_iotr_set_pin_type(HW_GPIO0_PIN_MAX+i, GPIO_PIN_TYPE_OUTPUT);
		reg_gpio_set_pull_up_down_disable(HW_GPIO0_PIN_MAX+i);
		reg_gpio_set_pin(HW_GPIO0_PIN_MAX+i, 1);		
	}	

}

#endif

static int shut_down_proc_write_kb(struct file *file,const char *buffer, unsigned long count, void *data)
{
 //   printk("here0\n");

 	if (count != 1) 
		return -EINVAL;

	if(copy_from_user(shut_down_proc->data,buffer,count))
		return -EFAULT;
	
 //   printk("here1\n")	
	if ( memcmp("y",shut_down_proc->data,count) == 0 )
	{
	//	do_power_off();
	}
	else
	{
		return -EINVAL;
	}
    
    return count;
} 

static inline int _isdigit(char a)
{
	if ((a >= '0') && (a <='9' ))
		return 1;
	else
		return 0;
}

//only change the digital nmuber.
static int my_atoi(const char *str)
{
	int v=0;
	int sign = 0;

	while ( *str == ' ')  
		str++;
	if(*str == '-'||*str == '+')
		sign = *str++;

	while (_isdigit(*str))
	{
		v = v*10 + *str - '0';
		str++;
	}

	return sign == '-' ? -v:v;
}

static int sleep_interval_proc_write_kb(struct file *file,const char *buffer, unsigned long count, void *data)
{
 //   printk("here0\n");
 	if (count >5) 
		return -EINVAL;
 
	memset(shut_down_proc->data,0,10);

	if(copy_from_user(shut_down_proc->data,buffer,count))
		return -EFAULT;
	
 //   printk("here1\n")	
	sleep_interval = my_atoi(shut_down_proc->data);
	
	printk("interval time for kb sleep is %d\n",sleep_interval);

	kb_client = current;
	kb_client_pid = current->pid;
#if 0
	kb_sleep();
#endif
    
    	return count;
}
#if 0
static  int kb_proc_init(void)
{
	int ret =0;

	shut_down_proc = create_proc_entry("shut_down",0666,stronglion_root);

	if (shut_down_proc == NULL)
	{
		printk("registering proc entry of shut down key failed,you will can't enable the shut down by softeware\n");
		goto out;		
	}
   
	shut_down_proc->data = &kb_proc_buf;
	shut_down_proc->write_proc = &shut_down_proc_write_kb;
	shut_down_proc->owner = THIS_MODULE;

	sleep_interval_proc = create_proc_entry("kb_sleep_interval",0666,stronglion_root);

	if (sleep_interval_proc == NULL)
	{
		printk("registering proc entry of keyboard sleep interval failed,you can't enter the sleep status by keyboard");
		goto bad_out1;		
	}
   
	sleep_interval_proc->data = &kb_proc_buf;
	sleep_interval_proc->write_proc = &sleep_interval_proc_write_kb;
	sleep_interval_proc->owner = THIS_MODULE;		

	goto out;

bad_out1:
	remove_proc_entry("shut_down",stronglion_root);

out:
	return ret ;
}
#endif
static void kb_proc_cleanup(void)
{	
	remove_proc_entry("kb_sleep_interval",stronglion_root);
	remove_proc_entry("shut_down",stronglion_root);
}

static int __init sam2416kb_init(void)
{
	int i;
	printk("sam2416 keyboard driver. version :1.0\n");

//	kb_proc_init();
	
	sam2416kb_dev = input_allocate_device();
	if (!sam2416kb_dev) 
	{
		printk(KERN_ERR "sam2416kb: not enough memory for input device\n");
		return -ENOMEM;
	}

	sam2416kb_dev->name = "sam2416 Keyboard";
	sam2416kb_dev->phys = "sa2416l/input2";
	sam2416kb_dev->id.bustype = BUS_AMIGA;
	sam2416kb_dev->id.vendor = 0x0001;
	sam2416kb_dev->id.product = 0x0001;
	sam2416kb_dev->id.version = 0x0100;

//	sam2416kb_dev->evbit[0] = BIT(EV_KEY) | BIT(EV_REP);
	sam2416kb_dev->evbit[0] = BIT(EV_KEY) ;
	sam2416kb_dev->keycode = sam2416_keycode;
	sam2416kb_dev->keycodesize = sizeof(unsigned char);
	sam2416kb_dev->keycodemax = ARRAY_SIZE(sam2416_keycode);

	for (i = 0; i < KEY_NUM+1; i++)
		if (sam2416_keycode[i])
			set_bit(sam2416_keycode[i], sam2416kb_dev->keybit);

	last_status_map = status_map_mask = IDEAL_KEY_STATUS;
	printk("firstly ,last_status_map: 0x%x\n", last_status_map);

	hardware_init();
//	while(1);

	input_register_device(sam2416kb_dev);

	raise_mytimer(SCAN_INTERVAL);
	
	return 0;
}

static void __exit sam2416kb_exit(void)
{
//	kb_proc_cleanup();
//	all_gpio_to_inputs_with_pull(0);

	input_unregister_device(sam2416kb_dev);
}

module_init(sam2416kb_init);
module_exit(sam2416kb_exit);

MODULE_AUTHOR("lxyvslyr <lxyvslyr@yahoo.com.cn>");
MODULE_DESCRIPTION("sam2416 keyboard driver");
MODULE_LICENSE("GPL");


