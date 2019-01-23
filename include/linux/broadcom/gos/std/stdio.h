/*****************************************************************************
* Copyright 2009 Broadcom Corporation.  All rights reserved.
*
* Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2, available at
* http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a
* license other than the GPL, without Broadcom's express prior written
* consent.
*****************************************************************************/
#if !defined( STD_STDIO_H )
#define STD_STDIO_H

/* ---- Include Files ---------------------------------------------------- */
#include <linux/broadcom/gos/gos.h>

/* ---- Constants and Types ---------------------------------------------- */
#ifdef printf
#undef printf
#endif

#define printf(fmt, args...) GOS_DEBUG(GOS_LOG_CONSOLE_FLAG, fmt, ## args)

#ifdef puts
#undef puts
#endif

#define puts(str) GOS_DEBUG(GOS_LOG_CONSOLE_FLAG, str)

/* ---- Variable Externs ------------------------------------------------- */

/* ---- Function Prototypes ---------------------------------------------- */
extern int sprintf(char * buf, const char *fmt, ...);

#endif   /* STD_STDIO_H */