/*
 *
 * Copyright (C) 2006-2010 TTI-china, Inc.
 *
 * Author: lxyvslyr <lxyvslyr@yahoo.com.cn.>
 * Date  : $Date: 2007/06/24 06:25:59 $
 *
 * $Revision: 1.1.1.1 $
 *
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
 *
 * 
*****************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h> 
#include <linux/elf.h>
#include <linux/crypto.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/file.h>
#include <linux/mount.h>
#include <linux/dirent.h>
#include <linux/unistd.h>
#include <linux/fsnotify.h>
 
#include <crypto/mpi.h>
#include <crypto/sha1.h>
#include <crypto/scl_key.h>
#include <crypto/sig_verify.h>
#include <linux/delay.h>
#include <linux/strong_lion_def.h>

char sig_init=0;
struct kek_status keks[3];

int pinpad_lock=0;
EXPORT_SYMBOL(pinpad_lock);

LIST_HEAD(pinpad_list);
EXPORT_SYMBOL(pinpad_list);

int pinpad_flush_lcd;
EXPORT_SYMBOL(pinpad_flush_lcd);

#define TEMP_MEM_SIZE	64*1024
#define TEMP_MEM_ORDER	get_page_order(TEMP_MEM_SIZE)

//#define DEBUG_VERY  1

unsigned char fix_mod[]={
	#include "rsa_m.h"
};

unsigned char fix_pub_e[]={
	#include "rsa_pub_e.h"
};

static char key_head[3][256] = {
				{"/usr/etc/signature/key0hdr.bin"},
				{"/usr/etc/signature/key1hdr.bin"},
				{"/usr/etc/signature/key2hdr.bin"},
				};

///open a readable file in the kernel
struct file *open_readable_file(const char *name)
{
	struct file *file;
	int err;

	file = do_filp_open(AT_FDCWD, name,
				O_LARGEFILE, FMODE_READ,
				MAY_READ | MAY_OPEN);
	if (IS_ERR(file))
		goto out;

	err = -EACCES;
	if (!S_ISREG(file->f_path.dentry->d_inode->i_mode))
		goto exit;

	if (file->f_path.mnt->mnt_flags & MNT_NOEXEC)
		goto exit;

	fsnotify_open(file->f_path.dentry);
out:
	return file;

exit:
	fput(file);
	return ERR_PTR(err);
}

static int is_keyhead_valid(int keyheda_index)
{
	struct file *keyhead_file;
	int file_len;
	char *f_temp;
	int ret;

	if (!keks[keyheda_index].valid)
		return 0;

//	printk("%d is ok\n",keyheda_index);

	f_temp = (char *)__get_free_page(GFP_KERNEL);
	if  ( !f_temp )
		return 0;
		
	memset(f_temp,0,PAGE_SIZE);

	keyhead_file = open_readable_file(key_head[keyheda_index]);
	if (IS_ERR(keyhead_file))
	{
		printk("can't open the kek signature file :%s\n",key_head[keyheda_index]);
		free_page((unsigned long)f_temp);
		return 0;
	}
			
	file_len = keyhead_file->f_dentry->d_inode->i_size;
	ret = kernel_read(keyhead_file, 0, f_temp, file_len);
	if (ret != file_len)
	{
		printk("read failed\n");
		ret = 0;
		goto out;
	}

	if (memcmp(f_temp, keks[keyheda_index].image,file_len) != 0)
	{
		ret = 0;
		goto out;
	}

	ret = 1;
out:
	fput(keyhead_file);
	free_page((unsigned long)f_temp);

	return ret;
}

static void mark_keyhead_valid(int keyhead_index)
{
	struct file *keyhead_file;
	int file_len;
	int ret;

//	printk("keyhead_index :%d\n",keyhead_index);

	if (keyhead_index > 3)
		return ;

	keyhead_file = open_readable_file(key_head[keyhead_index]);
	if (IS_ERR(keyhead_file))
	{
		printk("can't open the kek signature file :%s\n",key_head[keyhead_index]);
		return;
	}
			
	file_len = keyhead_file->f_dentry->d_inode->i_size;
	
	ret = kernel_read(keyhead_file, 0, keks[keyhead_index].image, file_len);
	fput(keyhead_file);

	if (ret < 0)
		printk("kernel_read failed , err :%d\n",ret);
	else
		keks[keyhead_index].valid = 1;

	fput(keyhead_file);

	return ;
}


#if 0
/**
  * dst = n ^a mod b
  */
void mpi_rsatrans(pmpi_t dst, pmpi_t n, pmpi_t a, pmpi_t b)
#endif

static void get_hash_from_MPI(MPI result, char* hash)
{
	int i;

	for (i=0;i<5;i++)
	{
		unsigned long temp;

		temp = result->d[4-i];
		hash[4*i+3] = temp & 0xff;
		hash[4*i+2] = (temp >> 8) & 0xff;
		hash[4*i+1] = (temp >> 16) & 0xff;
		hash[4*i+0] = (temp >> 24) & 0xff;
	}
}

/*
	return value:
	0	go on
	1 	return 0 because security 
	2	some error 
*/
char tty_name_buf[1024];
int do_pinpad_tty(struct file *file)
{
	char *file_name;
	char *xxx;
	pinpad_process_t *person;

	if (!pinpad_lock)
		return 0;
	
	file_name = d_path(&file->f_path,tty_name_buf, 1024 - 1);
	if (IS_ERR(file_name))
	{	
		printk("get full filename failed\n");
		return 2;
	}

	xxx = strstr(file_name,"/dev/tty");
	if (!xxx)
		return 0;

	if ((*(xxx+1)==0) || (*(xxx+1)== '0') || (*(xxx+1)== '1') || 
		(*(xxx+1)=='2') || (*(xxx+1)== '3') || (*(xxx+1)== '4') || 
		(*(xxx+1)=='5') || (*(xxx+1)== '6') || (*(xxx+1)== '7') || 
		(*(xxx+1)=='8') || (*(xxx+1)== '9'))
	{
		//pinpad family process
		list_for_each_entry(person, &pinpad_list, list)
		{
			if ((person->process == current) && (person->tsk_pid == current->pid))
				return 0;
		}

		//not the pinpad family
		return 1;
	}

	return 0;
}

static int last_is_pinpad = 0;

static int add_to_pinpad_family(struct task_struct *new)
{
	pinpad_process_t *person;

	list_for_each_entry(person, &pinpad_list, list)
	{
		if ((person->process == new) && (person->tsk_pid == new->pid))
			return 0;
	}	
	
	person = kmalloc(sizeof(*person), GFP_KERNEL);
	if(person)
	{
		person->process = new;
		person->tsk_pid = new->pid;
		list_add(&person->list, &pinpad_list);
//		printk("current:0x%x\n",new);
		printk("add process with PID: %d to pipad family\n",new->pid);
		return 0;
	}

	return -ENOMEM;	
}

int remove_from_pinpad_family(struct task_struct *old)
{
	pinpad_process_t *person;

	list_for_each_entry(person, &pinpad_list, list)
	{
		if ((person->process == old) && (person->tsk_pid == old->pid))
		{
			printk("remove process with PID: %d from pipad family\n",old->pid);

			list_del(&person->list);
			kfree(person);
#if 0
			printk("the remains in the pinpad list:");
			list_for_each_entry(person, &pinpad_list, list)
			{
				printk("task_struct:0x%x, pid:%d\n",person->process,person->tsk_pid);
			}
#endif
			return 0;
		}
	}

	return 0;
}
	
int is_pinpad_proc(struct task_struct *the_proc)
{
	pinpad_process_t *person;

	list_for_each_entry(person, &pinpad_list, list)
	{
		if ((person->process == the_proc) && (person->tsk_pid == the_proc->pid))
			return 1;
	}

	return 0;
}
EXPORT_SYMBOL(is_pinpad_proc);

/*
   verify the signature .return 0 ,if success . return 1 ,if faield\
	signature :the raw signature 
	sig_len: the lenth of raw signature
	digest:   the hash data we'll sign
*/

#if 1

#if 0
struct key_header {
	unsigned char number;			// key number is used to decrypt tti key file
	unsigned char softlen[4];		// software length
	unsigned char signature[256];		// 
}__attribute__ ((packed));
#endif

int RAS_verify_signature(const char *signature, int sig_len ,char* digest)
{
	struct key_entry pub_key ;
	MPI a=NULL,b=NULL,n=NULL,result=NULL;
	int i,j;
	char data_temp[130]; //rsa result + 2 bytes 
	unsigned int len;
	char hash[20];
	int ret = -1;
	struct file *key_file=0;

	for(j=0;j<3;j++)
	{
		unsigned char *N_p;

		if (!is_keyhead_valid(j))
			continue;

//		printk("to open %s\n",key_head[j]);
		key_file = open_readable_file(key_head[j]);
		if (IS_ERR(key_file))
		{
			printk("cann't open %s\n",key_head[j]);
			goto error_out1;
		}

		kernel_read(key_file, 0, (char*)&pub_key, sizeof(struct key_entry));

		if ( pub_key.magic != SCL_KEY_MAGIC )
		{
//			printk("%s, invalid Key Magic: 0x%x\n",key_head[j],pub_key.magic);
			fput(key_file);
			continue;
		}

		if ((pub_key.priority != SCL_PINPAD_KEY) && (pub_key.priority != SCL_NORMAL))
		{
	//		printf("key priority %d is err\n",pub_key.priority);
			fput(key_file);
			continue;
		}

		data_temp[1] = 1024 & 0xff;
		data_temp[0] = (1024 >> 8) & 0xff;
		for(i=0;i<128;i++)
		{
//			data_temp[i+2] = signature[128-1-i];
			data_temp[i+2] = signature[i];
		}

#ifdef DEBUG_VERY	
		printk("signature that will be decryptoed:\n");
		for(i=0;i<128;i++)
		{
			printk("%02x ",signature[i]);
			if ( (i+1)%25 == 0)
				printk("\n");
		}
		printk("\n");
#endif

		len = 130;
		n = mpi_read_from_buffer(data_temp, &len);

		a = mpi_alloc_set_ui(pub_key.D_key);

//		printk("size of N_key :%d\n",key_p->key_size/8);
		N_p = pub_key.N_key;
		data_temp[1] = 1024 & 0xff;
		data_temp[0] = (1024 >> 8) & 0xff;
		for(i=0;i<128;i++)
		{
			data_temp[i+2] = N_p[128-1-i];
		//	data_temp[i+2] = N_p[i];
		}			

		len = 130;
		b = mpi_read_from_buffer(data_temp, &len);

#ifdef DEBUG_VERY
		printk("D_key:%x\n",pub_key.D_key);
		printk("N_key:\n");
		for(i=0;i<pub_key.key_size/8;i++)
		{
			printk("%02x ",*(N_p+i));
			if ( (i+1)%25 == 0)
				printk("\n");
		}
		printk("\n");
#endif		
		result  = mpi_alloc(mpi_get_nlimbs(b));
		if (!result) 
			goto error_out2;

 		// result = n ^ a mod b 
		if ( mpi_powm(result,n,a,b) < 0)
			goto error_out2;

		get_hash_from_MPI(result,hash);

#ifdef DEBUG_VERY		
		printk("decryptoed MPI result, nlimbs:%d:\n",result->nlimbs);
		for(i=0;i<32;i++)
		{
			printk("%08x",result->d[i]);
		}
		printk("\n");

		printk("raw hash:\n");
		for(i=0;i<20;i++)
		{
			printk("%02x ",digest[i]);
		}
		printk("\n");
		
		printk("decryptoed hash:\n");
		for(i=0;i<20;i++)
		{
			printk("%02x ",hash[i]);
		}
		printk("\n");
#endif
		for(i=0;i<20;i++)
		{
			if ( digest[i] != hash[i] )
			{
				break;
			}
	 	}

		if ( i>=20 )
		{
#ifdef DEBUG_VERY
			printk("priority is %d\n",pub_key.priority);
#endif		
			if (pub_key.priority == SCL_PINPAD_KEY)
			{
				if ( add_to_pinpad_family(current) !=0 )
					goto error_out1;
				last_is_pinpad = 1;  //used for quick verify menthod
			}
			else
			{
				last_is_pinpad = 0;
			}
			
			ret = pub_key.priority;
			goto error_out2;
		}

		fput(key_file);
	}
	
error_out2:
	if (key_file)
		fput(key_file);

error_out1:
	mpi_free(a);
	mpi_free(b);
	mpi_free(n);
	mpi_free(result);

	return ret;
}
#endif 

inline static int get_page_order(int len)
{
	int temp,i;
	int order=0;
	
	temp = PAGE_SIZE; 
     
	for(i=0;;i++)
	{
		if (len <= temp)
			break;
		order += 1;
		temp *= 2;
	}
	
	return order;
}

//char f_temp[512];
#define MAX_FILE_NAME_LEN	128

static struct sig_entry* get_sig_entry_from_file(char *file_name)
{
	int file_len, ret;
	struct sig_entry* entry;
	
	struct file *sig_file=0;
	char sig_file_name[MAX_FILE_NAME_LEN];
	char *p_start;
	char *f_temp;

//	printk("get entry of file %s\n",file_name);
	entry = (void *)__get_free_page(GFP_KERNEL);
	if  ( !entry )
		return 0;
		
	f_temp = (char *)__get_free_page(GFP_KERNEL);
	if  ( !f_temp )
		goto out1;
	memset(f_temp,0,PAGE_SIZE);	

	if ( (p_start = strrchr(file_name,'/')) == NULL )
		goto out2;
	
	memset(sig_file_name,0,MAX_FILE_NAME_LEN);
	strcpy(sig_file_name,p_start);
//	printk("filename without path:%s\n",sig_file_name+1);

	strcpy(f_temp,file_name);
	strcat(f_temp,".sign");
	
#if 0
	if ( (p_end = strrchr(sig_file_name,'.')) == NULL )
	{
		strcpy(f_temp,file_name);
		strcat(f_temp,".sign");
	}
	else
	{
		memcpy(f_temp,file_name,p_start-file_name+1);
		strncat(f_temp,sig_file_name,p_end-sig_file_name);
		strcat(f_temp,".sign");
	}
#endif
			
//	printk("want to open the signature file :%s\n",f_temp);
	sig_file = open_readable_file(f_temp);
	if (IS_ERR(sig_file))
	{
		printk("can't open the signature file :%s\n",f_temp);
			goto out2;
	}
		
	file_len = sig_file->f_dentry->d_inode->i_size;
	if (file_len % sizeof(struct sig_entry)) 
	{
		printk("invaild signature file size\n");
			goto out3;
	}
	
//	printk("sig_file->f_mode :%x\n",sig_file->f_mode);

	memset(entry,0,PAGE_SIZE);	
	ret = kernel_read(sig_file, 0, (char*)entry, sizeof(struct sig_entry));
	if (ret < 0)
	{
		printk("kernel_read failed , err :%d\n",ret);
		goto out3;
	}

	fput(sig_file);
	free_page((unsigned long)f_temp);
	return entry;

out3:
	fput(sig_file);
out2:
	free_page((unsigned long)f_temp);
out1:
	free_page((unsigned long)entry);
	
	return 0 ;
}
#undef MAX_FILE_NAME_LEN

static void put_sig_entry(struct sig_entry* entry)
{
	free_page((unsigned long)entry);
}

static int digest_update(sha1 *base, unsigned char * digest, char *file_name)
{   
	int file_size ;
	char *buffer;
	int toread;
	int offset = 0, ret = -1;
	struct file *exe_file;
	
	exe_file = open_readable_file(file_name);
	if (IS_ERR(exe_file))
	{
		printk("can't open the file :%s!\n",file_name);
		return -1;
	}
	
	file_size = exe_file->f_dentry->d_inode->i_size;
	
	//get 64kbyte buffer to store the file context 
	buffer = (char *)__get_free_pages(GFP_KERNEL,TEMP_MEM_ORDER);  
	if  ( !buffer )
		return -ENOMEM;

	while (file_size)
	{
		if ( file_size >= 64 )
			toread = 64;
		else
			toread = file_size;
			
		ret = kernel_read(exe_file, offset, buffer, toread);
		if (ret < 0)
			goto error_out;

    		/* Process in chunks of 64 until we have less than 64 bytes left. */
    		if (toread == 64)
    		{
        		sha1_mid(base, buffer);
    		}
		else
		{
			sha1_end(base, buffer, toread, digest);
		}	
		
		offset += toread;
		file_size -= toread;
	}

error_out:
	free_pages((unsigned long)buffer,TEMP_MEM_ORDER);
	fput(exe_file);
	
	return ret;
}

static void setup_kek_status(void)
{
	char *kek_file_name;
	struct file *kek_file=0;
	int len,ret;
	int i,k;

	MPI a=NULL,b=NULL,n=NULL,result=NULL;
	unsigned char mpi_temp[150];
	unsigned char sig_temp[150];

	unsigned char cal_digest[30];
	unsigned char dec_digest[30];
	sha1 base;

	kek_file_name = (char *)__get_free_pages(GFP_KERNEL,TEMP_MEM_ORDER);  
	if  ( !kek_file_name )
		return ;

	memset(keks,0,3*sizeof(struct kek_status));

	len = sizeof(fix_mod);

	mpi_temp[1] = (len*8) & 0xff;
	mpi_temp[0] = ((len*8) >> 8) & 0xff;
	for(i=0;i<len;i++)
	{
//		mpi_temp[i+2] = fix_mod[128-1-i];
		mpi_temp[i+2] = fix_mod[i];
	}

	len += 2 ;
	b = mpi_read_from_buffer(mpi_temp, &len);

	len = sizeof(fix_pub_e);

	mpi_temp[1] = (len*8) & 0xff;
	mpi_temp[0] = ((len*8) >> 8) & 0xff;
	for(i=0;i<len;i++)
	{
//		mpi_temp[i+2] = fix_pub_e[3-1-i];
		mpi_temp[i+2] = fix_pub_e[i];
	}

	len += 2;
	a = mpi_read_from_buffer(mpi_temp, &len);

	for(i=0;i<3;i++)
	{
		sha1_start(&base);

		if (digest_update(&base,cal_digest,key_head[i]) < 0)
			continue;

		strcpy(kek_file_name,key_head[i]);
		strcat(kek_file_name,".sign");

		kek_file = open_readable_file(kek_file_name);
		if (IS_ERR(kek_file))
		{
			printk("can't open the kek signature file :%s\n",kek_file_name);
			continue;
		}
			
		len = kek_file->f_dentry->d_inode->i_size;
		if (len % sizeof(struct sig_entry))
		{
			printk("invaild kek signature file size\n");
			fput(kek_file);
			continue;
		}
	
		ret = kernel_read(kek_file, 0, sig_temp, sizeof(struct sig_entry));
		fput(kek_file);
		
		if (ret < 0)
		{
			printk("kernel_read failed , err :%d\n",ret);
			continue;
		}

		len = sizeof(fix_mod);

		mpi_temp[1] = (len*8) & 0xff;
		mpi_temp[0] = ((len*8) >> 8) & 0xff;
		for(k=0;k<len;k++)
		{
		//	mpi_temp[i+2] = sig_temp[128-1-k];
			mpi_temp[k+2] = sig_temp[k];
		}

		len += 2;
		n = mpi_read_from_buffer(mpi_temp, &len);

		result  = mpi_alloc(mpi_get_nlimbs(b));
		if (!result)
		{
			mpi_free(n);
			continue;
		}

 		// result = n ^ a mod b
		if ( mpi_powm(result,n,a,b) < 0)
		{	mpi_free(n);
			mpi_free(result);
			continue;
		}

		get_hash_from_MPI(result,dec_digest);

		if (memcmp(cal_digest, dec_digest, 20) == 0)
			mark_keyhead_valid(i);
		else
			printk("invalid %s\n",kek_file_name);

		mpi_free(n);
		mpi_free(result);
	}

	mpi_free(a);
	mpi_free(b);

	free_pages((unsigned long)kek_file_name,TEMP_MEM_ORDER);
 
	return ;
}

static int exec_verify_signature(struct file *file)
{
	int i, len;
	int ret = -1;
	const char *sig;
	struct sig_entry *s_entry;
	char *file_name;
	char *buf;
	unsigned char digest[30];
	sha1 base;
	
	if (!sig_init)
	{
		setup_kek_status();
		sig_init = 1;
	}
		
	buf = (char *) __get_free_page(GFP_KERNEL);
	if (!buf)
		goto out0;
	
	file_name = d_path(&file->f_path,buf, PAGE_SIZE - 1);
	if (IS_ERR(file_name))
	{	
		printk("get full filename failed\n");
		goto out1;
	}

//	printk("full path of the file :%s\n",file_name);
	if ( (s_entry = get_sig_entry_from_file(file_name)) == NULL )
	{
		printk("can't find the vaild signature for file: %s!\n",file_name);
		goto out1;
	}

//	len = s_entry->sig_size;
//	sig = s_entry->signature;

	len = 128;
	sig = s_entry->signature;

	sha1_start(&base);

	if (digest_update(&base,digest,file_name) < 0)
		goto out2;
	
	/* do the actual signature verification */
//	i = ksign_verify_signature(sig, len, evdata.digest);

	i = RAS_verify_signature(sig,len,digest);
	if ((i == 0) || ( i == 1) || ( i == 2))   //pinpad ,system commond and normal applications
	{
	//	updata_quick_verify(file_name);
		ret = 1;
	}
out2:
	put_sig_entry(s_entry);
out1:
	free_page((unsigned long)buf);
out0:
	return ret;
}

int elf_verify(struct file *file)
{
	return (exec_verify_signature(file));
} 

int script_verify(struct file *file)
{
	return (exec_verify_signature(file));
}

char mod_dir_path[] = "/usr/lib/modules/2.6.32.9/";
int module_verify(struct module *mod, char *mod_buf, int mod_len)
{
	int i, len;
	int ret = -1;
	const char *sig;
	struct sig_entry *s_entry;
	char *file_name;
	unsigned char digest[30];
//	unsigned char cur_digest[30];
	sha1 base;
		
	file_name = (char *) __get_free_page(GFP_KERNEL);
	if (!file_name)
		return -1;

	sprintf(file_name,"%s%s.ko",mod_dir_path,mod->name);
//	printk("full path of the module should be :%s\n",file_name);
	if ( (s_entry = get_sig_entry_from_file(file_name)) == NULL )
	{
	//	printk("can't find the vaild signature for file: %s\n",file_name);
		goto out0;
	}

//	len = s_entry->sig_size;
//	sig = s_entry->signature;

	len = 128;
	sig = s_entry->signature;

	sha1_start(&base);
	if (digest_update(&base,digest,file_name) < 0)
	{
		ret = -1;
		goto out1;
	}

#if 0
	sha1_start(&base);
	sha1_end(&base, mod_buf, mod_len, cur_digest);

	if (memcmp(digest, cur_digest, 20) != 0)
	{
		printk("invalid module!\n");
		ret = -1;
		goto out1;
	}
#endif

	/* do the actual signature verification */
	i = RAS_verify_signature(sig,len,digest);
	if ((i == 0) || ( i == 1) || ( i == 2))   //pinpad ,system commond and normal applications
	{
		ret = 1;
	}

out1:
	put_sig_entry(s_entry);
out0:
	free_page((unsigned long)file_name);
	
	return ret;
}
