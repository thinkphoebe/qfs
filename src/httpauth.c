#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "md5.h"
#include "httpauth.h"

#include "utils.h"


static void cvthex(hash_t Bin, hashhex_t Hex/*out*/)
{
    unsigned short i;
    unsigned char j;
	
    for (i = 0; i < HASHLEN; i++)
    {
        j = (Bin[i] >> 4) & 0xf;
        if (j <= 9)
            Hex[i*2] = (j + '0');
		else
            Hex[i*2] = (j + 'a' - 10);
        j = Bin[i] & 0xf;
        if (j <= 9)
            Hex[i*2+1] = (j + '0');
		else
            Hex[i*2+1] = (j + 'a' - 10);
    };
    Hex[HASHHEXLEN] = '\0';
}


/* calculate H(A1) as per spec */
void digest_calc_ha1(
		const char*alg,	        /* 算法名称 md5||md5-sess */
		const char*user,	    /* login user name */
		const char*realm,	    /* realm name */
		const char*pswd,	    /* login password */
		const char*nonce,	    /* 服务器随机产生的nonce返回串 */
		const char*cnonce,      /* 客户端随机产生的nonce串 */
		hashhex_t session_key   /* OUT H(A1) */
		)
{
	md5context_t md5ctx;
	hash_t ha1;

	md5_init(&md5ctx);
	md5_update(&md5ctx, user, strlen(user));
	md5_update(&md5ctx, ":", 1);
	md5_update(&md5ctx, realm, strlen(realm));
	md5_update(&md5ctx, ":", 1);
	md5_update(&md5ctx, pswd, strlen(pswd));
	md5_final(ha1, &md5ctx);
	if (strcasecmp(alg, "md5-sess") == 0)
    {
		md5_init(&md5ctx);
		md5_update(&md5ctx, ha1, HASHLEN);
		md5_update(&md5ctx, ":", 1);
		md5_update(&md5ctx, nonce, strlen(nonce));
		md5_update(&md5ctx, ":", 1);
		md5_update(&md5ctx, cnonce, strlen(cnonce));
		md5_final(ha1, &md5ctx);
	};
	cvthex(ha1, session_key);
}


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
			)
{
	md5context_t md5ctx;
	hash_t ha2;
	hash_t resp_hash;
	hashhex_t ha2hex;
	
	// calculate H(A2)
	md5_init(&md5ctx);
	md5_update(&md5ctx, method, strlen(method));
	md5_update(&md5ctx, ":", 1);
	md5_update(&md5ctx, digest_uri, strlen(digest_uri));
	if (strcasecmp(qop, "auth-int") == 0)
    {
		md5_update(&md5ctx, ":", 1);
		md5_update(&md5ctx, h_entity, HASHHEXLEN);
	};
	md5_final(ha2, &md5ctx);
	cvthex(ha2, ha2hex);
	
	// calculate response
	md5_init(&md5ctx);
	md5_update(&md5ctx, ha1, HASHHEXLEN);
	md5_update(&md5ctx, ":", 1);
	md5_update(&md5ctx, nonce, strlen(nonce));
	md5_update(&md5ctx, ":", 1);
    if (*qop)
    {
		md5_update(&md5ctx, nonce_count, strlen(nonce_count));
		md5_update(&md5ctx, ":", 1);
		md5_update(&md5ctx, cnonce, strlen(cnonce));
		md5_update(&md5ctx, ":", 1);
		md5_update(&md5ctx, qop, strlen(qop));
		md5_update(&md5ctx, ":", 1);
	};
	md5_update(&md5ctx, ha2hex, HASHHEXLEN);
	md5_final(resp_hash, &md5ctx);
	cvthex(resp_hash, response);
};

