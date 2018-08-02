#ifndef _MD5_H_
#define _MD5_H_

#ifdef __cplusplus
extern "C"
{
#endif


typedef struct _md5context
{
	uint32_t buf[4];
	uint32_t bits[2];
	unsigned char in[64];
}md5context_t;

/** 
 * @brief Start MD5 accumulation.  Set bit count to 0 and buffer to mysterious
 * initialization constants.
 * @param context 
 */
void md5_init(md5context_t *context);

/** 
 * @brief Update context to reflect the concatenation of another buffer full
 * of bytes.
 * @param context 
 * @param buf 
 * @param len 
 */
void md5_update(md5context_t *context, unsigned char const *buf, size_t len);

/** 
 * @brief Final wrapup - pad to 64-byte boundary with the bit pattern 
 * 1 0* (64-bit count of bits processed, MSB-first)
 * @param digest 
 * @param context 
 */
void md5_final(unsigned char digest[16], md5context_t *context);


void md5_digest(unsigned char const *buf, size_t len, unsigned char digest[16]);
void md5_string(unsigned char const *buf, size_t len, char result_str[33]);


#ifdef __cplusplus
}
#endif

#endif // _MD5_H_
