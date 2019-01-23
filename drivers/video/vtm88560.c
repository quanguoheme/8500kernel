/*
 * linux/drivers/video/vtm8560.c
 *
 *	 * Copyright (C) 2006-2007 TTI-china, Inc.
 *
 *      author: LiJianJun <lxyvslyr@yahoo.com.cn>
 *
 *         the low level drive of vtm8560 based on za9l,
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 *
 * 
 */ 
#include <linux/module.h> 
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/init.h>

#ifdef CONFIG_PM
#include <linux/pm.h>
#endif

//#include <asm/arch/regs-gpio.h>
#include <asm/io.h>
//#include <asm/arch/map.h>

#include "s3c2416fb.h"
#include "vtm88560.h"

#include <mach/reg_gpio.h>
#include <mach/hardware.h>



static unsigned char lcd_enable = 0;     //lcd enable is on?
static int last_addr = -100;// the data address of last write

static void lcd_setcursor(int addr);

static unsigned long backlight_status = BACKLIGHT_OFF;

static unsigned long lcd_virtual_addr;

static unsigned long lcd_cmd_addr;
static unsigned long lcd_data_addr;

static unsigned long now_constrast = 0;


//change by lee
static inline void lcd_cs(int status)
{
	//8500 GPC22
	int temp=0;
	if (status)
	{

		reg_gpio_set_pin( BCM5892_GPC22, 1 );
	}
	else
	{

		reg_gpio_set_pin( BCM5892_GPC22, 0 );

	}
}

//change by lee
static inline void lcd_cd(int type)
{
	int temp=0;
	//8500 GPC20
	if (type)
		{
		
		//	temp = __raw_readl(IO_ADDRESS (HW_GPIO2_PHY_BASE  + 0x800 + REGOFFSET_GPIO_IOTR));							
		//	__raw_writel( (temp | (1<<20)),(IO_ADDRESS (HW_GPIO2_PHY_BASE +0x800 + REGOFFSET_GPIO_DOUT)));
			reg_gpio_set_pin( BCM5892_GPC20, 1 );

		}
	else
		{

			reg_gpio_set_pin( BCM5892_GPC20, 0 );

		}
}

static inline void lcd_enable_op(void)
{
	if  (lcd_enable)
		return;
	
	lcd_cs(SELECT_LCD);

	lcd_enable = 1;                    //lcd enable
}

static inline void lcd_disable_op(void)
{
	if  (!lcd_enable)    
		return;    

	lcd_cs(DISELETT_LCD);
	lcd_enable = 0;                        //lcd disable
}

//change by lee
inline static void _write(char A)
{
	int i,p;
	int temp=0;

	ndelay(25);
	
	for(i=0;i<8;i++)
	{
		//clk is low
		//8500 GPC25


		reg_gpio_set_pin( BCM5892_GPC25, 0 );

		ndelay(5);
		
		if (A & (0x1<<(7-i)))
		{
			//8500 GPC24
			reg_gpio_set_pin( BCM5892_GPC24, 1 );


		}
		else
		{
			reg_gpio_set_pin( BCM5892_GPC24, 0 );

		}
		//delay 25ns
		ndelay(25);

		//clk is high ,lock the data
		reg_gpio_set_pin( BCM5892_GPC25, 1 );

		//delay 25ns
		ndelay(25);
	}
   
}

static void lcd_write_data(int addr ,unsigned char* wbuffer) 
{     
	int i;

//       printk("last_addr:%x ,addr:%x, val:%c\n",last_addr,addr);	

//       printk("addr is %d\n",addr);
       
	if ((addr %16) == 0)
		goto set_cursor;

	//the lcd inc the addr autoly	          
	if (last_addr == (addr-1))
	{
		last_addr++; 
		goto conti_write;
	}
      
set_cursor:
	last_addr = addr;
	lcd_setcursor(addr);	

conti_write:

	lcd_enable_op();
	ndelay(10);

	lcd_cd(LCD_DATA);
	
	for (i=0; i<8; i++)
		_write(*(wbuffer+i));

	ndelay(10);  
	lcd_disable_op();
}

static void lcd_write_cmd(int cmd)
{
	ndelay(10);
	lcd_enable_op();
	ndelay(10);
	
	lcd_cd(LCD_CMD);

	_write(cmd);

	lcd_disable_op();
}

static void clean_icon(void)
{
	int i,j,k;

	lcd_enable_op();
	SetPage(8);

	for (i=0;i<16;i++)
	{
		for(j=0;j<8;j++)
		{
			SetColL( i*8 +j );
			SetColU( i*8 +j );

			lcd_enable_op();
			ndelay(10);
			lcd_cd(LCD_DATA);

			_write(0x0);
//			_write(0xff);
		}
	}
	
	lcd_setcursor(last_addr);  //recover the cursor.
	lcd_disable_op();	
}

static void clear_lcd(void)
{
	int i ;
//	unsigned char zero_buf[8]={0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff};
	unsigned char zero_buf[8]={0,0,0,0,0,0,0,0};
    
	//clean the icon	
	for(i=0;i<16*8;i++)
	{
		lcd_write_data(i,zero_buf);
	}
	
	clean_icon();
}

static void display_all(void)
{
	int com = 64;
	int seg = 128;
	int i,j;

	for (i=0; i<com/8; i++)
	{
		SetPage(i);
		SetColL(2);		
		SetColU(0);

		lcd_enable_op();
		ndelay(20);

		lcd_cd(LCD_DATA);
		ndelay(100);
		
		for(j=0; j<seg; j++)
		{
			ndelay(100);
			_write(0xff);
		}
		
		ndelay(20);  
		lcd_disable_op();	
	}

}

//change by lee
static void hardware_wakeup_init(void)
{

    //LCD_CS
    //8500 GPC22
	reg_gpio_set_pull_up_down_disable(BCM5892_GPC22);									/* pull-up/down disable */
	reg_gpio_iotr_set_pin_type(BCM5892_GPC22,GPIO_PIN_TYPE_OUTPUT); 					//output
	reg_gpio_set_pin( BCM5892_GPC22, 1);												/* out 1 */
	

	//LCD_RES
	//8500 GPC21
	reg_gpio_set_pull_up_down_disable(BCM5892_GPC21);									/* pull-up/down disable */
	reg_gpio_iotr_set_pin_type(BCM5892_GPC21,GPIO_PIN_TYPE_OUTPUT); 					//output
	reg_gpio_set_pin( BCM5892_GPC21, 1);												/* out 1 */
	
	//LCD_CD
	//8500 GPC20
	reg_gpio_set_pull_up_down_disable(BCM5892_GPC20);									/* pull-up/down disable */
	reg_gpio_iotr_set_pin_type(BCM5892_GPC20,GPIO_PIN_TYPE_OUTPUT); 					//output
	reg_gpio_set_pin( BCM5892_GPC20, 1);												/* out 1 */


	//LCD_SCK
	//8500 GPC25
	reg_gpio_set_pull_up_down_disable(BCM5892_GPC25);									/* pull-up/down disable */
	reg_gpio_iotr_set_pin_type(BCM5892_GPC25,GPIO_PIN_TYPE_OUTPUT); 					//output
	reg_gpio_set_pin( BCM5892_GPC25, 1);												/* out 1 */
	
	//LCD_SDA
	//8500 GPC24
	reg_gpio_set_pull_up_down_disable(BCM5892_GPC24);									/* pull-up/down disable */
	reg_gpio_iotr_set_pin_type(BCM5892_GPC24,GPIO_PIN_TYPE_OUTPUT); 					//output
	reg_gpio_set_pin( BCM5892_GPC24, 1);												/* out 1 */

	//LCD_BACK_LIGHT
	//8500	GPC23
	reg_gpio_set_pull_up_down_disable(BCM5892_GPC23);									/* pull-up/down disable */
	reg_gpio_iotr_set_pin_type(BCM5892_GPC23,GPIO_PIN_TYPE_OUTPUT); 					//output
	reg_gpio_set_pin( BCM5892_GPC23, 1);												/* out 1 */

	///LCD_RES
	//8500 GPC21
	reg_gpio_set_pin( BCM5892_GPC21, 0);
	mdelay(1);
	reg_gpio_set_pin( BCM5892_GPC21, 1);

	
	// Initialization commands of LCD module.
	lcd_enable_op();
	mdelay(1);

	//power setup
	SetPowerCtl(0x4);
	mdelay(100);
	SetPowerCtl(0x6);
	mdelay(100);
	SetPowerCtl(0x7);
	mdelay(100);

	//setup brightness of lcd
	SetV0Rate(0x7);

	//setup backligt 
	SetV0OutVolmue(now_constrast);

	SetbooterRatio(0);

	SetDispLine(0);

	LcdWriteAutoInc();

	//right or left
//	SetADCselect(1);

	//up or down
	SetComCtrl(1);

	SetPage(0);
	SetColU(0);
	SetColL(0);

	last_addr = 0;

	SetDisEnable(1);

	lcd_disable_op();
	
}

//change by lee
static void hardware_init(struct fb_info *info)
{
	int i,j,p=0;
       unsigned char buf[8] = {0x1c,0x3c,0x6c,0xcc,0xfe,0x0c,0x1e,0x00};

	//LCD_CS
	//8500 GPC22
	reg_gpio_set_pull_up_down_disable(BCM5892_GPC22); 									/* pull-up/down disable */
	reg_gpio_iotr_set_pin_type(BCM5892_GPC22,GPIO_PIN_TYPE_OUTPUT); 					//output
	reg_gpio_set_pin( BCM5892_GPC22, 1);												/* out 1 */
	

	//LCD_RES
	//8500 GPC21
	reg_gpio_set_pull_up_down_disable(BCM5892_GPC21); 									/* pull-up/down disable */
	reg_gpio_iotr_set_pin_type(BCM5892_GPC21,GPIO_PIN_TYPE_OUTPUT); 					//output
	reg_gpio_set_pin( BCM5892_GPC21, 1);												/* out 1 */
	
	//LCD_CD
	//8500 GPC20
	reg_gpio_set_pull_up_down_disable(BCM5892_GPC20); 									/* pull-up/down disable */
	reg_gpio_iotr_set_pin_type(BCM5892_GPC20,GPIO_PIN_TYPE_OUTPUT); 					//output
	reg_gpio_set_pin( BCM5892_GPC20, 1);												/* out 1 */


	//LCD_SCK
	//8500 GPC25

	 									/* pull-up/down disable */

	reg_gpio_set_pull_up_down_disable(BCM5892_GPC25);
	reg_gpio_iotr_set_pin_type(BCM5892_GPC25,GPIO_PIN_TYPE_OUTPUT); 					//output
	reg_gpio_set_pin( BCM5892_GPC25, 1);	/* out 1 */
	
	//LCD_SDA
	//8500 GPC24

	reg_gpio_set_pull_up_down_disable(BCM5892_GPC24); 									/* pull-up/down disable */
	reg_gpio_iotr_set_pin_type(BCM5892_GPC24,GPIO_PIN_TYPE_OUTPUT); 					//output
	reg_gpio_set_pin( BCM5892_GPC24, 1);												/* out 1 */

	//LCD_BACK_LIGHT
	//8500  GPC23
	reg_gpio_set_pull_up_down_disable(BCM5892_GPC23); 									/* pull-up/down disable */
	reg_gpio_iotr_set_pin_type(BCM5892_GPC23,GPIO_PIN_TYPE_OUTPUT); 					//output
	reg_gpio_set_pin( BCM5892_GPC23, 1);												/* out 1 */

	///LCD_RES
	//8500 GPC21
	reg_gpio_set_pin( BCM5892_GPC21, 0);
	mdelay(1);
	reg_gpio_set_pin( BCM5892_GPC21, 1);

	// Initialization commands of LCD module.
	lcd_enable_op();
	mdelay(1);

	//power setup
	SetPowerCtl(0x4); //0x2c
	mdelay(100);
	SetPowerCtl(0x6); //0x2e
	mdelay(100);
	SetPowerCtl(0x7); //0x2f
	mdelay(100);

	//setup brightness of lcd
	SetV0Rate(0x7); //0x27

	//setup backligt 
	SetV0OutVolmue(0xff); //0x81,0x3f
	now_constrast = 0xff;

//	SetbooterRatio(0);

	SetDispLine(0); //0x40

	LcdWriteAutoInc(); //0xe0

	//right or left
//	SetADCselect(1);

	//no static indictor
//	SetStaticIndictor(1);
//	SetStaticIndictorMod(0);

	//up or down
	SetComCtrl(1); //0xc8

	SetPage(0); //0xb0
	SetColU(0); //0x10
	SetColL(0); //0x00

	SetDisEnable(1); //0xaf
 
//	SetAllOn(1);
//	display_all();
//	while(1);

	lcd_disable_op();
	
	clear_lcd();      //clear the lcd 
	printk("lcd vtm88560 initialtion finished\n");
#if 0	
//	lcd_backlight(BACKLIGHT_ON);
	printk("LCD test\n");
	lcd_write_data(0,buf);
	lcd_write_data(10,buf);
	lcd_write_data(20,buf);
	lcd_write_data(30,buf);
	
	while(1);
#endif	
}

static inline int flush_icon(struct fb_info *fbi ,int forced)
{
	struct s3c2416fb_info *zfbi = fbi->par;
	int icon_offset = 1024 ; //16*8  * 8 * 8 
	int i,j;
	char tmp;

	if ((memcmp(zfbi->last_lcd_buffer+icon_offset, zfbi->lcd_buffer+icon_offset ,16) == 0) && !forced)
		return 0;  //no change

	lcd_enable_op(); 
	SetPage(8);
	
	for (i=0;i<16;i++)
	{
		if(( *(zfbi->lcd_buffer+icon_offset +i) == *(zfbi->last_lcd_buffer+icon_offset+i) ) && !forced)
			continue;

		for(j=0;j<8;j++)
		{	   
			SetColL(i*8 +j );
			SetColU(i*8 +j );
//			tmp = ( *(zfbi->lcd_buffer+icon_offset +i) & (1<<(7-j)) ) ? 0 : 0x1 ;
			tmp = ( *(zfbi->lcd_buffer+icon_offset +i) & (1<<(7-j)) ) ? 0x01 : 0 ;

			lcd_enable_op();
			ndelay(10);
			lcd_cd(LCD_DATA);

			_write(tmp);
		}
	}
	
	lcd_setcursor(last_addr);  //recover the cursor.
	lcd_disable_op();
	
	return 1;
}

static void vtm88560_flush(struct fb_info *fbi,int forced)
{
      int bf_flag=0; 
      int i;
          
      int j,k,l,page_start,block_start;
      unsigned char temp;
      unsigned char buffer[8];   

//	printk("vtm88560_flush\n");
	  
      struct s3c2416fb_info *zfbi = fbi->par;
 
      for (i=0;i<8;i++)   //8 page (8*16 byte)
      {
		page_start = i * 128;  
		    
            	for(j=0;j<16;j++)   //16 block every one page (8byte)
		{
		        block_start = page_start +j;
			
			for(k=0;k<8;k++)    //8 byte every one block
			{
			    if ((*(zfbi->lcd_buffer+(block_start+k*16)) != *(zfbi->last_lcd_buffer+(block_start+k*16))) || forced)
			    {
				        //,the block have changed since last scaning , flush this block
				        memset(buffer,0,8);
#if 0				
					printk("block, i:%d, j:%d, k:%d\n",i,j,k);
					for (l=0;l<8;l++)
					    printk("%02x ",*(zfbi->lcd_buffer+(block_start+l*16)));
					printk("\n");
					//while(1);    
#endif		
					for(l=0;l<8;l++)
					{
						temp = 0;
					
						temp |= ((*(zfbi->lcd_buffer+(block_start+0*16)) & (1<<(7-l))) ? 0x1 : 0 );
						temp |= ((*(zfbi->lcd_buffer+(block_start+1*16)) & (1<<(7-l))) ? 0x2 : 0 );
						temp |= ((*(zfbi->lcd_buffer+(block_start+2*16)) & (1<<(7-l))) ? 0x4 : 0 );
						temp |= ((*(zfbi->lcd_buffer+(block_start+3*16)) & (1<<(7-l))) ? 0x8 : 0 );
						temp |= ((*(zfbi->lcd_buffer+(block_start+4*16)) & (1<<(7-l))) ? 0x10: 0 );
						temp |= ((*(zfbi->lcd_buffer+(block_start+5*16)) & (1<<(7-l))) ? 0x20: 0 );
						temp |= ((*(zfbi->lcd_buffer+(block_start+6*16)) & (1<<(7-l))) ? 0x40: 0 );
						temp |= ((*(zfbi->lcd_buffer+(block_start+7*16)) & (1<<(7-l))) ? 0x80: 0 );
					   
						buffer[l] = temp;
					}

#if 0				
					printk("buf is: ");
					for (l=0;l<8;l++)
					    printk("%2x ",buffer[7-l]);
					printk("\n");
					while(1);    
#endif	    
					lcd_write_data(i*16+j,buffer);
					bf_flag = 1;   
					
					break;  //jump to next block;
			    }    
			}     
		}
	}       

	if ( flush_icon(fbi,forced) > 0 )
	{
	//	printk("icon changed\n");
		bf_flag = 1; 
	}	
	
      //if no lcd pixel changed from last flush ,do nothing and exit
	if (bf_flag == 0)  
		goto out;
     
	for (i=0; i<zfbi->map_size; i++)
		*(zfbi->last_lcd_buffer+i) = *(zfbi->lcd_buffer+i);
out:      
	return;  
}

static void lcd_setcursor(int addr)
{
	unsigned int page ,col;
	
	page = addr/16 ;
	col = (addr%16) * 8;//?

//	printk("page : %d\n", page);
//	printk("col : %d\n", col);
	
	SetPage(page);
	SetColL(col);
	SetColU(col);
}

static void vtm88560_set_backlight(struct fb_info *fbi,BACKLIGHT_STATUS stause)
{
	if ( stause == BACKLIGHT_OFF )
	{
		//8500 GPC23
 
		reg_gpio_set_pin( BCM5892_GPC23, 0);		/* out 0 */      //turn off the backlight
	    backlight_status =  BACKLIGHT_OFF;
	}    
	else
	{
 	 	
		reg_gpio_set_pin( BCM5892_GPC23, 1);  /* out 1 */      //turn on the backlight
	    backlight_status = BACKLIGHT_ON;
	}
}
 
static void vtm88560_set_constrast(struct fb_info *fbi,unsigned long contrast)
{ 
	unsigned long temp;
	 
	if (contrast > MIN_CONTRAST)
	     contrast = MIN_CONTRAST;
	     
	temp = MIN_CONTRAST - contrast;

	lcd_enable_op();
	    	
	SetV0OutVolmue(temp);
	now_constrast = temp;
	
	lcd_disable_op(); 
}

#if 0
static void vtm88560_sleepdown(void)
{
	s3c2410_gpio_setpin(S3C2410_GPE10, 0); 	/* out 0 */      //turn off the backlight 	  
	
	lcd_enable_op();
	SetDisEnable(0);
	lcd_disable_op();
}

static void vtm88560_wakeup(void)
{
	s3c2410_gpio_setpin(S3C2410_GPE10, 1); 	/* out 1 */      //turn on the backlight 	  
		
	lcd_enable_op();
	SetDisEnable(1);
	lcd_disable_op();
	
}
#endif

void vtm88560_sleepdown(void)
{
	printk("vtm88560_sleepdown\n");
	//8500 GPC23
	//s3c2410_gpio_setpin(S3C2410_GPE10, 0); 	/* out 0 */      //turn off the backlight 	
	reg_gpio_set_pin( BCM5892_GPC23, 0);

	lcd_enable_op();
	SetStaticIndictor(0);

	SetDisEnable(0);

	SetAllOn(1);
	lcd_disable_op();
	
}

void vtm88560_wakeup(void)
{ 
	printk("vtm88560_wakeup\n");

	hardware_wakeup_init();

	lcd_enable_op();
	SetAllOn(0);

	SetStaticIndictor(1);

	SetStaticIndictorMod(0);
	lcd_disable_op();
	//8300 GPC23
	//s3c2410_gpio_setpin(S3C2410_GPE10, 1); 	/* out 1 */      //turn on the backlight
	reg_gpio_set_pin( BCM5892_GPC23, 1);
}

void vtm88560_init(struct fb_info *info)
{
       struct s3c2416fb_info *fbi = info->par;

       printk("LCD vtm88560  init ...\n");
     
       hardware_init(info);
     
//     printk("fbi->lcd_dev:0x%x\n",fbi->lcd_dev);
//     printk("fbi->lcd_dev->lcd_driver:0x%x\n",fbi->lcd_dev->lcd_driver);
//     while(1);
       fbi->lcd_dev->lcd_driver->flush_fb      = vtm88560_flush;
       fbi->lcd_dev->lcd_driver->sleep_down    = vtm88560_sleepdown;
       fbi->lcd_dev->lcd_driver->fbwake_up      = vtm88560_wakeup;
       fbi->lcd_dev->lcd_driver->set_backlight = vtm88560_set_backlight;
       fbi->lcd_dev->lcd_driver->set_contrast  = vtm88560_set_constrast;
   
     return;
}



