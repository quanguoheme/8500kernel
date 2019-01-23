/*
*  define some macro and constant for Strong_Lion device
*        Lijianjun:<>
*         
*         2011-03
*/
#ifndef __SECURE_KERNEL_API__
#define __SECURE_KERNEL_API__

#define MAXARGS		10	
#define INVALID_ID	20

struct mbox {

        volatile int valid; /* this valid bit indicates that test.c has sent a request. This field is
                            SET by test.c and cleared by this routine after being serviced */
        volatile int id;    /* this field is the index into the array of functions contained in libarm.a
                            This field is INITialized by test.c before SETting valid */

        volatile int ret_validen; /* return valid enable. This bit is SET and cleared by the ARM code */
        volatile int ret_valid; /* The return valid bit indicates to the test.c program that the
                                  library routine is complete and any return value is available
                                  this bit is SET by the ARM processor and cleared by test.c */
        unsigned int  return_value; /* returns a phoenix status code as defined in the header file global/include/phx2_sw */
        volatile unsigned args[MAXARGS]; /* One of two ways of passing arguments to api functions */
        volatile int num_args;     /* The mail box needs to know the number of arguments being passed */
        volatile int *extdata;     /* data pointer to data area for API's */
        volatile int *endsim;     /* Graceful exit from while loop */
        volatile int mbalive;
        volatile int error;
        volatile int mode;      /* flag indicating whether in open or secure mode. 0 = secure, 1 = open */
};

#define SECURE_SLEEP				0
#define SECURE_GET_SECURITY_FLAG	1
#define SECURE_KEK_KEK_ENCRYPT	2
#define SECURE_KEK_KEK_DECRYPT	3

int call_secure_kernel(int api_id,int argc,...);

#endif //__SECURE_KERNEL_API__

