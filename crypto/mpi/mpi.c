#include <crypto/mpi.h>

/*
  * dst > source => 1
  * dst = source => 0
  * dst < source => -1
  **/
int mpi_cmp(pmpi_t dst, pmpi_t source)
{
	int i;

    if(dst->m_nLength>source->m_nLength)return 1;
    if(dst->m_nLength<source->m_nLength)return -1;
    for(i=dst->m_nLength-1;i>=0;i--)
    {
        if(dst->m_ulValue[i]>source->m_ulValue[i])return 1;
        if(dst->m_ulValue[i]<source->m_ulValue[i])return -1;   
    }
    return 0;
} 

void mpi_mov(pmpi_t dst, mpi_t source)
{
	int i;

	dst->m_nLength=source.m_nLength;
    for(i=0;i<MPI_MAXLEN;i++) dst->m_ulValue[i]=source.m_ulValue[i];
}

void mpi_mov_int64(pmpi_t dst, unsigned __int64 source)
{
	int i;
	
    if(source>0xffffffffL)
    {
        dst->m_nLength=2;
        dst->m_ulValue[1]=(unsigned long)(source>>32);
        dst->m_ulValue[0]=(unsigned long)source;
    }
    else
    {
        dst->m_nLength=1;
        dst->m_ulValue[0]=(unsigned long)source;
    }
    for(i=dst->m_nLength;i<MPI_MAXLEN;i++) dst->m_ulValue[i]=0;
}

void mpi_add(pmpi_t dst, pmpi_t a, pmpi_t b)
{
	unsigned i;
    unsigned carry=0;
    unsigned __int64 sum=0;

	mpi_mov(dst, *a);
    if(dst->m_nLength<b->m_nLength) dst->m_nLength=b->m_nLength;
    for(i=0;i<dst->m_nLength;i++)
    {
        sum=b->m_ulValue[i];
		sum=sum+dst->m_ulValue[i]+carry;
        dst->m_ulValue[i]=(unsigned long)sum;
        carry=(unsigned)(sum>>32);
    }
    dst->m_ulValue[dst->m_nLength]=carry;
    dst->m_nLength+=carry;
}

void mpi_add_int32(pmpi_t dst, pmpi_t a, unsigned long b)
{
    unsigned __int64 sum;

	mpi_mov(dst, *a);
    sum=dst->m_ulValue[0];
	sum+=b;
    dst->m_ulValue[0]=(unsigned long)sum;
    if(sum>0xffffffffL)
    {
		unsigned i=1;
        while(dst->m_ulValue[i]==0xffffffff) {dst->m_ulValue[i]=0;i++;}
        dst->m_ulValue[i]++;
        if(dst->m_nLength==i) dst->m_nLength++;		// +++ 
    }
}

void mpi_sub(pmpi_t dst, pmpi_t a, pmpi_t b)
{
    unsigned carry=0;
    unsigned __int64 num;
	unsigned i;

	mpi_mov(dst, *a);
	if(mpi_cmp(dst,b) <= 0) { mpi_mov_int64(dst, 0); return; }
    for(i=0;i<a->m_nLength;i++)
    {
        if((a->m_ulValue[i]>b->m_ulValue[i])||((a->m_ulValue[i]==b->m_ulValue[i])&&(carry==0)))
        {
            dst->m_ulValue[i]=a->m_ulValue[i]-carry-b->m_ulValue[i];
            carry=0;
        }
        else
        {
            num=0x100000000LL+a->m_ulValue[i];
            dst->m_ulValue[i]=(unsigned long)(num-carry-b->m_ulValue[i]);
            carry=1;
        }
    }
    while(dst->m_ulValue[dst->m_nLength-1]==0) dst->m_nLength--;
}

void mpi_sub_int32(pmpi_t dst, pmpi_t a, unsigned long b)
{
    int i=1;
    unsigned __int64 num;

	mpi_mov(dst, *a);
    if(dst->m_ulValue[0]>=b) {dst->m_ulValue[0]-=b; return;}
    if(dst->m_nLength==1) { mpi_mov_int64(dst, 0); return;}
	num = 0x100000000LL + dst->m_ulValue[0];
    dst->m_ulValue[0]=(unsigned long)(num-b);
    while(dst->m_ulValue[i]==0){dst->m_ulValue[i]=0xffffffff;i++;}
    dst->m_ulValue[i]--;
    if(dst->m_ulValue[i]==0)dst->m_nLength--;
}

void mpi_mul(pmpi_t dst, pmpi_t a, pmpi_t b)
{
	unsigned __int64 sum,mul=0,carry=0;
	unsigned i,j;

	if(b->m_nLength == 1) return mpi_mul_int32(dst, a, b->m_ulValue[0]);
	dst->m_nLength=a->m_nLength+b->m_nLength-1;
    for(i=0;i<dst->m_nLength;i++)
	{
		sum=carry;
		carry=0;
		for(j=0;j<b->m_nLength;j++)
		{
            if(((i-j)>=0)&&((i-j)<a->m_nLength))
			{
				mul=a->m_ulValue[i-j];
				mul*=b->m_ulValue[j];
			    carry+=mul>>32;
				mul=mul&0xffffffff;
				sum+=mul;
			}
        }
		carry+=sum>>32;
		dst->m_ulValue[i]=(unsigned long)sum;
	}
	if(carry){dst->m_nLength++;dst->m_ulValue[dst->m_nLength-1]=(unsigned long)carry;}
}

void mpi_mul_int32(pmpi_t dst, pmpi_t a, unsigned long b)
{
    unsigned __int64 mul;
    unsigned long carry=0;
	unsigned i;

	mpi_mov(dst, *a);
    for(i=0;i<a->m_nLength;i++)
    {
        mul=a->m_ulValue[i];
        mul=mul*b+carry;
        dst->m_ulValue[i]=(unsigned long)mul;
        carry=(unsigned long)(mul>>32);
    }
    if(carry){dst->m_nLength++;dst->m_ulValue[dst->m_nLength-1]=carry;}
}

void mpi_div(pmpi_t dst, pmpi_t a, pmpi_t b)
{
	mpi_t x, y, z, t;
    unsigned i,len;
    unsigned __int64 num,div;

	mpi_mov_int64(&x, 0);
	mpi_mov_int64(&y, 0);
	mpi_mov_int64(&z, 0);
	mpi_mov_int64(&t, 0);

	if(b->m_nLength == 1) return mpi_div_int32(dst, a, b->m_ulValue[0]);
	mpi_mov(&y, *a);
	while(mpi_cmp(&y, b) >= 0)
    {       
		div=y.m_ulValue[y.m_nLength-1];
		num=b->m_ulValue[b->m_nLength-1];
		len=y.m_nLength-b->m_nLength;
		if((div==num)&&(len==0)){ 	// +++
#if 0
			mpi_add_int32(dst, dst, 1); 
#else
			mpi_add_int32(&x, dst, 1);
			mpi_mov(dst, x);
#endif
			break; 
		}
		if((div<=num)&&len){len--;div=(div<<32)+y.m_ulValue[y.m_nLength-2];}
		div=div/(num+1);
		mpi_mov_int64(&z, div);
		if(len)
		{
			z.m_nLength+=len;
			for(i=z.m_nLength-1;i>=len;i--)z.m_ulValue[i]=z.m_ulValue[i-len];
			for(i=0;i<len;i++)z.m_ulValue[i]=0;
		}
		mpi_add(dst, dst, &z);
		mpi_mul(&t, b, &z);		// t = b*z
#if 0
		mpi_sub(&y, &y, &t);		// y = y - b * z
#else
		mpi_sub(&x, &y, &t);		// x = x - b * z
		mpi_mov(&y, x);
#endif
    }
}

void mpi_div_int32(pmpi_t dst, pmpi_t a, unsigned long b)
{
    unsigned __int64 div,mul;
    unsigned long carry=0;
	int i;
	unsigned __int64 temp;

	mpi_mov(dst, *a);
    if(dst->m_nLength==1){dst->m_ulValue[0]=dst->m_ulValue[0]/b;return;}
    for(i=dst->m_nLength-1;i>=0;i--)
    {
        div=carry;
        div=(div<<32)+dst->m_ulValue[i];
        dst->m_ulValue[i]=(unsigned long)(div/b);
        mul=(div/b)*b;
        carry=(unsigned long)(div-mul);
    }
    if(dst->m_ulValue[dst->m_nLength-1]==0)dst->m_nLength--;
}

void mpi_mod(pmpi_t dst, pmpi_t a, pmpi_t b)
{
	unsigned __int64 div,num;
	unsigned i,len;
	mpi_t x, y, t;

	mpi_mov_int64(&x, 0);
	mpi_mov_int64(&y, 0);
	mpi_mov_int64(&t, 0);
	
	mpi_mov(dst, *a);
	while(mpi_cmp(dst, b) >= 0)
    {
		div=dst->m_ulValue[dst->m_nLength-1];
		num=b->m_ulValue[b->m_nLength-1];
		len=dst->m_nLength-b->m_nLength;
		if((div==num)&&(len==0)){ 
#if 0
			mpi_sub(dst, dst, b); 
#else
			mpi_sub(&x, dst, b);
			mpi_mov(dst, x);
#endif
			break;
		}
		if((div<=num)&&len){len--;div=(div<<32)+dst->m_ulValue[dst->m_nLength-2];}
		div=div/(num+1);
		mpi_mov_int64(&t, div);
		mpi_mul(&y, &t, b);
		if(len)
		{
			y.m_nLength+=len;
			for(i=y.m_nLength-1;i>=len;i--) y.m_ulValue[i]=y.m_ulValue[i-len];
			for(i=0;i<len;i++) y.m_ulValue[i]=0;
		}
#if 0
		mpi_sub(dst, dst, &y);
#else
		mpi_sub(&x, dst, &y);
		mpi_mov(dst, x);
#endif
    }
}

void mpi_mod_int32(pmpi_t dst, pmpi_t a, unsigned long b)
{
    unsigned __int64 div;
    unsigned long carry=0;
	int i;

    if(a->m_nLength==1){
		mpi_mov_int64(dst, a->m_ulValue[0]%b);
		return;
	}
    for(i=a->m_nLength-1;i>=0;i--)
    {
        div=a->m_ulValue[i];
		div+=carry*0x100000000LL;
        carry=(unsigned long)(div%b);
    }
	mpi_mov_int64(dst, carry);
}

void mpi_get(pmpi_t dst, char *buffer, int len, unsigned int system)
{
    int k, i;
	mpi_t x;

	mpi_mov_int64(&x, 0);
	mpi_mov_int64(dst, 0);
    for(i=0;i<len;i++)
    {
	   mpi_mul_int32(&x, dst, system);
	   mpi_mov(dst, x);
       if((buffer[i]>='0')&&(buffer[i]<='9'))k=buffer[i]-48;
       else if((buffer[i]>='A')&&(buffer[i]<='F'))k=buffer[i]-55;
       else if((buffer[i]>='a')&&(buffer[i]<='f'))k=buffer[i]-87;
       else k=0;
	   mpi_add_int32(&x, dst, k);
	   mpi_mov(dst, x);
    }
}

void mpi_get_bin(pmpi_t dst, char *buffer, int len)
{
    int k, i;
	mpi_t x;

	mpi_mov_int64(&x, 0);
	mpi_mov_int64(dst, 0);
    for(i=0;i<len;i++)
    {
	   mpi_mul_int32(&x, dst, 0x100);
	   mpi_mov(dst, x);
	   k = (unsigned char)buffer[len - 1 - i];		// ++++ inverse
	   mpi_add_int32(&x, dst, k);
	   mpi_mov(dst, x);
    }
}

int mpi_put(pmpi_t dst, char *buffer, int len, unsigned int system)
{
    char *str="0123456789ABCDEF";
    int i=0;
    char ch;
    mpi_t x, t;

    if((dst->m_nLength==1)&&(dst->m_ulValue[0]==0)){
		buffer[0] = '0';
		buffer[1] = 0x0;
		return 1;
	}
	mpi_mov_int64(&t, 0);
	mpi_mov(&x, *dst);
    while(x.m_ulValue[x.m_nLength-1]>0)
    {
		mpi_mod_int32(&t, &x, system);
        ch=str[t.m_ulValue[0]];
		buffer[i++] = ch;
		mpi_div_int32(&t, &x, system);
		mpi_mov(&x, t);
    }
	buffer[i] = 0;			// inverse buffer
	return i;
}

int mpi_put_bin(pmpi_t dst, char *buffer, int len)
{
	int i, j;
    mpi_t x, t;

	for(i=0; i<len; i++) buffer[i] = 0;

    if((dst->m_nLength==1)&&(dst->m_ulValue[0]==0)){
		return 1;
	}

	i = 0;
	mpi_mov_int64(&t, 0);
	mpi_mov(&x, *dst);
    while(x.m_ulValue[x.m_nLength-1]>0)
    {
		mpi_mod_int32(&t, &x, 0x100);
		buffer[i++] = t.m_ulValue[0];
		mpi_div_int32(&t, &x, 0x100);
		mpi_mov(&x, t);
    }

	{
		char swap;
		for(j=0; j< i / 2; j++){
			swap = buffer[j];
			buffer[j] = buffer[i-1-j];
			buffer[i-1-j] = swap;
		}
	}
	return i;
}

/**
  * dst = n ^a mod b
  */
void mpi_rsatrans(pmpi_t dst, pmpi_t n, pmpi_t a, pmpi_t b)
{
	int i,j,k;
	unsigned c;
	unsigned long num;
	mpi_t y, t, z;

	mpi_mov_int64(&y, 0);
	mpi_mov_int64(&t, 0);
	mpi_mov_int64(&z, 0);
	k=a->m_nLength*32-32;
	num=a->m_ulValue[a->m_nLength-1];
	while(num){num=num>>1;k++;}
	mpi_mov(dst, *n);
	for(i=k-2;i>=0;i--)
	{
		mpi_mul_int32(&t, dst, dst->m_ulValue[dst->m_nLength - 1]);
		mpi_mod(&y, &t, b);
        for(c=1;c<dst->m_nLength;c++)
		{          
			for(j=y.m_nLength;j>0;j--)y.m_ulValue[j]=y.m_ulValue[j-1];
			y.m_ulValue[0]=0;
			y.m_nLength++;
			mpi_mul_int32(&t, dst, dst->m_ulValue[dst->m_nLength-c-1]);
			mpi_add(&z, &t, &y);
			mpi_mod(&y, &z, b);
		}
		mpi_mov(dst, y);
		if((a->m_ulValue[i>>5]>>(i&31))&1)
		{
			mpi_mul_int32(&t, n, dst->m_ulValue[dst->m_nLength-1]);
			mpi_mod(&y, &t, b);
            for(c=1;c<dst->m_nLength;c++)
			{          
			    for(j=y.m_nLength;j>0;j--)y.m_ulValue[j]=y.m_ulValue[j-1];
			    y.m_ulValue[0]=0;
			    y.m_nLength++;
				mpi_mul_int32(&t, n, dst->m_ulValue[dst->m_nLength-c-1]);
				mpi_add(&z, &t, &y);
				mpi_mod(&y, &z, b);
			}
 			mpi_mov(dst, y);
		}
	}
}


