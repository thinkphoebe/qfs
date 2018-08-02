// 详细请参考《RFC2617》协议标准

#ifndef __HTTPAUTH_H__
#define __HTTPAUTH_H__

#ifdef __cplusplus
extern "C" {
#endif


#define HASHLEN 16
#define HASHHEXLEN 32
typedef char hash_t[HASHLEN];
typedef char hashhex_t[HASHHEXLEN+1];

/* calculate H(A1) as per spec */
void digest_calc_ha1(
		const char*alg,	        /* 算法名称 md5||md5-sess */
		const char*user,	    /* login user name */
		const char*realm,	    /* realm name */
		const char*pswd,	    /* login password */
		const char*nonce,	    /* 服务器随机产生的nonce返回串 */
		const char*cnonce,      /* 客户端随机产生的nonce串 */
		hashhex_t session_key   /* OUT H(A1) */
		);

/* calculate request-digest/response-digest as per HTTP Digest spec */
void digest_calc_response(
			hashhex_t ha1,          /* H(A1) */
			char *nonce,            /* nonce from server */
			char *nonce_count,      /* 8 hex digits */
			char *cnonce,           /* client nonce */
			char *qop,              /* qop-value: "", "auth", "auth-int" */
			char *method,           /* method from the request */
			char *digest_uri,       /* requested URL */
			hashhex_t h_entity,     /* H(entity body) if qop="auth-int" */
			hashhex_t response      /* OUT request-digest or response-digest */
			);


#ifdef __cplusplus
}
#endif

#endif // __HTTPAUTH_H__

