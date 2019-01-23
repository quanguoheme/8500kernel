
#ifndef _SCL_KEY_H_
#define _SCL_KEY_H_


#define SCL_PINPAD_KEY		0      //the key used to verify the pinpad program
#define SCL_SYS_KEY		1      //the key used to verify all the system code,like modules
#define SCL_NORMAL		2      //the key used to verify general pogrames

#define SCL_KEY_MAGIC	0xaa55aa55

struct  key_entry {
	unsigned int     magic;       //the magic of the TTI key ,should be TTI_KEY_MAGIC
	unsigned int     priority;    //the priority of this key entry
	unsigned char  	 reserve[16]; //reserved for the future use
	unsigned int     key_size;    //size of  this modulus
	unsigned int     D_key;       //exponent of the  key
	unsigned char    N_key[128];  //modulus of the key
};
//size of the key_entry is 150 bytes.


#endif //_SCL_KEY_H_





