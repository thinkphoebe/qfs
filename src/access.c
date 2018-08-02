#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "md5.h"
#include "access.h"


#define NAME_MAX 15
#define PSWD_MAX 15

typedef struct _userinfo 
{
    char name[NAME_MAX + 1];
    char pass[PSWD_MAX + 1];
    uint32_t mask;
    struct _userinfo *p_next;

} userinfo_t;

static userinfo_t *m_userlist = NULL;


int access_init()
{
    m_userlist = NULL;
    return 0;
}


void access_exit()
{
    userinfo_t *p = m_userlist;
    while (p != NULL)
    {
        m_userlist = m_userlist->p_next;
        free(p);
        p = m_userlist;
    }
    m_userlist = NULL;
}


int access_add_user(const char *name, const char *pass, uint32_t mask, int over_write)
{
    userinfo_t *p, *q;
    int ret = 0;

    p = m_userlist;
    q = NULL;
    while (p != NULL)
    {
        if (strncmp(p->name, name, NAME_MAX) == 0)
        {
            if (over_write == 0)
                return -1;
            q = p;
            ret = -2;
            break;
        }
        p = p->p_next;
    }
    if (q == NULL)
        q = (userinfo_t *)malloc(sizeof(userinfo_t));
    if (q == NULL)
        return -1;

    memset(q, 0, sizeof(userinfo_t));
    strncpy(q->name, name, NAME_MAX);
    strncpy(q->pass, pass, PSWD_MAX);

    if (ret != -2)
    {
        q->mask = mask;
        q->p_next = m_userlist;
        m_userlist = q;
    }

    return 0;
}


int access_del_user(const char *name)
{
    userinfo_t *p, *q;

    p = m_userlist;
    q = NULL;
    while (p != NULL)
    {
        if (strncmp(p->name, name, NAME_MAX) == 0)
        {
            if (q == NULL)
                m_userlist = m_userlist->p_next;
            else
                q->p_next = p->p_next;
            free(p);
            break;
        }
        q = p;
        p = p->p_next;
    }
    
    return -1;
}


//name存在且passwd正确时返回0, 并设置auth_mask. 否则返回-1
//ATTENTION 注意用户过多时的效率
int access_get_auth_state(const char *name, const char *pass, uint32_t *auth_mask/*out*/)
{
    userinfo_t *p;

    p = m_userlist;
    while (p != NULL)
    {
        if (strncmp(p->name, name, NAME_MAX) == 0)
        {
            if (strncmp(p->pass, pass, PSWD_MAX) == 0)
            {
                *auth_mask = p->mask;
                return 0;
            }
            else
                break;
        }
        p = p->p_next;
    }
    
    *auth_mask = E_AUTH_NULL;
    return -1;
}


const char* access_get_pswd(const char *name)
{
    userinfo_t *p, *q;

    p = m_userlist;
    q = NULL;
    while (p != NULL)
    {
        if (strncmp(p->name, name, NAME_MAX) == 0)
            return p->pass;
        q = p;
        p = p->p_next;
    }
    
    return NULL;
}

