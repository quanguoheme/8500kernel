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
#include <mach/shm.h>

static struct rtc_device *rtc_g;
static DEFINE_MUTEX(bcm5892_rtc_mutex);

/*
 * Read current time and date in RTC
 */
static int bcm5892_rtc_readtime(struct device *dev, struct rtc_time *tm)
{
	unsigned long  rtc_sec;

	rtc_sec = readl( IO_ADDRESS(BBL0_R_BBL_RTC_TIME_MEMADDR) );
	dev_dbg(dev, "%s rtc_sec:%ld\n", __func__, rtc_sec);

	rtc_time_to_tm(rtc_sec, tm);

	dev_dbg( dev, "%s: read time 0x%02d.0x%02d.0x%02d 0x%02d/0x%02d/0x%02d\n",
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
	ret = call_secure_api(CLS_RTC_SETTIME_ID, 1, virt_to_phys(&current_sec));	
	dmac_flush_range(&current_sec,(&current_sec+4));	

	mutex_unlock(&bcm5892_rtc_mutex);

	if (ret == 1) {
		dev_dbg(dev, "%s: error accessing secure mode, operation failed\n", 
			__func__ );
		return -EIO;
	}
	
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
	ret = call_secure_api(CLS_RTC_SETPER_ID, 1, virt_to_phys(&interval));
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
	ret= call_secure_api(CLS_RTC_GETPER_ID, 1, virt_to_phys(&interval));
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
static int bcm5892_rtc_readalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	int ret = 0;
	unsigned long alarm_sec;

	dmac_flush_range(&alarm_sec,(&alarm_sec+4));	
	ret = call_secure_api(CLS_RTC_GETALRM_ID, 1, virt_to_phys(&alarm_sec));
	dmac_flush_range(&alarm_sec,(&alarm_sec+4));	

	if (ret == 1) {
		dev_dbg(dev, "%s: error accessing secure mode, operation failed\n", 
			__func__ );
		return -EIO;
	}
	
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
	ret = call_secure_api(CLS_RTC_SETALRM_ID, 1, virt_to_phys(&alarm_sec)); 
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
			ret = call_secure_api(CLS_RTC_AIE_ON_OFF_ID, 1, virt_to_phys(&enable));
			dmac_flush_range(&enable,(&enable+4));	

			mutex_unlock(&bcm5892_rtc_mutex);
			if (ret == 1) 
			{
			  dev_dbg(dev, "%s: error accessing secure mode, operation failed\n",
				  __func__ );
				return -EIO;
			}
		break;
		case RTC_PIE_ON:
			enable = 1;
		case RTC_PIE_OFF:
			mutex_lock(&bcm5892_rtc_mutex);
			dmac_flush_range(&enable,(&enable+4));	
			ret = call_secure_api(CLS_RTC_PIE_ON_OFF_ID, 1, virt_to_phys(&enable));
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
			ret = bcm5892_rtc_getfreq(dev, &freq);
			if(ret == 0)
				ret = put_user(freq, (unsigned long  *)arg);
		}
			break;
		default:
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
	do_rtc_pending_org=do_rtc_pending;   //changed by lee
	do_rtc_pending=&do_rtc_pending_imp;  //changed by lee
	return bcm5892_platform_driver_register(&bcm5892_rtc_driver);  //platform_driver_register(drv) 还是注册在platform平台上
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

