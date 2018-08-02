#include "snsdk.h"
#include "snlog.h"
#include "snsetting.h"

#define MAX_SESSIONS 128


typedef union
{
    int vbool;
    int vint;
    double vdouble;
    char *vstring;
    time_t vtime;
    //char *vbinary;
} value_t;

typedef struct _node
{
    char *name;
    setting_data_type_t type;
    value_t value;
    struct _node *p_next;
} node_t;

struct _sn_setting 
{
    node_t *node_heads[MAX_SESSIONS];
    char *session_names[MAX_SESSIONS];
};


static node_t* find_node(sn_setting_t *h, const char *session, const char *name, int del)
{
    node_t *p, *q;
    int i;

    p = NULL;
    q = NULL;

    if (session == NULL)
    {
        i = 0;
        q = h->node_heads[0];
    }
    else
    {
        for (i = 1; i < MAX_SESSIONS; i++)
        {
            if (h->session_names[i] != NULL && strcmp(h->session_names[i], session) == 0)
            {
                q = h->node_heads[i];
                break;
            }
        }
    }

    if (q == NULL)
        return NULL;

    while (q != NULL)
    {
        if (strcmp(q->name, name) == 0)
        {
            if (del > 0)
            {
                if (p == NULL)
                {
                    h->node_heads[i] = q->p_next;
                    if (h->node_heads[i] == NULL)
                    {
                        free(h->session_names[i]);
                        h->session_names[i] = NULL;
                    }
                }
                else
                    p->p_next = q->p_next;
            }
            return q;
        }
        p = q;
        q = q->p_next;
    }
    return NULL;
}


static void free_node(node_t *node)
{
    if (node->type == SDT_STRING || node->type == SDT_BINARY)
        free(node->value.vstring);
    free(node->name);
    free(node);
}


static void free_all_nodes(sn_setting_t *h)
{
    node_t *p, *q;
    int i;
    for (i = 0; i < MAX_SESSIONS; i++)
    {
        p = h->node_heads[i];
        while (p != NULL)
        {
            q = p->p_next;
            free_node(p);
            p = q;
        }
        free(h->session_names[i]);
        h->session_names[i] = NULL;
        h->node_heads[i] = NULL;
    }
}


sn_setting_t* sn_setting_create()
{
    sn_setting_t *h;
    
    h = (sn_setting_t *)malloc(sizeof(sn_setting_t));
    if (h == NULL)
        return NULL;
    memset(h, 0, sizeof(sn_setting_t));

    return h;
}


void sn_setting_destory(sn_setting_t *h)
{
    if (h == NULL)
        return;
    free_all_nodes(h);
    free(h);
}


static char* read_file(const char *filename, int *size/*out*/)
{
    FILE *fp = NULL;
    int fsize;
    char *buf = NULL;

    fp = fopen(filename, "rb");
    if (fp == NULL)
        return NULL;

    fseek(fp, 0, SEEK_END);
    fsize = ftell(fp);
    if (fsize <= 0 || fsize > 1024 * 1024)
        goto FAIL;
    fseek(fp, 0, SEEK_SET);

    buf = (char *)malloc(fsize + 1);
    if (buf == NULL)
        goto FAIL;
    buf[fsize] = '\0';

    if (fread(buf, 1, fsize, fp) != fsize)
        goto FAIL;

    *size = fsize;
    fclose(fp);
    return buf;

FAIL:
    if (fp != NULL)
        fclose(fp);
    free(buf);
    return NULL;
}


//跳过空行, 注释行, 行头空格, 行尾空格, 行尾注释
//注释以#号开始, ''和""内的#号认为是内容, 不跳过
static int get_line(char *buf, int buf_len, char **pos/*out*/, int *len/*out*/)
{
    int found = 0;
    char *p_buf_end;
    char *p_line_start;
    char *p;
    int in1 = 0, in2 = 0;

    p_buf_end = buf + buf_len;
    p = buf;

    //find line start
    while (p < p_buf_end)
    {
        if (*p == '#')
            p = strpbrk(p, "\r\n");
        else if (*p == '\r' || *p == '\n' || isspace(*p))
            p++;
        else if (*p == '\0')
            break;
        else
        {
            found = 1;
            break;
        }
    }

    if (found == 0)
    {
        sn_log_error("get_line, find start FAILED!\n");
        sn_log_error("%s\n", p);
        return -1;
    }

    p_line_start = p;

    while (p < p_buf_end)
    {
        if (*p == '\'' && in2 == 0)
            in1 = (in1 + 1) % 2;
        else if (*p == '\"' && in1 == 0)
            in2 = (in2 + 1) % 2;
        else if (*p == '\r' || *p == '\n' || *p == '\0' || *p == '#')
        {
            if (in1 == 1 || in2 == 1)
            {
                sn_log_info("get_line, parse error:%d,%d\n", in1, in2);
                return -1;
            }
            else
            {
                *pos = p_line_start;
                *len = p - p_line_start + 1;
                *p = '\0';
                sn_log_info("get_line, find line:%p,%d\n", *pos, *len);
                return 0;
            }
        }

        p++;
    }

    sn_log_error("get_line, find end FAILED!\n");
    return -1;
}


static int parse_line(char *buf, char **name, setting_data_type_t *type, value_t *value)
{
    char localname[128];
    char *p, * q;

    p = strpbrk(buf, " =");
    if (p == NULL || p - buf >= 128)
    {
        sn_log_info("parse_line, invalid name!\n");
        return -1;
    }
    memcpy(localname, buf, p - buf);
    localname[p - buf] = '\0';

    sn_log_info("parse_line, name:%s\n", localname);

    while (*p == ' ' || *p == '=')
        p++;

    if (*p == '\'' || *p == '\"')
    {
        char *q;
        char s = *p;

        *type = (s == '\'') ? SDT_BINARY : SDT_STRING;

        p++;
        q = p;
        for (; ;)
        {
            q = strchr(q, s);
            if (q == NULL)
            {
                sn_log_info("parse_line, can not find string end!\n");
                return -1;
            }
            if (*(q - 1) != '\\')
                break;
            q++;
        }
        value->vstring = (char *)malloc(q - p + 1);
        if (value->vstring == NULL)
        {
            sn_log_error("parse_line, malloc FAILED, size:%d\n", (int)(q - p + 1));
            return -1;
        }
        memcpy(value->vstring, p, q - p);
        value->vstring[q - p] = '\0';

        sn_log_info("parse_line, string:%c,%s\n", s, value->vstring);
    }
    else if (strncasecmp(p, "true", strlen("true")) == 0
            || strncasecmp(p, "false", strlen("false")) == 0)
    {
        *type = SDT_BOOL;
        value->vbool = (strncasecmp(p, "true", strlen("true")) == 0) ? 1 : 0;
        sn_log_info("parse_line, bool:%d\n", value->vbool);
    }
    else if ((q = strchr(p, '.')) != NULL)
    {
        char *r = strchr(p, '\n');
        if (r != NULL && q > r)
        {
            *type = SDT_INTEGER;
            value->vint = atoi(p);
            sn_log_info("parse_line, integer:%d\n", value->vint);
        }
        else
        {
            *type = SDT_DOUBLE;
            value->vdouble = atof(p);
            sn_log_info("parse_line, dobule:%f\n", value->vdouble);
        }
    }
    else if (strchr(p, '-') != NULL)
    {
        int year, month, day, hour, min, sec;
        struct tm tm;
        *type = SDT_TIME;
        if (sscanf(p, "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &min, &sec) < 6)
        {
            sn_log_info("parse_line, parse TIME error!\n");
            return -1;
        }
        tm.tm_year = year - 1900;
        tm.tm_mon = month;
        tm.tm_mday = day;
        tm.tm_hour = hour;
        tm.tm_min = min;
        tm.tm_sec = sec;
        value->vtime = mktime(&tm);
        sn_log_info("parse_line, year:%d,mon:%d,day:%d,hour:%d,min:%d,sec:%d\n",
                year, month, day, hour, min, sec);
    }
    else if (*p >= '0' || *p <= '9')
    {
        *type = SDT_INTEGER;
        value->vint = atoi(p);
        sn_log_info("parse_line, integer:%d\n", value->vint);
    }
    else
    {
        sn_log_info("parse_line, can not find value!\n");
        return -1;
    }

    *name = strdup(localname);

    return 0;
}


static int find_session(sn_setting_t *h, const char *session_name)
{
    int i;
    //default session
    if (session_name == NULL)
        return 0;
    for (i = 0; i < MAX_SESSIONS; i++)
    {
        //session exists
        if (h->session_names[i] != NULL && strcmp(h->session_names[i], session_name) == 0)
            return i;
    }
    for (i = 1; i < MAX_SESSIONS; i++)
    {
        if (h->session_names[i] == NULL)
        {
            h->session_names[i] = strdup(session_name);
            return i;
        }
    }
    //full
    return -1;
}


int sn_setting_load(sn_setting_t *h, const char *filename)
{
    char *buf;
    char *p;
    int size;
    int rsize;
    char *lpos;
    int llen;
    int sindex = 0;
    char session_name[256];

    node_t *node;
    char *name;
    setting_data_type_t type;
    value_t value;

    free_all_nodes(h);

    buf = read_file(filename, &size);
    if (buf == NULL)
        return -1;

    p = buf;
    rsize = size;
    while (rsize > 0 && get_line(p, rsize, &lpos, &llen) == 0)
    {
        if (*lpos == '[')
        {
            memset(session_name, 0, 256);
            char *q = strchr(lpos, ']');
            if (q == NULL)
                goto NEXT;
            if (q - lpos >= 256)
                goto NEXT;
            memcpy(session_name, lpos + 1, q - lpos - 1);
            sindex = find_session(h, session_name);
            if (sindex < 0)
            {
                sn_log_error("read session name FAILED! %s\n", lpos);
                return -1;
            }
        }
        else
        {
            if (parse_line(lpos, &name, &type, &value) != 0)
            {
                free_all_nodes(h);
                free(buf);
                return -1;
            }
            node = (node_t *)malloc(sizeof(node_t));
            node->name = name;
            node->type = type;
            node->value = value;
            node->p_next = h->node_heads[sindex];
            h->node_heads[sindex] = node;
        }

NEXT:
        rsize = (size - (p - buf) - llen);
        p = lpos + llen;
    }

    return 0;
}


static void write_list(node_t *list_head, FILE *fp)
{
    node_t *pn = list_head;
    while (pn != NULL)
    {
        if (pn->type == SDT_BOOL)
            fprintf(fp, "%s = %s\r\n", pn->name, (pn->value.vbool > 0) ? "true" : "false");
        else if (pn->type == SDT_INTEGER)
            fprintf(fp, "%s = %d\r\n", pn->name, pn->value.vint);
        else if (pn->type == SDT_DOUBLE)
            fprintf(fp, "%s = %.4f\r\n", pn->name, pn->value.vdouble);
        else if (pn->type == SDT_STRING)
            fprintf(fp, "%s = \"%s\"\r\n", pn->name, pn->value.vstring);
        else if (pn->type == SDT_BINARY)
            fprintf(fp, "%s = \'%s\'\r\n", pn->name, pn->value.vstring);
        else if (pn->type == SDT_TIME)
        {
            struct tm t;
            localtime_r(&pn->value.vtime, &t);
            fprintf(fp, "%s = %d-%d-%dT%d:%d:%d\r\n", pn->name, t.tm_year + 1900,
                    t.tm_mon, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
        }
        pn = pn->p_next;
    }
}


int sn_setting_save(sn_setting_t *h, const char *filename)
{
    int write_flags[MAX_SESSIONS] = { 0 };
    FILE *fp;
    node_t *p, *q;
    int i, j;

    //保留文件中的已有参数
    //如果文件存在, 将文件的参数读入, 剔除和当前settings重复的, 然后写入文件
    sn_setting_t *setting_exist = sn_setting_create();
    sn_setting_load(setting_exist, filename);
    for (i = 0; i < MAX_SESSIONS; i++)
    {
        p = setting_exist->node_heads[i];
        while (p != NULL)
        {
            q = NULL;
            if (find_node(h, setting_exist->session_names[i], p->name, 0) != NULL)
                q = p;
            p = p->p_next;
            if (q != NULL)
            {
                q = find_node(setting_exist, setting_exist->session_names[i], q->name, 1);
                free_node(q);
            }
        }
    }

    fp = fopen(filename, "wb");
    if (fp == NULL)
        return -1;
    write_list(setting_exist->node_heads[0], fp);
    write_list(h->node_heads[0], fp);
    for (i = 1; i < MAX_SESSIONS; i++)
    {
        if (setting_exist->node_heads[i] == NULL)
            continue;
        if (setting_exist->session_names[i] == NULL)
        {
            sn_log_info("error, session_name is NULL, but have nodes!\n");
            continue;
        }
        fprintf(fp, "\r\n[%s]\r\n", setting_exist->session_names[i]);
        write_list(setting_exist->node_heads[i], fp);
        for (j = 1; j < MAX_SESSIONS; j++)
        {
            if (h->session_names[j] != NULL
                    && strcmp(setting_exist->session_names[i], h->session_names[j]) == 0)
            {
                write_list(h->node_heads[j], fp);
                write_flags[j] = 1;
            }
        }
    }
    for (i = 1; i < MAX_SESSIONS; i++)
    {
        if (write_flags[i] == 0 && h->node_heads[i] != NULL)
        {
            if (h->session_names[i] == NULL)
            {
                sn_log_info("error, session_name is NULL, but have nodes!\n");
                continue;
            }
            fprintf(fp, "\r\n[%s]\r\n", h->session_names[i]);
            write_list(h->node_heads[i], fp);
        }
    }

    fclose(fp);
    sn_setting_destory(setting_exist);
    return 0;
}


int sn_setting_delete(sn_setting_t *h, const char *session, const char *name)
{
    node_t *p;

    p = find_node(h, session, name, 1);
    if (p == NULL)
        return -1;
    free_node(p);

    return 0;
}


setting_data_type_t sn_setting_query(sn_setting_t *h, const char *session, const char *name)
{
    node_t *p;
    p = find_node(h, session, name, 0);
    if (p == NULL)
        return SDT_NULL;
    return p->type;
}


//以下函数读取失败时不修改输出参数的内容, 因此可将默认值填入输出参数

int sn_setting_read_bool(sn_setting_t *h, const char *session, const char *name, int *value/*out*/)
{
    node_t *p;
    p = find_node(h, session, name, 0);
    if (p == NULL)
        return -1;
    if (p->type != SDT_BOOL)
        return -1;
    *value = p->value.vbool;
    return 0;
}


int sn_setting_write_bool(sn_setting_t *h, const char *session, const char *name, int value)
{
    node_t *p;
    int sindex;

    p = find_node(h, session, name, 0);
    if (p == NULL)
    {
        sindex = find_session(h, session);
        if (sindex < 0)
            return -1;
        p = (node_t *)malloc(sizeof(node_t));
        if (p == NULL)
            return -1;
        memset(p, 0, sizeof(node_t));
        p->name = strdup(name);
        p->type = SDT_BOOL;
        p->p_next = h->node_heads[sindex];
        h->node_heads[sindex] = p;
    }
    else if (p->type != SDT_BOOL)
        return -1;
    p->value.vbool = value;
    return 0;
}


int sn_setting_read_integer(sn_setting_t *h, const char *session, const char *name, int *value/*out*/)
{
    node_t *p;
    p = find_node(h, session, name, 0);
    if (p == NULL)
        return -1;
    if (p->type != SDT_INTEGER)
        return -1;
    *value = p->value.vint;
    return 0;
}


int sn_setting_write_integer(sn_setting_t *h, const char *session, const char *name, int value)
{
    node_t *p;
    int sindex;

    p = find_node(h, session, name, 0);
    if (p == NULL)
    {
        sindex = find_session(h, session);
        if (sindex < 0)
            return -1;
        p = (node_t *)malloc(sizeof(node_t));
        if (p == NULL)
            return -1;
        memset(p, 0, sizeof(node_t));
        p->name = strdup(name);
        p->type = SDT_INTEGER;
        p->p_next = h->node_heads[sindex];
        h->node_heads[sindex] = p;
    }
    else if (p->type != SDT_INTEGER)
        return -1;
    p->value.vint = value;
    return 0;
}


int sn_setting_read_double(sn_setting_t *h, const char *session, const char *name, double *value/*out*/)
{
    node_t *p;
    p = find_node(h, session, name, 0);
    if (p == NULL)
        return -1;
    if (p->type != SDT_DOUBLE)
        return -1;
    *value = p->value.vdouble;
    return 0;
}


int sn_setting_write_double(sn_setting_t *h, const char *session, const char *name, double value)
{
    node_t *p;
    int sindex;

    p = find_node(h, session, name, 0);
    if (p == NULL)
    {
        sindex = find_session(h, session);
        if (sindex < 0)
            return -1;
        p = (node_t *)malloc(sizeof(node_t));
        if (p == NULL)
            return -1;
        memset(p, 0, sizeof(node_t));
        p->name = strdup(name);
        p->type = SDT_DOUBLE;
        p->p_next = h->node_heads[sindex];
        h->node_heads[sindex] = p;
    }
    else if (p->type != SDT_DOUBLE)
        return -1;
    p->value.vdouble = value;
    return 0;
}


const char* sn_setting_read_string(sn_setting_t *h, const char *session, const char *name)
{
    node_t *p;
    p = find_node(h, session, name, 0);
    if (p == NULL)
        return NULL;
    if (p->type != SDT_STRING)
        return NULL;
    return p->value.vstring;
}


int sn_setting_write_string(sn_setting_t *h, const char *session, const char *name, const char *value)
{
    node_t *p;
    int sindex;

    p = find_node(h, session, name, 0);
    if (p == NULL)
    {
        sindex = find_session(h, session);
        if (sindex < 0)
            return -1;
        p = (node_t *)malloc(sizeof(node_t));
        if (p == NULL)
            return -1;
        memset(p, 0, sizeof(node_t));
        p->name = strdup(name);
        p->type = SDT_STRING;
        p->p_next = h->node_heads[sindex];
        h->node_heads[sindex] = p;
    }
    else if (p->type != SDT_STRING)
        return -1;
    else
        free(p->value.vstring);
    p->value.vstring = strdup(value);

    //remove '\r', '\n'
    int len;
    int i;
    len = strlen(name);
    for (i = 0; i < len; i++)
    {
        if (p->name[i] == '\r' || p->name[i] == '\n')
        {
            p->name[i] = '\0';
            break;
        }
    }
    len = strlen(value);
    for (i = 0; i < len; i++)
    {
        if (p->value.vstring[i] == '\r' || p->value.vstring[i] == '\n')
        {
            p->value.vstring[i] = '\0';
            break;
        }
    }

    return 0;
}


int sn_setting_read_time(sn_setting_t *h, const char *session, const char *name, time_t *value/*out*/)
{
    node_t *p;
    p = find_node(h, session, name, 0);
    if (p == NULL)
        return -1;
    if (p->type != SDT_TIME)
        return -1;
    *value = p->value.vtime;
    return 0;
}


int sn_setting_write_time(sn_setting_t *h, const char *session, const char *name, time_t value)
{
    node_t *p;
    int sindex;

    p = find_node(h, session, name, 0);
    if (p == NULL)
    {
        sindex = find_session(h, session);
        if (sindex < 0)
            return -1;
        p = (node_t *)malloc(sizeof(node_t));
        if (p == NULL)
            return -1;
        memset(p, 0, sizeof(node_t));
        p->name = strdup(name);
        p->type = SDT_TIME;
        p->p_next = h->node_heads[sindex];
        h->node_heads[sindex] = p;
    }
    else if (p->type != SDT_TIME)
        return -1;
    p->value.vtime = value;
    return 0;
}


//binary保存时使用base64编码
//value的内存snsetting模块分配, 外部负责释放. 返回值为实际读取的字节数. 读取错误返回-1.
int sn_setting_read_binary(sn_setting_t *h, const char *session, const char *name, char **value/*out*/)
{
    node_t *p;
    p = find_node(h, session, name, 0);
    if (p == NULL)
        return -1;
    if (p->type != SDT_BINARY)
        return -1;
    //TODO *value = p->value.vbool;
    return 0;
}


//size为要求写入的字节数, 返回值为实际写入的字节数
int sn_setting_write_binary(sn_setting_t *h, const char *session, const char *name, const char *value, int size)
{
    node_t *p;
    int sindex;

    p = find_node(h, session, name, 0);
    if (p == NULL)
    {
        sindex = find_session(h, session);
        if (sindex < 0)
            return -1;
        p = (node_t *)malloc(sizeof(node_t));
        if (p == NULL)
            return -1;
        memset(p, 0, sizeof(node_t));
        p->name = strdup(name);
        p->type = SDT_BINARY;
        p->p_next = h->node_heads[sindex];
        h->node_heads[sindex] = p;
    }
    else if (p->type != SDT_BINARY)
        return -1;
    else
        free(p->value.vstring);

    //TODO p->value.vbool = value;

    return 0;
}


//迭代, 初次调用时将index指向NULL, 调用后以下函数填入位置信息, 下次调用时将上次填入的index传回
const char* sn_setting_get_session_name(sn_setting_t *h, void **index)
{
    POINTERINT pos = (POINTERINT)(*index);
    if (pos < 0 || pos >= MAX_SESSIONS)
        return NULL;
    for (; pos < MAX_SESSIONS; pos++)
    {
        if (h->session_names[pos] != NULL)
            break;
    }
    *index = (POINTERINT *)(pos + 1);
    if (pos == MAX_SESSIONS)
        return NULL;
    return h->session_names[pos];
}


const char* sn_setting_get_value_name(sn_setting_t *h, const char *session, void **index)
{
    node_t *p;
    int i;
    p = NULL;
    if (*index == NULL)
    {
        if (session == NULL)
        {
            i = 0;
            p = h->node_heads[0];
        }
        else
        {
            for (i = 1; i < MAX_SESSIONS; i++)
            {
                if (h->session_names[i] != NULL && strcmp(h->session_names[i], session) == 0)
                {
                    p = h->node_heads[i];
                    break;
                }
            }
        }
    }
    else if (*index == (void *)-1)
        return NULL;
    else
        p = (node_t *)(*index);

    if (p == NULL)
        return NULL;
    else
    {
        if (p->p_next == NULL)
            *index = (void *)-1;
        else
            *index = p->p_next;
        return p->name;
    }
}


void sn_setting_print(sn_setting_t *h)
{
    node_t *p;
    int i;

    for (i = 0; i < MAX_SESSIONS; i++)
    {
        if (i == 0)
        {
            sn_log_info("################################ session 0, default ################################\n");
        }
        else if (h->session_names[i] != NULL)
        {
            sn_log_info("################################ session %d, name:%s ################################\n", i, h->session_names[i]);
        }
        p = h->node_heads[i];
        while (p != NULL)
        {
            if (p->type == SDT_STRING || p->type == SDT_BINARY)
            {
                sn_log_info("name:%s,type:%d,value:%s\n", p->name, p->type, p->value.vstring);
            }
            else if (p->type == SDT_INTEGER || p->type ==SDT_BOOL)
            {
                sn_log_info("name:%s,type:%d,value:%d\n", p->name, p->type, p->value.vint);
            }
            else if (p->type == SDT_DOUBLE)
            {
                sn_log_info("name:%s,type:%d,value:%.2f\n", p->name, p->type, p->value.vdouble);
            }
            else if (p->type == SDT_TIME)
            {
                struct tm t;
                localtime_r(&p->value.vtime, &t);
                sn_log_info("name:%s,type:%d,value:%s\n", p->name, p->type, asctime(&t));
            }
            p = p->p_next;
        }
    }
}
