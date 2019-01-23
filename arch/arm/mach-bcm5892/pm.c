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

#include <linux/version.h>
#include <linux/pm.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/suspend.h>
#include <linux/interrupt.h>
#include <linux/sysfs.h>
#include <linux/module.h>
#include <linux/delay.h>

#include <mach/pmb.h>
#include <mach/bcm5892_sw.h>
#include <mach/hardware.h>
#include <mach/regaccess.h>
#include <asm/io.h>
#include <asm/cacheflush.h>
#include <mach/shm.h>
#include <mach/reg_gpio.h>
#include <mach/secure_api.h>


/* From dmu_cpuapi.h */
#define REFCLK		0x0
#define PLLCLK_TAP1	0x1
#define BBLCLK		0x2
#define SPLCLK		0x3
#define CPUCLK		DMU_F_dmu_cpuclk_sel_MASK	
#define CPUCLK_SEL	DMU_F_dmu_cpuclk_sel_R

#define MCLK		DMU_F_dmu_mclk_sel_MASK
#define HCLK		DMU_F_dmu_hclk_sel_MASK	
#define PCLK		DMU_F_dmu_pclk_sel_MASK	
#define BCLK		DMU_F_dmu_bclk_sel_MASK
#define BCLK_SEL		DMU_F_dmu_bclk_sel_R

#define REFCLK_SPEED	(24*1000*1000)
#define BBLCLK_SPEED	(32768)
#define SPLCLK_SPEED	(1)

#define SLEEP_CLK_SPEED	BBLCLK_SPEED

/* VIC0 is the secure VIC. Allow any secure interrupt to wake us up. */
#define VIC0_MASK	(~0)

#define VIC1_USB_OTG	(1 << 26)
#define VIC1_USB_HOST1	(1 << 27)
#define VIC1_USB_UDC	VIC1_USB_HOST1
#define VIC1_USB_HOST2	(1 << 28)
#define VIC1_EMAC	(1 << 29)
#define VIC1_SEC_PEND	(1 << 0)

#define VIC1_MASK	(VIC1_USB_UDC | VIC1_SEC_PEND)

#define VIC2_UART0	(1 << 6)
#define VIC2_UART1	(1 << 7)
#define VIC2_UART2	(1 << 8)
#define VIC2_UART3	(1 << 9)

#define VIC2_MASK	(VIC2_UART0 | VIC2_UART1 | VIC2_UART2 | VIC2_UART3)

#define PREFIX	"BCM5892 PM: "

extern void bcm5892_sleep_enter(void);

static int bcm5892_suspend_valid(suspend_state_t state);
static int bcm5892_suspend_prepare(void);
static int bcm5892_suspend_enter(suspend_state_t state);
static void bcm5892_suspend_finish(void);
static void bcm5892_suspend_recover(void);
static u32 bcm5892_get_pll_speed(int pll);
static int bcm5892_set_pll_speed(int pll, u32 speed);
static u32 bcm5892_get_cpu_speed(void);
static void bcm5892_set_cpu_speed(u32 speed);

#define _CPU_WRITE_SINGLE(addr, val, type)  	writel(val, IO_ADDRESS(addr))
#define _CPU_READ_SINGLE(addr,type) readl(IO_ADDRESS(addr))

#define  _CPU_RMW_OR_SINGLE(addr, val, type) \
	_CPU_WRITE_SINGLE(addr, _CPU_READ_SINGLE(addr,type) | val, type)

#define  _CPU_RMW_AND_SINGLE(addr,val,type) \
	_CPU_WRITE_SINGLE(addr, _CPU_READ_SINGLE(addr,type) & val, type)

typedef struct {
	uint32_t dma0_nohalt;
	uint32_t dma1_nohalt;
	uint32_t lcd;
	uint32_t usbh;
	uint32_t usbd;
	uint32_t usbotg;
} DMA_STATE;

int dmu_pwd(uint32_t blk1, uint32_t blk2)
{
	_CPU_WRITE_SINGLE(DMU_R_dmu_pwd_blk1_MEMADDR, blk1, AHB_BIT32);
	_CPU_WRITE_SINGLE(DMU_R_dmu_pwd_blk2_MEMADDR, blk2, AHB_BIT32);
	return 0;
}

unsigned int ddr_self_refresh_enable(void)
{
	volatile unsigned int data, power_down_reg;

	//Issue auto-refresh command
	data = _CPU_READ_SINGLE(DDR_R_SOFT_COMMANDS_CLIENT_MEMADDR,AHB_BIT32);
	data |= DDR_F_SOFT_COMMANDS_CLIENT_REFCmd_MASK;
	_CPU_WRITE_SINGLE(DDR_R_SOFT_COMMANDS_CLIENT_MEMADDR,data,AHB_BIT32);
	data &= ~DDR_F_SOFT_COMMANDS_CLIENT_REFCmd_MASK;
	_CPU_WRITE_SINGLE(DDR_R_SOFT_COMMANDS_CLIENT_MEMADDR,data,AHB_BIT32);

        //Put DDR into self refresh mode
        power_down_reg = data = _CPU_READ_SINGLE(DDR_R_POWER_DOWN_MODE_MEMADDR,AHB_BIT32);
        data &= ~DDR_F_POWER_DOWN_MODE_PDN_ENTER_MODE_MASK;
        data |= 2 << DDR_F_POWER_DOWN_MODE_PDN_ENTER_MODE_R;
        _CPU_WRITE_SINGLE(DDR_R_POWER_DOWN_MODE_MEMADDR, data, AHB_BIT32);
	return power_down_reg;
}

int cls_dmu_cpuclk_sel(uint32_t src, uint32_t clk, uint32_t clk_sel) 
{
    	uint32_t ulimit =0, count=0,value, status, config=0,data, tdm0=0, tdm1=0, tdm2=0, tdm3=0;

	//disable TDM buffers if its enabled per PR #4191
	value = _CPU_READ_SINGLE(MEM_R_MEM_CH_CTRL0_MEMADDR, AHB_BIT32);
	if ((value  & MEM_F_MEM_ENABLE_CH0_MASK) >> MEM_F_MEM_ENABLE_CH0_R)
	{
		_CPU_RMW_AND_SINGLE(MEM_R_MEM_CH_CTRL0_MEMADDR,(~MEM_F_MEM_ENABLE_CH0_MASK),AHB_BIT32);
		tdm0=1;
	}

	value = _CPU_READ_SINGLE(MEM_R_MEM_CH_CTRL1_MEMADDR, AHB_BIT32);
	if ((value  & MEM_F_MEM_ENABLE_CH1_MASK) >> MEM_F_MEM_ENABLE_CH1_R)
	{
		_CPU_RMW_AND_SINGLE(MEM_R_MEM_CH_CTRL1_MEMADDR,(~MEM_F_MEM_ENABLE_CH1_MASK),AHB_BIT32);
		tdm1=1;
	}

	value = _CPU_READ_SINGLE(MEM_R_MEM_CH_CTRL2_MEMADDR, AHB_BIT32);
	if ((value  & MEM_F_MEM_ENABLE_CH2_MASK) >> MEM_F_MEM_ENABLE_CH2_R)
	{
		_CPU_RMW_AND_SINGLE(MEM_R_MEM_CH_CTRL2_MEMADDR,(~MEM_F_MEM_ENABLE_CH2_MASK),AHB_BIT32);
		tdm2=1;
	}

	value = _CPU_READ_SINGLE(MEM_R_MEM_CH_CTRL3_MEMADDR, AHB_BIT32);
	if ((value  & MEM_F_MEM_ENABLE_CH3_MASK) >> MEM_F_MEM_ENABLE_CH3_R)
	{
		_CPU_RMW_AND_SINGLE(MEM_R_MEM_CH_CTRL3_MEMADDR,(~MEM_F_MEM_ENABLE_CH3_MASK),AHB_BIT32);
		tdm3=1;
	}

	value = _CPU_READ_SINGLE(DMU_R_dmu_clk_sel_MEMADDR, AHB_BIT32);
    	value = (value & ~clk) | (src <<clk_sel);

	if (( clk == PCLK) ||( clk == HCLK) ||( clk == MCLK) ||( clk == CPUCLK))
	{
		//check ulimit save, set to ff, then revert it after
		config = _CPU_READ_SINGLE(SPL_R_LFDETC_MEMADDR, AHB_BIT32);
		if ((config>>SPL_F_LFDET_ENABLE_R) &0x1)
		{
			ulimit = ((config >>SPL_F_ULIMIT_RGE_R) & 0xff);
			//set ulimit to 0xff
			_CPU_RMW_OR_SINGLE(SPL_R_LFDETC_MEMADDR,SPL_F_ULIMIT_RGE_MASK,AHB_BIT32);
		}
	}

	_CPU_WRITE_SINGLE(DMU_R_dmu_clk_sel_MEMADDR, value,  AHB_BIT32);

	do { 
		status = _CPU_READ_SINGLE(DMU_R_dmu_clk_sel_MEMADDR, AHB_BIT32);
		count++;
		if (count>10000)
		{
			printk(" ERROR!, timeout waiting for write to dmu_clk_sel register\n");
		        return CLS_STATUS_TIMED_OUT;
		}
		
	} while ( status != value);

	if (( clk == PCLK) ||( clk == HCLK) ||( clk == MCLK) ||( clk == CPUCLK))
	{
		//restore old ulimit value
		config = _CPU_READ_SINGLE(SPL_R_LFDETC_MEMADDR, AHB_BIT32);
		if ((config>>SPL_F_LFDET_ENABLE_R) &0x1)
		{
		//restoring ulimit by using a variable instead of writing directly into the register
		data =(config & ~SPL_F_ULIMIT_RGE_MASK );
		data |= (ulimit<<SPL_F_ULIMIT_RGE_R);
		//not needed at review timeout_ns(6000);
		_CPU_WRITE_SINGLE(SPL_R_LFDETC_MEMADDR, data ,AHB_BIT32);

		//_CPU_RMW_AND_SINGLE(SPL_R_LFDETC_MEMADDR, ~SPL_F_ULIMIT_RGE_MASK,AHB_BIT32);
		//_CPU_RMW_OR_SINGLE(SPL_R_LFDETC_MEMADDR, ulimit<<SPL_F_ULIMIT_RGE_R,AHB_BIT32);
		}
	}

	//if at the beginning of this api tdm is enable, now we need to reenable it
	if (tdm0==1)
	{
		_CPU_RMW_OR_SINGLE(MEM_R_MEM_CH_CTRL0_MEMADDR,(MEM_F_MEM_ENABLE_CH0_MASK),AHB_BIT32);
		tdm0=0;
	}

	if(tdm1==1)
	{
		_CPU_RMW_OR_SINGLE(MEM_R_MEM_CH_CTRL1_MEMADDR,(MEM_F_MEM_ENABLE_CH1_MASK),AHB_BIT32);
		tdm1=0;
	}

	if(tdm2==1)
	{
		_CPU_RMW_OR_SINGLE(MEM_R_MEM_CH_CTRL2_MEMADDR,(MEM_F_MEM_ENABLE_CH2_MASK),AHB_BIT32);
		tdm2=0;
	}

	if(tdm3==1)
	{
		_CPU_RMW_OR_SINGLE(MEM_R_MEM_CH_CTRL3_MEMADDR,(MEM_F_MEM_ENABLE_CH3_MASK),AHB_BIT32);
		tdm3=0;
	}

    return CLS_STATUS_OK;
}

int  cls_dmu_pll_resync(int p1div, int p2div, int ndiv, int m1div, int m2div, int m3div, int m4div, int pll_num, int frac,int bclk_sel) 
{
	uint32_t temp, temp1, temp2, config, status, count=0, ctrl=0, frac_new=0;
	//float vco_rng=0, ndiv_frac =0, vco_rng_int;
	uint32_t vco_rng, ndiv_frac, vco_rng_int;

	if (pll_num==0)
	{
		///1.
		///2.
		cls_dmu_cpuclk_sel(REFCLK, CPUCLK,CPUCLK_SEL);

		///3.

	    // Set enb_clkout
	    config = DMU_F_dmu_pll0_enb_clkout_MASK;
	    _CPU_RMW_OR_SINGLE(DMU_R_dmu_pll0_clk_param_MEMADDR, config, AHB_BIT32);
//		timeout_ns(100);
		ndelay(100);

	 ///4.
	    config = (1 << DMU_F_dmu_pll0_dreset_R)     ; // dreset
	    _CPU_RMW_OR_SINGLE(DMU_R_dmu_pll0_clk_param_MEMADDR, config, AHB_BIT32);

	// 5.
	    temp = _CPU_READ_SINGLE(DMU_R_dmu_pll0_clk_param_MEMADDR, AHB_BIT32);

	     //ndiv_frac =  (frac/1.68) * (1/1000000);
	     ndiv_frac =  (frac/42) * (25/1000000);
	     vco_rng = ((p2div/p1div) * (ndiv + ndiv_frac) * 24);
	     vco_rng_int = vco_rng;

	    config = p1div                             ; // p1div
	    config |= p2div << DMU_F_dmu_pll0_p2div_R    ; // p2div
	    config |= ndiv << DMU_F_dmu_pll0_ndiv_int_R  ; // ndiv
	    config |= (1 << DMU_F_dmu_pll0_enb_clkout_R) ; // enb_clkout
	    config |= (1 << DMU_F_dmu_pll0_dreset_R)     ; // dreset
	    config |= (0 << DMU_F_dmu_pll0_pwrdn_R)      ; // pwrdn
	    if(vco_rng_int > 1600){
		config |= (1 << DMU_F_dmu_pll0_vco_rng_R)      ; }// vco_rng
	    else{
		config |= (0 << DMU_F_dmu_pll0_vco_rng_R)      ; }// vco_rng
	    _CPU_WRITE_SINGLE(DMU_R_dmu_pll0_clk_param_MEMADDR, config, AHB_BIT32);

	    config = m1div                              ; // m1div
	    config |= (0 << DMU_F_dmu_pll0_pwrdn_ch1_R)  ; // pwrdn_ch1
	    _CPU_WRITE_SINGLE(DMU_R_dmu_pll0_ch1_param_MEMADDR, config, AHB_BIT32);

	    config = m2div                              ; // m2div
	    config |= (0 << DMU_F_dmu_pll0_pwrdn_ch2_R)  ; // pwrdn_ch2
	    _CPU_WRITE_SINGLE(DMU_R_dmu_pll0_ch2_param_MEMADDR, config, AHB_BIT32);

	    config = m3div                              ; // m3div
	    config |= (0 << DMU_F_dmu_pll0_pwrdn_ch3_R)  ; // pwrdn_ch3
	    _CPU_WRITE_SINGLE(DMU_R_dmu_pll0_ch3_param_MEMADDR, config, AHB_BIT32);

	    config = m4div                              ; // m4div
	    config |= (0 << DMU_F_dmu_pll0_pwrdn_ch4_R)  ; // pwrdn_ch4
	    _CPU_WRITE_SINGLE(DMU_R_dmu_pll0_ch4_param_MEMADDR, config, AHB_BIT32);

		temp1 = _CPU_READ_SINGLE(DMU_R_dmu_pll0_frac_param_MEMADDR, AHB_BIT32);
		temp2 = _CPU_READ_SINGLE(DMU_R_dmu_pll0_ctrl1_MEMADDR, AHB_BIT32);

		if (frac != 0)
		{
		    if  ((temp1 &DMU_F_dmu_pll0_ndiv_frac_MASK) != frac)
		    {
	   		frac_new = frac;
			temp1 = temp1 & ~DMU_F_dmu_pll0_ndiv_mode_MASK;
			frac_new =  frac_new | (temp1 & ~DMU_F_dmu_pll0_ndiv_frac_MASK) | (2 << DMU_F_dmu_pll0_ndiv_mode_R);
			// Clear bypass sdmod field
			frac_new &= ~DMU_F_dmu_pll0_bypass_sdmod_MASK;
			_CPU_WRITE_SINGLE(DMU_R_dmu_pll0_frac_param_MEMADDR, frac_new, AHB_BIT32);
			//set ctrl _low to frac
			if(vco_rng > 1600) {
		 	  _CPU_WRITE_SINGLE(DMU_R_dmu_pll0_ctrl1_MEMADDR, 0x382c2860, AHB_BIT32);
			}
			else {
		 	  _CPU_WRITE_SINGLE(DMU_R_dmu_pll0_ctrl1_MEMADDR, 0x202c2820, AHB_BIT32);
		 	}
		    }
		}
		else
		{
			//set ndiv mode back to interger mode
			frac_new =  (temp1 & ~DMU_F_dmu_pll0_ndiv_frac_MASK);
			frac_new =  (frac_new & ~DMU_F_dmu_pll0_ndiv_mode_MASK) ;
			// Set bypass sdmod field
			frac_new |= DMU_F_dmu_pll0_bypass_sdmod_MASK;
			_CPU_WRITE_SINGLE(DMU_R_dmu_pll0_frac_param_MEMADDR, frac_new, AHB_BIT32);
		        if(vco_rng > 1600) {
		           _CPU_WRITE_SINGLE(DMU_R_dmu_pll0_ctrl1_MEMADDR, 0x38000700, AHB_BIT32);}
		        else{
			   _CPU_WRITE_SINGLE(DMU_R_dmu_pll0_ctrl1_MEMADDR, 0x200005c0, AHB_BIT32);}
		}

		ctrl= _CPU_READ_SINGLE(DMU_R_dmu_pll0_ctrl1_MEMADDR, AHB_BIT32);

	   // Clear enb_clkout
	   config = ~DMU_F_dmu_pll0_enb_clkout_MASK;
	   _CPU_RMW_AND_SINGLE(DMU_R_dmu_pll0_clk_param_MEMADDR, config, AHB_BIT32);

	#ifdef ATE
	    timeout_ns(2000);
	#else
		//6 . wait for unlock only if u change vco frequency
		if( ((temp & DMU_F_dmu_pll0_p1div_MASK) !=  p1div) || (((temp & DMU_F_dmu_pll0_p2div_MASK)>>DMU_F_dmu_pll0_p2div_R) !=  p2div) ||(((temp & DMU_F_dmu_pll0_ndiv_int_MASK)>>DMU_F_dmu_pll0_ndiv_int_R) !=  ndiv) || (frac_new != temp1) || (temp2 != ctrl))
		{
		status = _CPU_READ_SINGLE(DMU_R_dmu_status_MEMADDR, AHB_BIT32);
		while( (!((status >>DMU_F_dmu_pll0_intrp_R) & 0x1)) && (count<8000) )
	    	{
		status = _CPU_READ_SINGLE(DMU_R_dmu_status_MEMADDR, AHB_BIT32);
		count++;
		if (count>7998)
		{
			printk(" timeout waiting for unlock complete\n");
		}
		}
		count=0;
		}
	    	//7.
		// Poll on pll lock bit
	  	status = _CPU_READ_SINGLE(DMU_R_dmu_status_MEMADDR, AHB_BIT32);
		while( !(status  & 0x1) )
	    	{
		status = _CPU_READ_SINGLE(DMU_R_dmu_status_MEMADDR, AHB_BIT32);
		count++;
		if (count>8000)
		{
		  printk("timeout waiting for lock complete\n");
	          return CLS_STATUS_TIMED_OUT;
		}
		}
	#endif
		count=0;

	   	//8.
		 // clear dreset
	    config = ~DMU_F_dmu_pll0_dreset_MASK;
	    _CPU_RMW_AND_SINGLE(DMU_R_dmu_pll0_clk_param_MEMADDR, config, AHB_BIT32);

		//10. 11.
		cls_dmu_cpuclk_sel(PLLCLK_TAP1, CPUCLK,CPUCLK_SEL);
	} //pll_num =0
	else //pll_num =1
	{
	    // Set enb_clkout
	    	config = DMU_F_dmu_pll1_enb_clkout_MASK;
		_CPU_RMW_OR_SINGLE(DMU_R_dmu_pll1_clk_param_MEMADDR, config, AHB_BIT32);
//		timeout_ns(100);
		ndelay(100);

		temp = _CPU_READ_SINGLE(DMU_R_dmu_pll1_clk_param_MEMADDR, AHB_BIT32);

		 config  = p1div                             ; // p1div
	    config |= p2div << DMU_F_dmu_pll1_p2div_R    ; // p2div
	    config |= ndiv << DMU_F_dmu_pll1_ndiv_int_R  ; // ndiv
	    config |= (1 << DMU_F_dmu_pll1_enb_clkout_R) ; // enb_clkout
	    config |= (1 << DMU_F_dmu_pll1_dreset_R)     ; // dreset
	    config |= (0 << DMU_F_dmu_pll1_pwrdn_R)      ; // pwrdn
	    _CPU_WRITE_SINGLE(DMU_R_dmu_pll1_clk_param_MEMADDR, config, AHB_BIT32);

	    config = m1div                              ; // m1div
	    config |= (0 << DMU_F_dmu_pll1_pwrdn_ch1_R)  ; // pwrdn_ch1
	    _CPU_WRITE_SINGLE(DMU_R_dmu_pll1_ch1_param_MEMADDR, config, AHB_BIT32);

	    config = m2div                              ; // m2div
	    config |= (0 << DMU_F_dmu_pll1_pwrdn_ch2_R)  ; // pwrdn_ch1
	    _CPU_WRITE_SINGLE(DMU_R_dmu_pll1_ch2_param_MEMADDR, config, AHB_BIT32);

		temp1 = _CPU_READ_SINGLE(DMU_R_dmu_pll1_frac_param_MEMADDR, AHB_BIT32);
		temp2 = _CPU_READ_SINGLE(DMU_R_dmu_pll1_ctrl1_MEMADDR, AHB_BIT32);

		if ((frac != 0) && ((temp1 &DMU_F_dmu_pll1_ndiv_frac_MASK) != frac))
		{
	   	frac_new = frac;
		temp1 = temp1 &~DMU_F_dmu_pll1_ndiv_mode_MASK;
		frac_new =  frac_new | (temp1 & ~DMU_F_dmu_pll1_ndiv_frac_MASK) | (2 << DMU_F_dmu_pll1_ndiv_mode_R);
		// Clear bypass sdmod field
		frac_new &= ~DMU_F_dmu_pll1_bypass_sdmod_MASK;
		_CPU_WRITE_SINGLE(DMU_R_dmu_pll1_frac_param_MEMADDR, frac_new, AHB_BIT32);
		//set ctrl _low to frac
		_CPU_WRITE_SINGLE(DMU_R_dmu_pll1_ctrl1_MEMADDR, 0x202c2820, AHB_BIT32);
		}
		else if ((frac != 0) && ((temp1 &DMU_F_dmu_pll1_ndiv_frac_MASK) == frac))
		{
		}
		else
		{
			//set ndiv mode back to interger mode
			frac_new =   temp1 & (~(DMU_F_dmu_pll1_ndiv_frac_MASK | DMU_F_dmu_pll1_ndiv_mode_MASK)) ;
			// Set bypass sdmod field
			frac_new |= DMU_F_dmu_pll1_bypass_sdmod_MASK;
			_CPU_WRITE_SINGLE(DMU_R_dmu_pll1_frac_param_MEMADDR, frac_new, AHB_BIT32);
			_CPU_WRITE_SINGLE(DMU_R_dmu_pll1_ctrl1_MEMADDR, 0x200005c0, AHB_BIT32);
		}

		ctrl= _CPU_READ_SINGLE(DMU_R_dmu_pll1_ctrl1_MEMADDR, AHB_BIT32);

	   	if( ((temp & DMU_F_dmu_pll1_p1div_MASK) !=  p1div) || (((temp & DMU_F_dmu_pll1_p2div_MASK)>>DMU_F_dmu_pll1_p2div_R ) !=  p2div) ||(((temp & DMU_F_dmu_pll1_ndiv_int_MASK)>>DMU_F_dmu_pll1_ndiv_int_R ) !=  ndiv) || (frac_new != temp1) || (temp2 != ctrl))
		{
		status = _CPU_READ_SINGLE(DMU_R_dmu_status_MEMADDR, AHB_BIT32);
		while( (!((status >>DMU_F_dmu_pll1_intrp_R) & 0x1)) && (count<8000) )
	    	{
		status = _CPU_READ_SINGLE(DMU_R_dmu_status_MEMADDR, AHB_BIT32);
		count++;
		if (count>7998)
		{
		printk("timeout waiting for unlock complete\n");
		}
		}
		count=0;
		}

	#ifdef ATE
	    timeout_ns(2000);
	#else
	    // Poll on pll lock bit
	 	status = _CPU_READ_SINGLE(DMU_R_dmu_status_MEMADDR, AHB_BIT32);
		while( !(status>>2  & 0x1) )
	    	{
		status = _CPU_READ_SINGLE(DMU_R_dmu_status_MEMADDR, AHB_BIT32);
		count++;
		if (count>8000)
		{
		  printk("timeout waiting for lock complete\n");
	          return CLS_STATUS_TIMED_OUT;
		}
		}
	#endif

		cls_dmu_cpuclk_sel(bclk_sel, BCLK,BCLK_SEL);

	    // Clear enb_clkout
	    config = ~DMU_F_dmu_pll1_enb_clkout_MASK;
	    _CPU_RMW_AND_SINGLE(DMU_R_dmu_pll1_clk_param_MEMADDR, config, AHB_BIT32);

	    // clear dreset
	    config = ~DMU_F_dmu_pll1_dreset_MASK;
	    _CPU_RMW_AND_SINGLE(DMU_R_dmu_pll1_clk_param_MEMADDR, config, AHB_BIT32);

	} //pll_num =1

    return 0;
}

void ddr_self_refresh_disable(unsigned int power_down_reg)
{
	volatile unsigned int data;

	//Disable self-refresh mode
	_CPU_WRITE_SINGLE(DDR_R_POWER_DOWN_MODE_MEMADDR, power_down_reg, AHB_BIT32);

	//Issue auto-refresh command
	data = _CPU_READ_SINGLE(DDR_R_SOFT_COMMANDS_CLIENT_MEMADDR,AHB_BIT32);
	data |= DDR_F_SOFT_COMMANDS_CLIENT_REFCmd_MASK;
	_CPU_WRITE_SINGLE(DDR_R_SOFT_COMMANDS_CLIENT_MEMADDR,data,AHB_BIT32);
	data &= ~DDR_F_SOFT_COMMANDS_CLIENT_REFCmd_MASK;
	_CPU_WRITE_SINGLE(DDR_R_SOFT_COMMANDS_CLIENT_MEMADDR,data,AHB_BIT32);
}

int  dmu_cpuclk_sel_wrapper(uint32_t src, uint32_t clk, uint32_t clk_sel, int *ret)
{
	int result;
	uint32_t power_down_reg;

	power_down_reg = ddr_self_refresh_enable();
	//printk("calling pll resync\n");
	result = cls_dmu_cpuclk_sel(src, clk, clk_sel);
	ddr_self_refresh_disable(power_down_reg);
	/* Only write result after DDR re-enabled */
	*ret = result;
	//printk("result = %d %d\n", result, *ret);
	return 0;
}

void dma_resume(DMA_STATE *state)
{
	int i;
	uint32_t tmp, blk1, blk2;

	/* determine which blocks are enabled */
	blk1 = _CPU_READ_SINGLE(DMU_R_dmu_pwd_blk1_MEMADDR, AHB_BIT32);
	blk2 = _CPU_READ_SINGLE(DMU_R_dmu_pwd_blk2_MEMADDR, AHB_BIT32);

	/* lcd - handled by unloading the driver. */
	/* dec - handled by unloading the driver. */
	/* smau - don't disable */
	/* eth - handled by unloading the driver. */

	/* usb-otg */
	if (!(blk2 & DMU_F_dmu_pwd_usb0_MASK)) {
		_CPU_WRITE_SINGLE(USB0_R_GAHBCFG_MEMADDR, state->usbotg, AHB_BIT32);
	}

	/* usb-device */
	if (!(blk2 & DMU_F_dmu_pwd_usb1_MASK)) {
		_CPU_WRITE_SINGLE(USBD_R_UDCAHB_GLBL_DeviceControlReg_MEMADDR,
				 state->usbd, AHB_BIT32);
	}

	/* usb-host */
	if (!(blk2 & DMU_F_dmu_pwd_usb2_MASK)) {
		/* Check that HostControllerFunctionalState is USBOPERATIONAL */
		if ((state->usbh & 0x00c0) == 0x0080) {
			/* Simply restore the list processing enable bits */
			_CPU_WRITE_SINGLE(USB2_R_HcControl_MEMADDR, state->usbh, AHB_BIT32);
		}
	}

	/* sdio - nothing to do */

	/* dma */
	if (!(blk1 & DMU_F_dmu_pwd_odma_MASK)) {
		/* dma0 - modified PL080, 8 channel */
		for (i = 0; i < 8; ++i) {
			if (state->dma0_nohalt & (1 << i)) {
				uint32_t addr = DMA0_R_DMACC0Configuration_MEMADDR + i*0x20;
				/* Clear the halt bit */
				tmp = _CPU_READ_SINGLE(addr, AHB_BIT32);
				tmp &= ~(1 << DMA0_F_C0Halt_R);
				_CPU_WRITE_SINGLE(addr, tmp, AHB_BIT32);
			}
		}
		/* dma1 - PL081, 2 channel */
		for (i = 0; i < 2; ++i) {
			if (state->dma1_nohalt & (1 << i)) {
				uint32_t addr = DMA1_R_DMACC0Configuration_MEMADDR + i*0x20;
				/* Clear the halt bit */
				tmp = _CPU_READ_SINGLE(addr, AHB_BIT32);
				tmp &= ~(1 << DMA1_F_C0Halt_R);
				_CPU_WRITE_SINGLE(addr, tmp, AHB_BIT32);
			}
		}
	}
	/* All DMA that was previously enabled should be re-enabled at this point */
}


void dma_suspend(DMA_STATE *state)
{
	int i;
	uint32_t tmp, blk1, blk2;

	/* initialize the state datastructure */
	state->dma0_nohalt = 0;
	state->dma1_nohalt = 0;
	state->lcd = 0;
	state->usbh = 0;
	state->usbd = 0;
	state->usbotg = 0;

	/* determine which blocks are enabled */
	blk1 = _CPU_READ_SINGLE(DMU_R_dmu_pwd_blk1_MEMADDR, AHB_BIT32);
	blk2 = _CPU_READ_SINGLE(DMU_R_dmu_pwd_blk2_MEMADDR, AHB_BIT32);

	/* lcd - handled by unloading the driver. */
	/* dec - handled by unloading the driver. */
	/* smau - don't disable */
	/* eth - handled by unloading the driver. */

	/* usb-otg */
	if (!(blk2 & DMU_F_dmu_pwd_usb0_MASK)) {
		state->usbotg = _CPU_READ_SINGLE(USB0_R_GAHBCFG_MEMADDR, AHB_BIT32);
		tmp = state->usbotg & ~USB0_F_DMAEn_MASK;
		_CPU_WRITE_SINGLE(USB0_R_GAHBCFG_MEMADDR, tmp, AHB_BIT32);
	}

	/* usb-device - turn off RX and TX DMA bits */
	if (!(blk2 & DMU_F_dmu_pwd_usb1_MASK)) {
		state->usbd = _CPU_READ_SINGLE(USBD_R_UDCAHB_GLBL_DeviceControlReg_MEMADDR,
					      AHB_BIT32);
		tmp = state->usbd;
		tmp &= ~USBD_F_UDCAHB_GLBL_DevCtrl_TDE_MASK;
		tmp &= ~USBD_F_UDCAHB_GLBL_DevCtrl_RDE_MASK;
		_CPU_WRITE_SINGLE(USBD_R_UDCAHB_GLBL_DeviceControlReg_MEMADDR, tmp,
				 AHB_BIT32);
	}

	/* usb-host - turn off list processing, wait one frame for it to really stop */
	if (!(blk2 & DMU_F_dmu_pwd_usb2_MASK)) {
		uint32_t frame1, frame2;

		/* Save the HcControl register value for resume processing */
		state->usbh = _CPU_READ_SINGLE(USB2_R_HcControl_MEMADDR, AHB_BIT32);

		/* Check that HostControllerFunctionalState is USBOPERATIONAL */
		if ((state->usbh & 0x00c0) == 0x0080) {

			/* Turn off Periodic, Iso, Control and Bulk list enables */
			tmp = state->usbh & ~0x003c;
			_CPU_WRITE_SINGLE(USB2_R_HcControl_MEMADDR, tmp, AHB_BIT32);

			frame1 = _CPU_READ_SINGLE(USB2_R_HcFmNumber_MEMADDR, AHB_BIT32);
			frame1 &= USB2_R_HcFmNumber_MASK;
			do {
				frame2 = CPU_READ_SINGLE(USB2_R_HcFmNumber_MEMADDR,
							 AHB_BIT32);
				frame2 &= USB2_R_HcFmNumber_MASK;
			} while (frame1 == frame2);
		}
	}

	/* sdio */
	if (!(blk1 & DMU_F_dmu_pwd_sdm0_MASK)) {
		/* SDM0 */
		tmp = _CPU_READ_SINGLE(SDM0_R_present_state_MEMADDR, AHB_BIT32);
		if (tmp & SDM0_F_cmd_inhibit_cmd_MASK) {
			do {
				tmp = _CPU_READ_SINGLE(SDM0_R_interupt_status_MEMADDR,
						      AHB_BIT32);
			} while (!(tmp & SDM0_F_command_complete_MASK));
		}
		/* SDM1 */
	}
	if (!(blk1 & DMU_F_dmu_pwd_sdm1_MASK)) {
		tmp = _CPU_READ_SINGLE(SDM1_R_present_state_MEMADDR, AHB_BIT32);
		if (tmp & SDM1_F_cmd_inhibit_cmd_MASK) {
			do {
				tmp = _CPU_READ_SINGLE(SDM1_R_normal_interupt_status_MEMADDR,
						      AHB_BIT32);
			} while (!(tmp & SDM1_F_command_complete_MASK));
		}
	}

	/* dma */
	if (!(blk1 & DMU_F_dmu_pwd_odma_MASK)) {
		/* dma0 - modified PL080, 8 channel */
		for (i = 0; i < 8; ++i) {
			uint32_t addr = DMA0_R_DMACC0Configuration_MEMADDR + i*0x20;
			tmp = _CPU_READ_SINGLE(addr, AHB_BIT32);
			if (!(tmp & DMA0_F_C0Halt_MASK)) {
				state->dma0_nohalt |= (1 << i);
				/* set halt, wait for active to clear */
				tmp |= DMA0_F_C0Halt_MASK;
				_CPU_WRITE_SINGLE(addr, tmp, AHB_BIT32);
				do {
					tmp = _CPU_READ_SINGLE(addr, AHB_BIT32);
				} while (tmp & DMA0_F_C0Active_MASK);
			}
		}
		/* dma1 - PL081, 2 channel */
		for (i = 0; i < 2; ++i) {
			uint32_t addr = DMA1_R_DMACC0Configuration_MEMADDR + i*0x20;
			tmp = _CPU_READ_SINGLE(addr, AHB_BIT32);
			if (!(tmp & DMA1_F_C0Halt_MASK)) {
				state->dma1_nohalt |= (1 << i);
				/* set halt, wait for active to clear */
				tmp |= DMA1_F_C0Halt_MASK;
				_CPU_WRITE_SINGLE(addr, tmp, AHB_BIT32);
				do {
					tmp = _CPU_READ_SINGLE(addr, AHB_BIT32);
				} while (tmp & DMA1_F_C0Active_MASK);
			}
		}
	}
	/* All DMA should be disabled, and complete at this point */
}

int dmu_pll_resync_wrapper(int p1div, int p2div, int ndiv, int m1div, int m2div, int m3div, int m4div, int pll_num, int frac,int bclk_sel, int *ret)
{
	int result;
	DMA_STATE state;
	register uint32_t power_down_reg;	/* MUST be in a register */
	uint32_t data, val, val_2;

	dma_suspend(&state);
	/* dump ddr registers
	data = 0x85000;
	for (val = 0; val < 0x78; val += 4)
		printk ("reg %x val %x \n", data+val, *(uint32_t *)(data + val));
	*/

	power_down_reg = ddr_self_refresh_enable();
	//printk("calling pll resync\n");
	result = cls_dmu_pll_resync(p1div, p2div, ndiv,
			m1div, m2div, m3div, m4div,
			pll_num, frac, bclk_sel);

	/* following is code snippet given by Amit Pal, who thinks this is necessary after a cpu clock switch (email on 12/8/2010)
	*/
       //Reset DLL
        data = _CPU_READ_SINGLE(CFG_R_CFG_DDR_PWR_MEMADDR, AHB_BIT32);
        data &= ~CFG_F_DLL_Reset_MASK;
        _CPU_WRITE_SINGLE(CFG_R_CFG_DDR_PWR_MEMADDR,data,AHB_BIT32);

        data = _CPU_READ_SINGLE(DDR_R_DDR_DLL_MODE_MEMADDR,AHB_BIT32);
        data |= DDR_F_DDR_DLL_MODE_DLL_RESET_MASK;
        _CPU_WRITE_SINGLE(DDR_R_DDR_DLL_MODE_MEMADDR,data,AHB_BIT32);
        data &= ~DDR_F_DDR_DLL_MODE_DLL_RESET_MASK;
        _CPU_WRITE_SINGLE(DDR_R_DDR_DLL_MODE_MEMADDR,data,AHB_BIT32);

        //Wait for DLL lock
        do
        {
            data = 0;
            data = _CPU_READ_SINGLE(DDR_R_DDR_DLL_LOCK_BIT_MEMADDR,AHB_BIT32);
            data &= DDR_F_DDR_DLL_LOCK_BIT_DLL_LOCK_MASK;

            val = _CPU_READ_SINGLE(DDR_R_DDR_DLL_PHASE_LOW_MEMADDR, AHB_BIT32);
            val_2 = _CPU_READ_SINGLE(DDR_R_DDR_DLL_PHASE_HIGH_MEMADDR, AHB_BIT32);
        }   while((data == 0) & (val != 0x3F) & (val_2 != 0x3F));
	/*	*/

	ddr_self_refresh_disable(power_down_reg);

	/* dump ddr registers
	data = 0x85000;
	for (val = 0; val < 0x78; val += 4)
		printk("reg %x val %x \n", data+val, *(uint32_t *)(data + val));
	*/

	dma_resume(&state);
	/* Only write result after DDR re-enabled */
	*ret = result;
	//printk("result = %d %d\n", result, *ret);
	return 0;
}

int  spl_lf_monitor(int enable)
{
	if (enable)
		_CPU_RMW_OR_SINGLE(SPL_R_LFDETC_MEMADDR,SPL_F_LFDET_ENABLE_MASK, AHB_BIT32);
	else
		_CPU_RMW_AND_SINGLE(SPL_R_LFDETC_MEMADDR, ~SPL_F_LFDET_ENABLE_MASK, AHB_BIT32);
	return 0;
}

int  dmu_pll_enable(int pll)
{
	if (!pll)
		_CPU_RMW_AND_SINGLE(DMU_R_dmu_pll0_clk_param_MEMADDR,
				   ~DMU_F_dmu_pll0_pwrdn_MASK, AHB_BIT32);
	else
		_CPU_RMW_AND_SINGLE(DMU_R_dmu_pll1_clk_param_MEMADDR,
				   ~DMU_F_dmu_pll1_pwrdn_MASK, AHB_BIT32);
	return 0;
}

int  dmu_pll_disable(int pll)
{
	if (!pll)
		_CPU_RMW_OR_SINGLE(DMU_R_dmu_pll0_clk_param_MEMADDR,
				  DMU_F_dmu_pll0_pwrdn_MASK, AHB_BIT32);
	else
		_CPU_RMW_OR_SINGLE(DMU_R_dmu_pll1_clk_param_MEMADDR,
				  DMU_F_dmu_pll1_pwrdn_MASK, AHB_BIT32);
	return 0;
}

#if 0
static void pll_dump(void)
{
	u32 value;
	u32 m1div;


	/* PPLCLK_TAP1 */
	value = readl(IO_ADDRESS(DMU_R_dmu_pll0_clk_param_MEMADDR));
	printk(KERN_ERR "dmu-pll0-clk-param 0x%08x\n", value);
	printk(KERN_ERR "    p1div 0x%08x\n", (value >> 0) & 0x0f);
	printk(KERN_ERR "    p2div 0x%08x\n", (value >> 4) & 0x0f);
	printk(KERN_ERR "    ndiv  0x%08x\n", (value >> 8) & 0xff);
	printk(KERN_ERR "    bypen 0x%08x\n", (value >> 17) & 0x01);
	printk(KERN_ERR "    vco   0x%08x\n", (value >> 18) & 0x03);
	printk(KERN_ERR "    test en  %d\n", (value >> 20) & 0x01);
	printk(KERN_ERR "    test sel 0x%08x\n", (value >> 21) & 0x0f);
	printk(KERN_ERR "    pwrdn    %d\n", (value >> 25) & 0x01);
	printk(KERN_ERR "    enb      %d\n", (value >> 26) & 0x01);
	printk(KERN_ERR "    dreset   %d\n", (value >> 27) & 0x01);

	value = readl(IO_ADDRESS(DMU_R_dmu_pll0_frac_param_MEMADDR));
	printk(KERN_ERR "dmu-pll0-frac-param 0x%08x\n", value);
	printk(KERN_ERR "    frac 0x%08x\n", (value >> 0) & 0x00ffffff);
	printk(KERN_ERR "    byps 0x%08x\n", (value >> 24) & 0x1);
	printk(KERN_ERR "    mode 0x%08x\n", (value >> 25) & 0x07);
	printk(KERN_ERR "    pwrdn  %d\n", (value >> 28) & 0x01);
	printk(KERN_ERR "    dither %d\n", (value >> 29) & 0x01);

	value = readl(IO_ADDRESS(DMU_R_dmu_pll0_ch1_param_MEMADDR));
	m1div = (value >> 0) & 0xff;
	/* m1div==16 is encoded as 0 */
	if (m1div == 0)
		m1div = 16;
	printk(KERN_ERR "dmu-pll0-ch1-param 0x%08x\n", value);
	printk(KERN_ERR "    m1div 0x%08x\n", m1div);
	printk(KERN_ERR "    pwrdn 0x%08x\n", (value >> 8) & 0x01);
	printk(KERN_ERR "    delay 0x%08x\n", (value >> 9) & 0x03);
}
#endif

#if 0 //moved to kernel ...will fix latter 
/*
 * return true if the UDC is a wakeup source, false otherwise.
 */
int bcm5892_pm_udc_wakeup(void)
{
	return VIC1_MASK & VIC1_USB_UDC;
}
EXPORT_SYMBOL(bcm5892_pm_udc_wakeup);

/*
 * return true if the OTG is a wakeup source, false otherwise.
 */
int bcm5892_pm_otg_wakeup(void)
{
	return VIC1_MASK & VIC1_USB_OTG;
}
EXPORT_SYMBOL(bcm5892_pm_otg_wakeup);
#endif

static u32 bcm5892_get_pll_speed(int pll)
{
	u32 value;
	u32 ndiv;
	u32 m1div;
	u32 speed = 0;
	u32 sel = (pll == 0) ? 0 : 0x20;

	/* PPLCLK_TAP1 */
	value = readl(IO_ADDRESS(DMU_R_dmu_pll0_clk_param_MEMADDR + sel));
	ndiv = (value >> 8) & 0xff;
	value = readl(IO_ADDRESS(DMU_R_dmu_pll0_ch1_param_MEMADDR + sel));
	m1div = (value >> 0) & 0xff;

	/* m1div==16 is encoded as 0 */
	if (m1div == 0)
		m1div = 16;
	speed = (24000000UL * ndiv)/m1div;

	return speed;
}

static int bcm5892_set_pll_speed(int pll, u32 speed)
{
	u32 value;
	u32 status;
	u32 p1div, p2div, ndiv, m1div, m2div, m3div, m4div, frac;
	int ret;
	u32 sel = (pll == 0) ? 0 : 0x20;

	value = readl(IO_ADDRESS(DMU_R_dmu_pll0_clk_param_MEMADDR + sel));
	p1div = (value >> 0) & 0x0f;
	p2div = (value >> 4) & 0x0f;
	ndiv = (value >> 8) & 0xff;

	value = readl(IO_ADDRESS(DMU_R_dmu_pll0_ch1_param_MEMADDR + sel));
	m1div = (value >> 0) & 0xff;

	value = readl(IO_ADDRESS(DMU_R_dmu_pll0_ch2_param_MEMADDR + sel));
	m2div = (value >> 0) & 0xff;

	value = readl(IO_ADDRESS(DMU_R_dmu_pll0_ch3_param_MEMADDR + sel));
	m3div = (value >> 0) & 0xff;

	value = readl(IO_ADDRESS(DMU_R_dmu_pll0_ch4_param_MEMADDR + sel));
	m4div = (value >> 0) & 0xff;

	value = readl(IO_ADDRESS(DMU_R_dmu_pll0_frac_param_MEMADDR + sel));
	frac = (value >> 0) & 0x00ffffff;

#if 0
	printk(KERN_ERR "read PLL%d ndiv=%d frac=0x%08x mdiv=%d/%d/%d/%d\n",
		pll, ndiv, frac, m1div, m2div, m3div, m4div);
#endif

	if (speed == 400000000) {
		ndiv = 50;
		frac = 0;
		m1div = 3;
		m2div = 24;
		m3div = 48;
		m4div = 9;
	} else if (speed >= 383000000) {
		ndiv = 47;
		frac = 0x00eaaaaa;
		m1div = 3;
		m2div = 23;
		m3div = 46;
		m4div = 9;
	} else if (speed >= 360000000) {
		ndiv = 75;
		frac = 0;
		m1div = 5;
		m2div = 36;
		m3div = 72;
		m4div = 15;
	} else if (speed >= 333000000) {
		ndiv = 41;
		frac = 0x00aaaaaa;
		m1div = 3;
		m2div = 20;
		m3div = 40;
		m4div = 8;
	} else if (speed >= 300000000) {
		ndiv = 50;
		frac = 0;
		m1div = 4;
		m2div = 24;
		m3div = 48;
		m4div = 9;
	} else if (speed >= 266000000) {
		ndiv = 33;
		frac = 0x00555555;
		m1div = 3;
		m2div = 16;
		m3div = 32;
		m4div = 6;
	} else if (speed >= 240000000) {
		ndiv = 50;
		frac = 0;
		m1div = 5;
		m2div = 24;
		m3div = 48;
		m4div = 9;
	} else if (speed >= 200000000) {
		ndiv = 50;
		frac = 0;
		m1div = 6;
		m2div = 24;
		m3div = 48;
		m4div = 9;
	} else if (speed >= 133000000) {
		ndiv = 50;
		frac = 0;
		m1div = 9;
		m2div = 24;
		m3div = 48;
		m4div = 9;
	} else if (speed >= 120000000) {
		ndiv = 50;
		frac = 0;
		m1div = 10;
		m2div = 24;
		m3div = 48;
		m4div = 9;
	} else {
		/* 100MHz */
		ndiv = 50;
		frac = 0;
		m1div = 12;
		m2div = 24;
		m3div = 48;
		m4div = 9;
	}

#if 0
	printk(KERN_ERR "set  PLL%d ndiv=%d frac=0x%08x mdiv=%d/%d/%d/%d\n",
		pll, ndiv, frac, m1div, m2div, m3div, m4div);
#endif

        dmac_inv_range(&ret, (&ret+1));

#if 0
        status = call_secure_api(CLS_DMU_PLL_RESYNC, 11,
			p1div,
			p2div,
			ndiv,
			m1div,
			m2div,
			m3div,
			m4div,
			pll,		/* pll_num */
			frac,
			0,
			(uint32_t)virt_to_phys(&ret));
#endif

	status = dmu_pll_resync_wrapper(
			p1div,
			p2div,
			ndiv,
			m1div,
			m2div,
			m3div,
			m4div,
			pll,		/* pll_num */
			frac,
			0,
			&ret);

  //      dmac_inv_range(&ret, (&ret+1));

	if (status == 0) {
		if (ret != CLS_STATUS_OK) {
			printk(KERN_ERR PREFIX "DMA_PLL_RESYNC failed %d\n", ret);
			return -1;
		}
	}
	else {
		printk(KERN_ERR PREFIX "secure_call failed\n");
		return -1;
	}
	return speed;
}

static u32 bcm5892_get_cpu_speed(void)
{
	u32 value;
	u32 speed = 0;

	value = readl(IO_ADDRESS(DMU_R_dmu_clk_sel_MEMADDR));
	switch (value & 0x03) {
	case 0:
		/* REFCLK */
		speed = REFCLK_SPEED;
		break;
	case 1:
		/* PPLCLK_TAP1 */
		speed = bcm5892_get_pll_speed(0);
		break;
	case 2:
		/* BBLCLK */
		speed = BBLCLK_SPEED;
		break;
	case 3:
		/* SPL clock - varies from part to part */
		speed = SPLCLK_SPEED;
		break;
	}
	return speed;
}

static void bcm5892_set_cpu_speed(u32 speed)
{
	int ret;
	int status;
	u32 clk = PLLCLK_TAP1;

	if (speed == REFCLK_SPEED) {
		/* select REFCLK */
		clk = REFCLK;
	}
	else if (speed == BBLCLK_SPEED) {
		/* select BBLCLK */
		/* XXX - may get stuck here due to low-freq monitor */
		clk = BBLCLK;
	}
	else if (speed == SPLCLK_SPEED) {
		/* select SPLCLK */
		clk = SPLCLK;
	}
	else {
		/* select PLLCLK_TAP1 */
		clk = PLLCLK_TAP1;
		printk("bcm5892_set_cpu_speed -- 1\n");
		bcm5892_set_pll_speed(0, speed);
	}

//	dmac_inv_range(&ret, (&ret+1));

#if 0
	status = call_secure_api(CLS_DMU_CPUCLK_SEL, 4,
				 clk, CPUCLK, CPUCLK_SEL,
				 (uint32_t)virt_to_phys(&ret));
#endif
printk("bcm5892_set_cpu_speed -- 2\n");
	status = dmu_cpuclk_sel_wrapper(clk, CPUCLK, CPUCLK_SEL, &ret);
	printk("bcm5892_set_cpu_speed --3\n");
//	dmac_inv_range(&ret, (&ret+1));
	
	if (status == 0) {
		if (ret != CLS_STATUS_OK) {
			printk(KERN_ERR PREFIX "DMA_CPUCLK_SEL failed %d\n", ret);
		}
	}
	else {
		printk(KERN_ERR PREFIX "secure_call failed\n");
	}
}

static ssize_t bcm5892_cpu_speed_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	u32 speed;

	/*pll_dump(); */
	speed = bcm5892_get_cpu_speed();
	return sprintf(buf, "%u\n", speed);
}

static ssize_t bcm5892_cpu_speed_store(struct kobject *kobj, struct kobj_attribute *attr,
			  const char * buf, size_t n)
{
	u32 speed;

	if (sscanf(buf, "%u", &speed) != 1) {
		printk(KERN_ERR "%s: Invalid value\n", __func__);
		return -EINVAL;
	}
	if ((speed < 1) || (speed > 400000000)) {
		printk(KERN_ERR "%s: Invalid speed\n", __func__);
		return -EINVAL;
	}
	bcm5892_set_cpu_speed(speed);
	return n;
}

static struct kobj_attribute bcm5892_cpu_speed_attr =
	__ATTR(speed, 0644, bcm5892_cpu_speed_show, bcm5892_cpu_speed_store);


static ssize_t bcm5892_dmu_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	u32 blk1, blk2;

	blk1 = readl(IO_ADDRESS(DMU_R_dmu_pwd_blk1_MEMADDR));
	blk2 = readl(IO_ADDRESS(DMU_R_dmu_pwd_blk2_MEMADDR));
	return sprintf(buf, "0x%08x 0x%08x\n", blk1, blk2);
}

static ssize_t bcm5892_dmu_store(struct kobject *kobj, struct kobj_attribute *attr,
			  const char * buf, size_t n)
{
	u32 blk1, blk2;

	if (sscanf(buf, "0x%x 0x%x", &blk1, &blk2) != 2) {
		printk(KERN_ERR "%s: Invalid value\n", __func__);
		return -EINVAL;
	}
        call_secure_api(CLS_DMU_PWD, 2, blk1, blk2);
	return n;
}

static struct kobj_attribute bcm5892_dmu_attr =
	__ATTR(dmu_pwd_blk, 0644, bcm5892_dmu_show, bcm5892_dmu_store);

static int bcm5892_suspend_valid(suspend_state_t state)
{
	printk("bcm5892_suspend_valid\n");

	switch (state) {
	case PM_SUSPEND_MEM:
	case PM_SUSPEND_STANDBY:
		return 1;
	default:
		printk(KERN_ERR PREFIX "%s: unknown %d\n", __func__, (int) state);
		return 0;
	}
}

static int bcm5892_suspend_prepare(void)
{
	int val;
	
	printk("bcm5892_suspend_prepare\n");
	
#if 0
	reg_gpio_disable_interrupt( BCM5892_GPB1 );
	reg_gpio_iotr_set_pin_type( BCM5892_GPB1,GPIO_PIN_TYPE_INPUT_WITH_INTERRUPT );
	reg_gpio_clear_interrupt( BCM5892_GPB1 );
	reg_gpio_itr_set_interrupt_type_level( BCM5892_GPB1, GPIO_LEWEL_TRIGGER );		
	reg_gpio_itr_set_interrupt_type( BCM5892_GPB1, GPIO_LOW_LEVEL_INTERRUPT_TRIGGER );

	reg_gpio_enable_interrupt( BCM5892_GPB1 );
#endif	

	return 0;
}

static int bcm5892_enable_hw_assisted_ddr_refresh(int frequency_sel)
{
		printk(KERN_ERR "\n\nbcm5892_enable_hw_assisted_ddr_refresh entry...\r\n");
	//	call_secure_api(CLS_DDR_SELF_RERRESH_ENABLE, 0);
		ddr_self_refresh_enable();
#if 0
	struct clk_config {
		uint8_t p1; 
		uint8_t p2; 
		uint16_t ndiv_int; 
		uint32_t ndiv_frac; 
		uint16_t m1; 
		uint16_t m2; 
		uint16_t m3; 
		uint16_t m4; 
		uint8_t mdiv; 
		uint8_t hdiv; 
		uint8_t pdiv; 
		char *desc;
	};

	printk(KERN_ERR "\n\nbcm5892_enable_hw_assisted_ddr_refresh entry...\r\n");
	struct clk_config clk_config_table[] = {

		// p1, p2, ndiv_int, ndiv_frac, m1, m2, m3, m4, mdiv, hdiv, pdiv
		{ 1, 1, 50, 0, 3, 24, 48, 9, 3, 3, 2,           "hclk(133), mclk(133)"},
		{ 1, 1, 50, 0, 3, 24, 48, 9, 4, 3, 2,           "hclk(133), mclk(100)"},
		{ 1, 1, 50, 0, 3, 24, 48, 9, 4, 4, 2,           "hclk(100), mclk(100)"},
		{ 1, 1, 47, 0xEAAAAA, 3, 23, 46, 9, 3, 3, 2,    "hclk(128), mclk(128)"},
		{ 1, 1, 47, 0xEAAAAA, 3, 23, 46, 9, 4, 3, 2,    "hclk(128), mclk(96),"},
		{ 1, 1, 47, 0xEAAAAA, 3, 23, 46, 9, 4, 4, 2,    "hclk(96),  mclk(96),"},
		{ 1, 1, 75, 0, 5, 36, 72, 15, 3, 3, 2,          "hclk(120), mclk(120)"},
		{ 1, 1, 75, 0, 5, 36, 72, 15, 4, 3, 2,          "hclk(120), mclk(90),"},
		{ 1, 1, 75, 0, 5, 36, 72, 15, 4, 4, 2,          "hclk(90),  mclk(90),"},
		{ 1, 1, 41, 0xAAAAAA, 3, 20, 40, 8, 3, 3, 2,    "hclk(111)  mclk(111)"},
		{ 1, 1, 50, 0, 4, 24, 48, 9, 3, 3, 2,           "hclk(100)  mclk(100)"},
		{ 1, 1, 50, 0, 4, 24, 48, 9, 4, 3, 2,           "hclk(100)  mclk(75) "},
		{ 1, 1, 50, 0, 4, 24, 48, 9, 4, 4, 2,           "hclk(75)   mclk(75) "},
		{ 1, 1, 33, 0x555555, 3, 16, 32, 6, 2, 2, 2,    "hclk(133)  mclk(133)"},
		{ 1, 1, 50, 0, 6, 24, 48, 9, 4, 2, 2,           "hclk(100)  mclk(50) "},
		{ 1, 1, 50, 0, 5, 24, 48, 9, 3, 2, 2,           "hclk(120)  mclk(80) "},
		{ 1, 1, 50, 0, 5, 24, 48, 9, 4, 2, 2,           "hclk(120)  mclk(60) "},
		{ 1, 1, 50, 0, 5, 24, 48, 9, 3, 3, 2,           "hclk(80)   mclk(80) "},
		{ 1, 1, 50, 0, 5, 24, 48, 9, 4, 3, 2,           "hclk(80)   mclk(60) "},
		{ 1, 1, 50, 0, 5, 24, 48, 9, 4, 4, 1,           "hclk(60)   mclk(60) "},
		{ 1, 1, 50, 0, 6, 24, 48, 9, 2, 2, 2,           "hclk(100)  mclk(100)"},
		{ 1, 1, 50, 0, 6, 24, 48, 9, 4, 2, 2,           "hclk(100)  mclk(50) "},
		{ 1, 1, 50, 0, 4, 24, 48, 9, 3, 3, 2,           "hclk(100)  mclk(100)"},
		{ 1, 1, 50, 0, 6, 24, 48, 9, 3, 3, 1,           "hclk(66)   mclk(66) "},
		{ 1, 1, 50, 0, 6, 24, 48, 9, 4, 4, 1,           "hclk(50)   mclk(50) "},
		{ 1, 1, 50, 0, 9, 24, 48, 9, 2, 1, 2,           "hclk(133)  mclk(66) "},
		{ 1, 1, 50, 0, 10, 24, 48, 9, 2, 1, 2,          "hclk(120)  mclk(60) "},
		{ 1, 1, 50, 0, 12, 24, 48, 9, 2, 1, 2,          "hclk(100)  mclk(50) "}

	};

	volatile int i;
	unsigned int  power_down_reg, self_ref_cntr, timeout;
	volatile unsigned int data, val, val_2, mclk, aref;
	struct clk_config *conf;
	uint32_t row, num_rows=1;

	//val = _CPU_READ_SINGLE(DDR_R_CNTRLR_CONFIG_MEMADDR, AHB_BIT32);
	val = readl(IO_ADDRESS(DDR_R_CNTRLR_CONFIG_MEMADDR));
	printk(KERN_ERR "DDR_R_CNTRLR_CONFIG_MEMADDR value:%08x\n",val);
	row = ((val & DDR_F_CNTRLR_CONFIG_ROW_BITS_MASK) >> DDR_F_CNTRLR_CONFIG_ROW_BITS_R);
	printk(KERN_ERR "row value:%08x\n",row);
	while(row--)
	num_rows *= 2;
	printk(KERN_ERR "num_rows value:%08x\n",num_rows);
	if(frequency_sel != -1)
	{
		conf = &clk_config_table[frequency_sel]; 
		printk(KERN_ERR "ddr_change_frequency:%s\n",conf->desc);
	}

	//1.	Issue dummy read to take DDR out of self refresh
	//2.	Poll power down status register to make sure we are not in any power down (including self refresh) mode.
	//3.	WH Self Refresh in global config block
	//4.	Change clock dividers
	//5.	Resync PLL
	//6.	Reset DLL
	//7.	HW Exit self-refresh

	   
	//1        //dummy read from DDR
		data = *(volatile unsigned int *)0x40000000;

	//2.	Poll power down status register to make sure we are not in any power down (including self refresh) mode.
		do
		{
			data = _CPU_READ_SINGLE(DDR_R_POWER_DOWN_STATUS_MEMADDR,AHB_BIT32);
			data &= DDR_F_POWER_DOWN_STATUS_STATUS_MASK;
			printk(KERN_ERR "\n\nWaiting for DDR to switch to active mode...\r");
		}while (data);


	//3.	Issue auto-refresh

		data = _CPU_READ_SINGLE(DDR_R_SOFT_COMMANDS_CLIENT_MEMADDR,AHB_BIT32);
		data |= DDR_F_SOFT_COMMANDS_CLIENT_REFCmd_MASK;
		_CPU_WRITE_SINGLE(DDR_R_SOFT_COMMANDS_CLIENT_MEMADDR,data,AHB_BIT32);
		data &= ~DDR_F_SOFT_COMMANDS_CLIENT_REFCmd_MASK;
		_CPU_WRITE_SINGLE(DDR_R_SOFT_COMMANDS_CLIENT_MEMADDR,data,AHB_BIT32);


	//4		// 2. Put DDR in self refresh through global config block
		// (both i_hw_pwrdn_exit and i_hw_selfref_enter pins of EMI shall be set to �1�)
		data = _CPU_READ_SINGLE(CFG_R_CFG_DDR_PWR_MEMADDR,AHB_BIT32);
		data |= CFG_F_HW_Self_Ref_Enter_MASK; // hw_pwrdn_exit = bit 1, hw_selfref_enter = bit 0
		_CPU_WRITE_SINGLE(CFG_R_CFG_DDR_PWR_MEMADDR,data,AHB_BIT32);


	//5.	Change clock dividers
		if(frequency_sel != -1)
		{
			mclk = (24 *  conf->ndiv_int * conf->p2) / (conf->p1 * conf->m1 * conf->mdiv);
		}
		else
		{
			mclk = 24;
		}

		aref = ((mclk * 64000) / num_rows);
		aref -= 48; // keep margin of 48 clocks

		printk(KERN_ERR "\n\rmclk = %d, auto refresh = 0x%x\n\r", mclk, aref);
		_CPU_WRITE_SINGLE((START_DDR_CFG + 0x054), aref       , 0x2 ); // 

	    
		//If frequency_sel == -1 select 24Mhz clock

		if(frequency_sel == -1)
		{
			cls_dmu_cpuclk_sel(0, CPUCLK, CPUCLK_SEL);
			cls_dmu_cpuclk_sel(0, MCLK,   MCLK_SEL);
			cls_dmu_cpuclk_sel(0, HCLK,   HCLK_SEL);
			cls_dmu_cpuclk_sel(0, PCLK,   PCLK_SEL);
		}

		else
		{
				/*Configure clock divider first*/
			cls_dmu_cpuclk_sel(REFCLK, CPUCLK, CPUCLK_SEL);
			cls_dmu_cpuclk_sel((conf->mdiv - 1), MCLK, MCLK_SEL);
			cls_dmu_cpuclk_sel((conf->hdiv - 1), HCLK, HCLK_SEL);
			cls_dmu_cpuclk_sel((conf->pdiv - 1), PCLK, PCLK_SEL);

	//6.	Resync PLL
			cls_dmu_pll_resync(conf->p1, conf->p2, conf->ndiv_int, conf->m1, conf->m2, conf->m3, 0, 0, conf->ndiv_frac, 0); 
			cls_dmu_cpuclk_sel(PLLCLK_TAP1, CPUCLK, CPUCLK_SEL);
		}

	//7.	Reset DLL
		/*DDR DLL configuration*/
		data = _CPU_READ_SINGLE(CFG_R_CFG_DDR_PWR_MEMADDR, AHB_BIT32);
		data &= ~CFG_F_DLL_Reset_MASK;
		_CPU_WRITE_SINGLE(CFG_R_CFG_DDR_PWR_MEMADDR,data,AHB_BIT32);

		//Reset DLL
		data = _CPU_READ_SINGLE(DDR_R_DDR_DLL_MODE_MEMADDR,AHB_BIT32);
		data |= DDR_F_DDR_DLL_MODE_DLL_RESET_MASK;
		_CPU_WRITE_SINGLE(DDR_R_DDR_DLL_MODE_MEMADDR,data,AHB_BIT32);
		data &= ~DDR_F_DDR_DLL_MODE_DLL_RESET_MASK;
		_CPU_WRITE_SINGLE(DDR_R_DDR_DLL_MODE_MEMADDR,data,AHB_BIT32);
	   
		//Wait for DLL lock
		do
		{
			data = 0;
			data = _CPU_READ_SINGLE(DDR_R_DDR_DLL_LOCK_BIT_MEMADDR,AHB_BIT32);
			data &= DDR_F_DDR_DLL_LOCK_BIT_DLL_LOCK_MASK;

			val = _CPU_READ_SINGLE(DDR_R_DDR_DLL_PHASE_LOW_MEMADDR, AHB_BIT32);
			val_2 = _CPU_READ_SINGLE(DDR_R_DDR_DLL_PHASE_HIGH_MEMADDR, AHB_BIT32);
		}   while((data == 0) & (val != 0x3F) & (val_2 != 0x3F));

		if((val != 0x3F) && (val_2 != 0x3F))
		{
			printk(KERN_ERR "\n\rDDR DLL locked...!!!\n\r");
		}

		else
		{
			printk(KERN_ERR "\n\rDDR DLL saturated...!!!\n\r");
		}
#endif
		printk(KERN_ERR "\n\nbcm5892_enable_hw_assisted_ddr_refresh exit...\r\n");
		return 0;
}

static int bcm5892_disable_hw_assisted_ddr_refresh(void)
{
		printk(KERN_ERR "\n\nbcm5892_disable_hw_assisted_ddr_refresh entry...\r\n");
		printk(KERN_ERR "\n\nbcm5892_disable_hw_assisted_ddr_refresh exit...\r\n");
		
		return 0;
}

static int bcm5892_suspend_enter(suspend_state_t state)
{
	unsigned long flags;
	int ret;

	switch (state) 
	{
		case PM_SUSPEND_MEM:
		case PM_SUSPEND_STANDBY:
			break;
		default:
			printk(KERN_ERR PREFIX "%s: unknown %d\n", __func__, (int) state);
			return 0;	
	}

	ret = call_secure_kernel(SECURE_SLEEP, 0);	

	return 1;
}

static void bcm5892_suspend_finish(void)
{
	printk("bcm5892_suspend_finish\n");
}

static void bcm5892_suspend_recover(void)
{
}

int bcm5892_pm_udc_wakeup(void)
{
	return VIC1_MASK & VIC1_USB_UDC;
}
EXPORT_SYMBOL(bcm5892_pm_udc_wakeup);

/*
 * return true if the OTG is a wakeup source, false otherwise.
 */
int bcm5892_pm_otg_wakeup(void)
{
	return VIC1_MASK & VIC1_USB_OTG;
}
EXPORT_SYMBOL(bcm5892_pm_otg_wakeup);

struct platform_suspend_ops bcm5892_suspend_ops = {
	.valid		= bcm5892_suspend_valid,
	.prepare	= bcm5892_suspend_prepare,
	.enter		= bcm5892_suspend_enter,
	.finish		= bcm5892_suspend_finish,
	.recover	= bcm5892_suspend_recover
};

int bcm5892_pm_init(struct platform_suspend_ops *bcm5892_suspend_ops,struct kobj_attribute *bcm5892_cpu_speed_attr, struct kobj_attribute *bcm5892_dmu_attr)
{
	int error;	

	printk(KERN_ERR PREFIX "Initializing Power Management for BCM5892...\n");
	bcm5892_suspend_set_ops(bcm5892_suspend_ops);

	error = bcm5892_sysfs_create_file(bcm5892_get_power_kobj(), &bcm5892_cpu_speed_attr->attr);
	if (error)
		printk(KERN_ERR "sysfs_create_file failed: %d\n", error);

	error = bcm5892_sysfs_create_file(bcm5892_get_power_kobj(), &bcm5892_dmu_attr->attr);
	if (error)
		printk(KERN_ERR "sysfs_create_file failed: %d\n", error);

	return 0;
}
int __init bcm5892_pm_init_mod(void) 
{
	return bcm5892_pm_init( &bcm5892_suspend_ops, &bcm5892_cpu_speed_attr,  &bcm5892_dmu_attr);
}

module_init(bcm5892_pm_init_mod);

