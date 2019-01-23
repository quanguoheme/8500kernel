/*****************************************************************************
 * Copyright (c) 2009 Broadcom Corporation.  All rights reserved.
 *
 * This program is the proprietary software of Broadcom Corporation and/or
 * its licensors, and may only be used, duplicated, modified or distributed
 * pursuant to the terms and conditions of a separate, written license
 * agreement executed between you and Broadcom (an "Authorized License").
 * Except as set forth in an Authorized License, Broadcom grants no license
 * (express or implied), right to use, or waiver of any kind with respect to
 * the Software, and Broadcom expressly reserves all rights in and to the
 * Software and all intellectual property rights therein.  IF YOU HAVE NO
 * AUTHORIZED LICENSE, THEN YOU HAVE NO RIGHT TO USE THIS SOFTWARE IN ANY
 * WAY, AND SHOULD IMMEDIATELY NOTIFY BROADCOM AND DISCONTINUE ALL USE OF
 * THE SOFTWARE.
 *
 * Except as expressly set forth in the Authorized License,
 * 1. This program, including its structure, sequence and organization,
 *    constitutes the valuable trade secrets of Broadcom, and you shall use
 *    all reasonable efforts to protect the confidentiality thereof, and to
 *    use this information only in connection with your use of Broadcom
 *    integrated circuit products.
 * 2. TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 *    AND WITH ALL FAULTS AND BROADCOM MAKES NO PROMISES, REPRESENTATIONS OR
 *    WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH
 *    RESPECT TO THE SOFTWARE.  BROADCOM SPECIFICALLY DISCLAIMS ANY AND ALL
 *    IMPLIED WARRANTIES OF TITLE, MERCHANTABILITY, NONINFRINGEMENT, FITNESS
 *    FOR A PARTICULAR PURPOSE, LACK OF VIRUSES, ACCURACY OR COMPLETENESS,
 *    QUIET ENJOYMENT, QUIET POSSESSION OR CORRESPONDENCE TO DESCRIPTION. YOU
 *    ASSUME THE ENTIRE RISK ARISING OUT OF USE OR PERFORMANCE OF THE SOFTWARE.
 * 3. TO THE MAXIMUM EXTENT PERMITTED BY LAW, IN NO EVENT SHALL BROADCOM OR ITS
 *    LICENSORS BE LIABLE FOR (i) CONSEQUENTIAL, INCIDENTAL, SPECIAL, INDIRECT,
 *    OR EXEMPLARY DAMAGES WHATSOEVER ARISING OUT OF OR IN ANY WAY RELATING TO
 *    YOUR USE OF OR INABILITY TO USE THE SOFTWARE EVEN IF BROADCOM HAS BEEN
 *    ADVISED OF THE POSSIBILITY OF SUCH DAMAGES; OR (ii) ANY AMOUNT IN EXCESS
 *    OF THE AMOUNT ACTUALLY PAID FOR THE SOFTWARE ITSELF OR U.S. $1, WHICHEVER
 *    IS GREATER. THESE LIMITATIONS SHALL APPLY NOTWITHSTANDING ANY FAILURE OF
 *    ESSENTIAL PURPOSE OF ANY LIMITED REMEDY.
 *****************************************************************************/


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/time.h>
#include <linux/rtc.h>
#include <linux/interrupt.h>
#include <linux/ioctl.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <mach/dmu.h>
#include <mach/pmb.h>
#include <mach/platform.h>
#include <asm/cacheflush.h>
#include <mach/shm.h>]

#include <mach/bcm5892_reg.h>
#include <mach/regaccess.h>
#include "rtc-bcm5892.h"

//added by lee
#define BBL_WRITE_INDREG         0x61
#define BBL_32KHZ_PERIOD         320
#define BBL_SOFT_RST_BBL         0x81
#define BBL_READ_INDREG          0x21

#define BBL_RTC_STOP             0x0
#define BBL_RTC_START            0x1


static struct rtc_device *rtc_g;
static DEFINE_MUTEX(bcm5892_rtc_mutex);


/*
 *added base on stbl by lee 
 */

//cls_rtc_getper
//cls_rtc_setper
//cls_rtc_aie_on_off
//cls_rtc_getalarm

/*
 *Periodic int. disable off
 */
void cls_rtc_pie_on_off(int *enable)
{
	uint32_t addr, val, status;

	if (*enable)
		val = 1 << BBL1_F_bbl_period_intr_en_R;
	else
		val = 0 << BBL1_F_bbl_period_intr_en_R;

	addr = BBL0_R_BBL_ACCDATA_MEMADDR;
//	CPU_WRITE_SINGLE(IO_ADDRESS(addr), val, AHB_BIT32);
	__raw_writel(val,IO_ADDRESS(addr));

	val = (BBL1_R_BBL_INT_EN_SEL << BBL0_F_indaddr_R) | (BBL_WRITE_INDREG);
	addr = BBL0_R_BBL_CMDSTS_MEMADDR;
//	CPU_WRITE_SINGLE(IO_ADDRESS(addr), val, AHB_BIT32);
	__raw_writel(val,IO_ADDRESS(addr));
	do {
//		status = CPU_READ_SINGLE(IO_ADDRESS(addr), AHB_BIT32);
		status = __raw_readl(IO_ADDRESS(addr));
	} while ((status & BBL0_F_rdyn_go_MASK));
}


/*
 * 设置周期波特率
 */

void cls_rtc_setper(uint32_t *interval)
{
	uint32_t val, addr, status;
	int ien;

	// Disable and clear the periodic interrupt
	ien = 0;
	cls_rtc_pie_on_off(&ien);
	val = BBL1_F_bbl_per_intr_clr_MASK;
	addr = BBL0_R_BBL_ACCDATA_MEMADDR;
//	CPU_WRITE_SINGLE(IO_ADDRESS(addr), val, AHB_BIT32);
	__raw_writel(val,IO_ADDRESS(addr));
	val = (BBL1_R_BBL_CLR_INT_SEL << BBL0_F_indaddr_R) | (BBL_WRITE_INDREG);
	addr = BBL0_R_BBL_CMDSTS_MEMADDR;
//	CPU_WRITE_SINGLE(IO_ADDRESS(addr), val, AHB_BIT32);
	__raw_writel(val,IO_ADDRESS(addr));
	do {
		//status = CPU_READ_SINGLE(IO_ADDRESS(addr), AHB_BIT32);
		  status =__raw_readl(IO_ADDRESS(addr));
	} while ((status & BBL0_F_rdyn_go_MASK));

	// set period
	addr = BBL0_R_BBL_ACCDATA_MEMADDR;
//	CPU_WRITE_SINGLE(IO_ADDRESS(addr), *interval, AHB_BIT32);
	__raw_writel(*interval,IO_ADDRESS(addr));
	// Indirect Reg Write to Register 0 (bbl_per)
	val = (BBL1_R_BBL_PER_SEL << BBL0_F_indaddr_R) | (BBL_WRITE_INDREG);
	addr = BBL0_R_BBL_CMDSTS_MEMADDR;
//	CPU_WRITE_SINGLE(IO_ADDRESS(addr), val, AHB_BIT32);
	__raw_writel(val,IO_ADDRESS(addr));
	do {
		//status = CPU_READ_SINGLE(IO_ADDRESS(addr), AHB_BIT32);
		 status = __raw_readl(IO_ADDRESS(addr));
	} while ((status & BBL0_F_rdyn_go_MASK));

}


/*
 *Read IRQ rate
 */
void cls_rtc_getper(uint32_t *interval)
{
	uint32_t val, addr, status;

	// Indirect Reg Write to Register 0 (bbl_per)
	val = (BBL1_R_BBL_PER_SEL << BBL0_F_indaddr_R) | (BBL_READ_INDREG);
	addr = BBL0_R_BBL_CMDSTS_MEMADDR;
//	CPU_WRITE_SINGLE(IO_ADDRESS(addr), val, AHB_BIT32);
	__raw_writel(val,IO_ADDRESS(addr));
	do {
		//status = CPU_READ_SINGLE(IO_ADDRESS(addr), AHB_BIT32);
		  status =__raw_readl(IO_ADDRESS(addr));
	} while ((status & BBL0_F_rdyn_go_MASK));

	addr = BBL0_R_BBL_ACCDATA_MEMADDR;
//	*interval = CPU_READ_SINGLE(IO_ADDRESS(addr), AHB_BIT32);
	*interval =__raw_readl(IO_ADDRESS(addr));
}






/*
 *读取RTC定时器时使用
 *
 */
void cls_rtc_getalarm(unsigned long *alarm_secs)
{
	uint32_t addr, val, status;
        //		        100000000                         0x21=100001
	val = (BBL1_R_BBL_MATCH_SEL << BBL0_F_indaddr_R) | (BBL_READ_INDREG);
	addr = BBL0_R_BBL_CMDSTS_MEMADDR;   //0x1028000
	__raw_writel(val,IO_ADDRESS(addr));

	do {
		//status = CPU_READ_SINGLE(IO_ADDRESS(addr), AHB_BIT32);
		  status = __raw_readl(IO_ADDRESS(addr));
	} while ((status & BBL0_F_rdyn_go_MASK));

	addr = BBL0_R_BBL_ACCDATA_MEMADDR;     //0x1028004
	*alarm_secs = __raw_readl(IO_ADDRESS(addr));

}

/*
 *设置RTC定时器时使用
 *
 */

void cls_rtc_aie_on_off(int *enable)
{
	uint32_t val, addr, status;
	printk("kernel call cls_rtc_aie_on_off\n");
	if (*enable)
		val = 1 << BBL1_F_bbl_match_intr_en_R;
	else
		val = 0 << BBL1_F_bbl_match_intr_en_R;

	addr = BBL0_R_BBL_ACCDATA_MEMADDR;
//	CPU_WRITE_SINGLE(IO_ADDRESS(addr), val, AHB_BIT32);
	__raw_writel(val,IO_ADDRESS(addr));

	val = (BBL1_R_BBL_INT_EN_SEL << BBL0_F_indaddr_R) | (BBL_WRITE_INDREG);
	addr = BBL0_R_BBL_CMDSTS_MEMADDR;
//	CPU_WRITE_SINGLE(IO_ADDRESS(addr), val, AHB_BIT32);
	__raw_writel(val,IO_ADDRESS(addr));
	do {
		//status = CPU_READ_SINGLE(IO_ADDRESS(addr), AHB_BIT32);
		  status = __raw_readl(IO_ADDRESS(addr));
	} while ((status & BBL0_F_rdyn_go_MASK));

}



void cls_rtc_setalarm(unsigned long *alarm_secs)
{
	uint32_t addr, val, status;
	int ien;
	printk("kernel set alarm from stbl\n");
	// Disable and clear alarm interrupt
	ien = 0;
	cls_rtc_aie_on_off(&ien);
	val = BBL1_F_bbl_per_intr_clr_MASK | BBL1_F_bbl_match_intr_clr_MASK;
	addr = BBL0_R_BBL_ACCDATA_MEMADDR;
//	CPU_WRITE_SINGLE(IO_ADDRESS(addr), val, AHB_BIT32);
	__raw_writel(val,IO_ADDRESS(addr));

	val = (BBL1_R_BBL_CLR_INT_SEL << BBL0_F_indaddr_R) | (BBL_WRITE_INDREG);
	addr = BBL0_R_BBL_CMDSTS_MEMADDR;
//	CPU_WRITE_SINGLE(IO_ADDRESS(addr), val, AHB_BIT32);
	__raw_writel(val,IO_ADDRESS(addr));

	do {
		//status = CPU_READ_SINGLE(IO_ADDRESS(addr), AHB_BIT32);
		status = __raw_readl(IO_ADDRESS(addr));
	} while ((status & BBL0_F_rdyn_go_MASK));

	// set alarm value
	addr = BBL0_R_BBL_ACCDATA_MEMADDR;
	//CPU_WRITE_SINGLE(IO_ADDRESS(addr), *alarm_secs, AHB_BIT32);
	__raw_writel(*alarm_secs,IO_ADDRESS(addr));

	// Indirect Reg Write to bbl_match
	val = (BBL1_R_BBL_MATCH_SEL << BBL0_F_indaddr_R) | (BBL_WRITE_INDREG);
	addr = BBL0_R_BBL_CMDSTS_MEMADDR;
//	CPU_WRITE_SINGLE(IO_ADDRESS(addr), val, AHB_BIT32);
	__raw_writel(val,IO_ADDRESS(addr));
	do {
	//	status = CPU_READ_SINGLE(IO_ADDRESS(addr), AHB_BIT32);
		status = __raw_readl(IO_ADDRESS(addr));
	} while ((status & BBL0_F_rdyn_go_MASK));

}


/*
 *设置RTC时钟时使用
 *
 */
void bbl_rtc_control(uint32_t start) {
  uint32_t val, addr, status;

	// Start/Stop the rtc clock using bbl_rtc_stop
	if (start) 
	{
		val = BBL1_R_BBL_CONTROL_INIT & (~BBL1_F_bbl_rtc_stop_MASK);
	}
	else {
		val = BBL1_R_BBL_CONTROL_INIT | BBL1_F_bbl_rtc_stop_MASK;
	}
	addr = BBL0_R_BBL_ACCDATA_MEMADDR;
	
	__raw_writel(val,IO_ADDRESS(addr));
	
	val = (BBL1_R_BBL_CONTROL_SEL << BBL0_F_indaddr_R) | (BBL_WRITE_INDREG);
	
	addr = BBL0_R_BBL_CMDSTS_MEMADDR;

	__raw_writel(val,IO_ADDRESS(addr));
	do {
		  status = __raw_readl(IO_ADDRESS(addr));
		  
	   } while ((status & BBL0_F_rdyn_go_MASK));
#if 0
	if (start) {
		printk("Started the rtc clock using bbl_rtc_stop\n");
	}
	else {
		print_log("Stopped the rtc clock using bbl_rtc_stop\n");
	}
#endif
}
void bbl_set_rtc_secs(uint32_t rtc_secs) 
{
  uint32_t val, addr, status;

    // Load the rtc_seconds with an initial value
    val = rtc_secs;
    addr = BBL0_R_BBL_ACCDATA_MEMADDR;       //0x1028004 BBL Indirect Data Register
	__raw_writel(val,IO_ADDRESS(addr));
  
    // Indirect Reg Write to Register 2 (rtc_div_match)
    val = (BBL1_R_BBL_RTC_SECONDS_SEL << BBL0_F_indaddr_R) | (BBL_WRITE_INDREG);
	
    addr = BBL0_R_BBL_CMDSTS_MEMADDR;
	
   __raw_writel(val,IO_ADDRESS(addr));
    do {
     	  status = __raw_readl(IO_ADDRESS(addr));
		  
       } while ((status & BBL0_F_rdyn_go_MASK));
#if 0
    print_log("STEP 3a. Load the rtc_seconds with an initial value\n");
#endif
}

void cls_rtc_settime(uint32_t *secs)
{
	bbl_rtc_control(BBL_RTC_STOP);
	bbl_set_rtc_secs(*secs);
	bbl_rtc_control(BBL_RTC_START);
}

/*
 * Read current time and date in RTC
 */
static int bcm5892_rtc_readtime(struct device *dev, struct rtc_time *tm)
{
	unsigned long  rtc_sec;

	rtc_sec = readl( IO_ADDRESS(BBL0_R_BBL_RTC_TIME_MEMADDR) );  //0x1028010
	dev_dbg(dev, "%s rtc_sec:%ld\n", __func__, rtc_sec);
	rtc_time_to_tm(rtc_sec, tm);
	dev_dbg( dev, "lee %s: read time 0x%02d.0x%02d.0x%02d 0x%02d/0x%02d/0x%02d\n",
		 __func__,
		  tm->tm_year, tm->tm_mon, tm->tm_mday, 
		  tm->tm_hour, tm->tm_min, tm->tm_sec );
	return 0;
}

/*
 * Set current time and date in RTC
 */
static int bcm5892_rtc_settime(struct device *dev, struct rtc_time *tm)
{
	unsigned long current_sec;
	int ret = 0;

	dev_dbg( dev,"%s(): %4d-%02d-%02d %02d:%02d:%02d\n", __func__,
		1900 + tm->tm_year, tm->tm_mon, tm->tm_mday,
		tm->tm_hour, tm->tm_min, tm->tm_sec);

	mutex_lock(&bcm5892_rtc_mutex);

	rtc_tm_to_time( tm, &current_sec);

	dev_dbg(dev, "%s current_sec:%ld\n", __func__, current_sec);

	dmac_flush_range(&current_sec,(&current_sec+4));	
//	ret = call_secure_api(CLS_RTC_SETTIME_ID, 1, virt_to_phys(&current_sec));	
	cls_rtc_settime(&current_sec);
	dmac_flush_range(&current_sec,(&current_sec+4));	

	mutex_unlock(&bcm5892_rtc_mutex);
#if 0
	if (ret == 1) {
		dev_dbg(dev, "%s: error accessing secure mode, operation failed\n", 
			__func__ );
		return -EIO;
	}
#endif
	return 0;
}

static int bcm5892_rtc_setfreq(struct device *dev, int freq)
{
	int interval;
	int ret = 0;

	dev_dbg(dev, "%s(): freq=%d\n", __func__, freq);

	switch (freq) {
	case 1:
		interval = RTC_PER_1SEC; 
		break;
	case 2:
		interval = RTC_PER_2SECS; 
		break;
	case 4:
		interval = RTC_PER_4SECS; 
		break;
	case 8:
		interval = RTC_PER_8SECS; 
		break;
	case 16:
		interval = RTC_PER_16SECS; 
		break;
	case 32:
		interval = RTC_PER_32SECS; 
		break;
	case 64:
		interval = RTC_PER_64SECS; 
		break;
	case 128:
		interval = RTC_PER_128SECS;
		break;
	case 256:
		interval = RTC_PER_256SECS;
		break;
	default:
		dev_dbg (dev, "%s(): invalid freq=%d\n", __func__, freq);
		return -EINVAL;
	}

	dmac_flush_range(&interval,(&interval+4));	
//	ret = call_secure_api(CLS_RTC_SETPER_ID, 1, virt_to_phys(&interval));   //23
	cls_rtc_setper(&interval);
	dmac_flush_range(&interval,(&interval+4));	

	if (ret == 1) {
		dev_dbg(dev, "%s(): error accessing secure mode, operation failed\n", 
			__func__ );
		return -EIO;
	}
	return 0;
}

static int bcm5892_rtc_getfreq( struct device *dev, int *freq )
{
	int ret = 0, interval;

	dmac_flush_range(&interval,(&interval+4));	
//	ret= call_secure_api(CLS_RTC_GETPER_ID, 1, virt_to_phys(&interval));  //24
	cls_rtc_getper(&interval);
	dmac_flush_range(&interval,(&interval+4));	

	if (ret == 1) {
		dev_dbg(dev, "%s(): error accessing secure mode, operation failed\n", 
			__func__ );
		return -EIO;
	}

	switch (interval) {
	case RTC_PER_125ms:   
	case RTC_PER_250ms:
	case RTC_PER_500ms:
		break;
	case RTC_PER_1SEC:
		*freq = 1;
		break;
	case RTC_PER_2SECS:
		*freq = 2;
		break;
	case RTC_PER_4SECS:
		*freq = 4;
		break;
	case RTC_PER_8SECS:
		*freq = 8;
		break;
	case RTC_PER_16SECS:
		*freq = 16;
		break;
	case RTC_PER_32SECS:
		*freq = 32;
		break;
	case RTC_PER_64SECS:
		*freq = 64;
		break;
	case RTC_PER_128SECS:
		*freq = 128;
		break;
	case RTC_PER_256SECS:
		*freq = 256;
		break;
	}

	dev_dbg(dev,"%s(): interval=0x%08x, freq=%d\n", __func__, interval, 
		*freq );
	return ret;
}






/*
 * Read alarm time and date in RTC
 */
unsigned long alarm_sec;

static int bcm5892_rtc_readalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	int ret = 0;
	//unsigned long alarm_sec;

	dmac_flush_range(&alarm_sec,(&alarm_sec+4));

	//ret = call_secure_api(CLS_RTC_GETALRM_ID, 1, virt_to_phys(&alarm_sec));

	cls_rtc_getalarm(&alarm_sec);


	
	dmac_flush_range(&alarm_sec,(&alarm_sec+4));	
#if 0
	if (ret == 1) {
		dev_dbg(dev, "%s: error accessing secure mode, operation failed\n", 
			__func__ );
		return -EIO;
	}
#endif
	rtc_time_to_tm(alarm_sec, &alrm->time);
	
	return 0;
}

/*
 * Set alarm time and date in RTC
 */
static int bcm5892_rtc_setalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	unsigned long rtc_sec, alarm_sec;
	struct rtc_time *time = &alrm->time;
	int ret = 0;

	dev_dbg(dev, "%s: %d, %02x/%02x/%02x %02x.%02x.%02x\n",
		__func__, alrm->enabled, time->tm_mday & 0xff, time->tm_mon & 0xff,
		time->tm_year & 0xff, time->tm_hour & 0xff, time->tm_min & 0xff,
		time->tm_sec );

	rtc_sec = readl( IO_ADDRESS(BBL0_R_BBL_RTC_TIME_MEMADDR) );	
	mutex_lock(&bcm5892_rtc_mutex);	
	rtc_tm_to_time(time, &alarm_sec);
	if (alarm_sec < rtc_sec) {
		ret = -EINVAL;
		goto unlock;
	}
	dev_dbg(dev, "%s(): RTC time:%ld alrm time:%ld\n", 
		__func__, rtc_sec, alarm_sec);
	alarm_sec /= 128;
	if (alarm_sec > 0xFFFF) {
		dev_dbg(dev, "%s(): invalid time, alarm_sec=%ld\n",
			__func__, alarm_sec);
		ret = -EINVAL;
		goto unlock;
	}


	dmac_flush_range(&alarm_sec,(&alarm_sec+4));	
	printk("kernel call secure api --->set alarm\n");
//	ret = call_secure_api(CLS_RTC_SETALRM_ID, 1, virt_to_phys(&alarm_sec)); //20
	cls_rtc_setalarm(&alarm_sec);
	dmac_flush_range(&alarm_sec,(&alarm_sec+4));	

	if (ret == 1) {
		dev_dbg(dev, "%s: error accessing secure mode, operation failed\n", 
			__func__ );
		ret = -EIO;
	}

unlock:
	mutex_unlock(&bcm5892_rtc_mutex);	
	return ret;
}

/*
 * Handle commands from user-space
 */
static int bcm5892_rtc_ioctl(struct device *dev, unsigned int cmd,
			unsigned long arg)
{
	int ret = 0;
	int enable = 0;

	dev_dbg(dev, "%s(): cmd=%08x. arg=%08lx\n", __func__, cmd, arg);

	switch (cmd) 
	{
		
		case RTC_AIE_ON:
			enable = 1;
		case RTC_AIE_OFF:
			mutex_lock(&bcm5892_rtc_mutex);

			dmac_flush_range(&enable,(&enable+4));	
//			printk("======================5=======================\n");
		//	ret = call_secure_api(CLS_RTC_AIE_ON_OFF_ID, 1, virt_to_phys(&enable));
			cls_rtc_aie_on_off(&enable);
//			printk("======================6=======================\n");
			dmac_flush_range(&enable,(&enable+4));	

			mutex_unlock(&bcm5892_rtc_mutex);
			if (ret == 1) 
			{
			  dev_dbg(dev, "%s: error accessing secure mode, operation failed\n", __func__ );
				return -EIO;
			}
			break;
		case RTC_PIE_ON:
			enable = 1;
		case RTC_PIE_OFF:
			mutex_lock(&bcm5892_rtc_mutex);
			dmac_flush_range(&enable,(&enable+4));	
//			printk("=============3===============\n");
		//	ret = call_secure_api(CLS_RTC_PIE_ON_OFF_ID, 1, virt_to_phys(&enable));  //25
			cls_rtc_pie_on_off(&enable);
//			printk("=============4===============\n");
			dmac_flush_range(&enable,(&enable+4));	
			mutex_unlock(&bcm5892_rtc_mutex);
			if (ret == 1) {
			  dev_dbg(dev, "%s: error accessing secure mode, operation failed\n",
				  __func__ );
				return -EIO;
			}
			break;
		case RTC_IRQP_SET:
			mutex_lock(&bcm5892_rtc_mutex);
			ret = bcm5892_rtc_setfreq(dev, (int) arg);
			mutex_unlock(&bcm5892_rtc_mutex);
			break;
		case RTC_IRQP_READ:
		{
			int freq = 0;
	//		printk("========>RTC_IRQP_READ\n");
			ret = bcm5892_rtc_getfreq(dev, &freq);
			if(ret == 0)
				ret = put_user(freq, (unsigned long  *)arg);
		}
			break;
		default:
		//	printk("lee kernerl don't have common\n");
			ret = -ENOIOCTLCMD;
			break;
		}

		return ret;
}

void do_rtc_pending_imp(const uint32_t int_status)
{
	if (int_status &  BBL1_F_bbl_period_intr_sts_MASK)
		bcm5892_rtc_update_irq(rtc_g, 1, RTC_PF | RTC_IRQF);
	else if (int_status & BBL1_F_bbl_match_intr_sts_MASK)
		bcm5892_rtc_update_irq(rtc_g, 1, RTC_AF | RTC_IRQF);
		
}
/*
 * Provide additional RTC information in /proc/driver/rtc
 */
static int bcm5892_rtc_proc(struct device *dev, struct seq_file *seq)
{
	seq_printf(seq, "rtc time register\t: %x\n", 
		   readl (IO_ADDRESS(BBL0_R_BBL_RTC_TIME_MEMADDR)));

	return 0;
}

static const struct rtc_class_ops bcm5892_rtc_ops = {
	.ioctl		= bcm5892_rtc_ioctl,
	.read_time	= bcm5892_rtc_readtime,
	.set_time	= bcm5892_rtc_settime,
	.read_alarm	= bcm5892_rtc_readalarm,
	.set_alarm	= bcm5892_rtc_setalarm,
	.proc		= bcm5892_rtc_proc,
};

/*
 * Initialize and install RTC driver
 */


static int __devinit bcm5892_rtc_probe(struct platform_device *pdev)  
{
	struct rtc_device *rtc;
							
	rtc_g = rtc = bcm5892_rtc_device_register(pdev->name, &pdev->dev, &bcm5892_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc)) 
		return PTR_ERR(rtc);

	platform_set_drvdata(pdev, rtc);    			//保存信息,使信息可在其他函数中使用
	mutex_init(&bcm5892_rtc_mutex);

	printk(KERN_INFO "BCM5892 Real Time Clock driver.\n");
	return 0;
}

/*
 * Disable and remove the RTC driver
 */
static int __devexit bcm5892_rtc_remove(struct platform_device *pdev)
{
	struct rtc_device *rtc = platform_get_drvdata(pdev);

	bcm5892_rtc_device_unregister(rtc);
	platform_set_drvdata(pdev, NULL);    			//清楚函数共享信息

	return 0;
}

#ifdef CONFIG_PM

static int bcm5892_rtc_suspend(struct platform_device *pdev, pm_message_t state)
{
	return 0;
}

static int bcm5892_rtc_resume(struct platform_device *pdev)
{
	return 0;
}
#else
#define bcm5892_rtc_suspend NULL
#define bcm5892_rtc_resume  NULL
#endif



static struct platform_driver bcm5892_rtc_driver = {
	.driver		= {
		.name	= "bcm5892-rtc",
		.owner	= THIS_MODULE,
	},
	.probe = bcm5892_rtc_probe,
	.remove		= __devexit_p(bcm5892_rtc_remove),  //__devexit_p宏和__init宏一样起到优化的作用
	.suspend	= bcm5892_rtc_suspend,
	.resume		= bcm5892_rtc_resume,
};

uint32_t (*do_rtc_pending_org)(const uint32_t);
//extern uint32_t (*do_rtc_pending)(const uint32_t);  //changed by lee
uint32_t (*do_rtc_pending)(const uint32_t);  //changed by lee


static int __init bcm5892_rtc_init(void)
{
	do_rtc_pending_org=do_rtc_pending;   
	do_rtc_pending=&do_rtc_pending_imp;  
	return bcm5892_platform_driver_register(&bcm5892_rtc_driver);  //驱动属于platform总线
}

static void __exit bcm5892_rtc_exit(void)
{
	do_rtc_pending=do_rtc_pending_org;   //changed by lee
	bcm5892_platform_driver_unregister(&bcm5892_rtc_driver);
}

module_init(bcm5892_rtc_init);
module_exit(bcm5892_rtc_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RTC driver for BCM5892");

