#ifndef _SCL_SERVICE_RARAS_H_
#define _SCL_SERVICE_RARAS_H_

#define DES_8	0			//single DES
#define DES_16	1			// 3DES 16 bytes key
#define DES_24	2			// 3DES 24 bytes key

#define ENCRYPT	0
#define DECRYPT	1

//0x180000 -- 0x1bffff
#define PARAMETER_ADDR_START	0x1BE000		//maxium size is  0x2000    8Kbytes 

#define PARAMETER_BUF1_OFFSET		(0x1c0000 - 1024 - PARAMETER_ADDR_START)		// 1k bytes
#define PARAMETER_BUF2_OFFSET		(0x1c0000 - 2048 - PARAMETER_ADDR_START)		// 1k bytes
#define PARAMETER_BUF3_OFFSET		(0x1c0000 - 3072 - PARAMETER_ADDR_START)		// 1k bytes
#define PARAMETER_BUF4_OFFSET		(0x1c0000 - 4096 - PARAMETER_ADDR_START)		// 1k bytes

typedef struct _paras_kek_des_encrypt {
	int status;		//output 
	
	int des_type;	      	//input, 
	int key_id;		//input,
	int in_len;		//input
	char *in_data;	//input
	
	char *out_data;	//output
}paras_kek_des_encrypt;


typedef struct _paras_kek_des_decrypt {
	int status;		//output 
	
	int des_type;	      	//input, 
	int key_id;		//input,
	int in_len;		//input
	char *in_data;	//input
	
	char *out_data;	//output
}paras_kek_des_decrypt;


typedef struct _paras_get_real_random {
	int status;		//output 

	char *random;	//output	
	char len;			//input
}paras_get_real_random;


typedef struct _paras_set_not_security_run {
	int status;		//output 

	char *flag;		//input	
	char len;			//input
}paras_set_not_security_run;


typedef struct _paras_not_security_run {
	int status;		//output 
	
	int allowed;		//output
}paras_not_security_run;


typedef struct _paras_rtc_settime {
	int status;		//output 
	
	int sec;			//input
}paras_rtc_settime;

typedef struct _paras_rtc_gettime {
	int status;		//output 
	
	int sec;			//output
}paras_rtc_gettime;

#endif

