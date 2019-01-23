/* module-verify.h: module verification definitions
 *
 * Copyright (C) 2007 TTI, Inc. All Rights Reserved.
 * Written by tony (lxyvslyr@yahoo.com.cn)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/*
  only used for elf file ,don't support  files using other file format 
                     by lijianjun
 */
 
#ifndef  _EXE_VERIFY_H_
#define  _EXE_VERIFY_H_

#include <linux/module.h>
#include <linux/types.h>
#include <asm/module.h>

extern int elf_verify(struct file *file);
extern int script_verify(struct file *file);
//extern int RAS_verify_signature(const char *signature, int sig_len ,char*digest);

extern int module_verify(struct module *mod, char *mod_buf, int mod_len);

/*
 * store the last executable file into this truct, used to quickly 
 * check the vaild of the executable program .
*/ 
struct exe_image {
	char image_name[256];
	int  image_size;
	char *image;
};

struct kek_status{
	int valid;		
	char image[2048];
};

//struct sig_entry ,shared the struct with user program.
#if 0
struct sig_entry{
	char file_name[256];        //the file name from which create the signature
	int  sig_size;		    //the length of signature,normally it is 65byte ,if the key is 1024bit
	char signature[80];         //the signature output ,
};//DSA signature
#endif


#if 0
struct sig_entry{
	char file_name[256];        //the file name from which create the signature
	int  sig_size;		    //the length of signature,normally it is 128byte,if the key is 1024bit
	char signature[128];        //the signature output ,
};//RSA signature
#endif

struct sig_entry{
	char signature[256];        //the signature output ,
};//RSA signature




#endif //_EXE_VERIFY_H_
