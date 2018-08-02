#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "rule.h"


#define MAX_RULE_NUM 64

typedef struct _ruleitem 
{
    int used;

    //TODO 注意大小端的问题
    uint32_t ip;
    uint32_t mask;
    uint8_t opt;
    char file[1024];
    time_t add_time;
    int duration;
} ruleitem_t;

static ruleitem_t m_rule_table[MAX_RULE_NUM];


int rule_init()
{
    memset(&m_rule_table, 0, sizeof(m_rule_table));
    return 0;
}


void rule_exit()
{
}


static void remove_timeout()
{
    ruleitem_t *item;
    time_t now;
    int i;
    time(&now);
    for (i = 0; i < MAX_RULE_NUM; i++)
    {
        item = &m_rule_table[i];
        if (item->used == 1 && item->duration > 0 && now - item->add_time > item->duration)
            item->used = 0;
    }
}


int rule_add(uint32_t ip, uint32_t mask, uint32_t opt, int duration, const char *filename)
{
    ruleitem_t *item;
    time_t now;
    int found;
    int i;

    time(&now);

    remove_timeout();

    for (i = 0; i < MAX_RULE_NUM; i++)
    {
        item = &m_rule_table[i];
        if (item->used == 1 && (ip & mask) == (item->ip & item->mask)
                && opt == item->opt && strcmp(item->file, filename) == 0)
        {
            item->duration = duration;
            time(&item->add_time);
            return 0;
        }
    }

    found = 0;
    for (i = 0; i < MAX_RULE_NUM; i++)
    {
        item = &m_rule_table[i];
        if (item->used == 0 || (item->duration > 0 && (now - item->add_time > item->duration)))
        {
            found = 1;
            break;
        }
    }
    if (found == 0)
        return -1;
    item->ip = ip;
    item->mask = mask;
    item->opt = opt;
    item->duration = duration;
    item->used = 1;
    memset(item->file, 0, 1024);
    if (filename != NULL)
        strncpy(item->file, filename, 1023);
    time(&item->add_time);
    return 0;
}


int rule_match(uint32_t ip, uint32_t opt, const char *filename)
{
    ruleitem_t *item;
    time_t now;
    int i;

    time(&now);
    
    for (i = 0; i < MAX_RULE_NUM; i++)
    {
        item = &m_rule_table[i];
        if (item->used == 1 && (ip & item->mask) == (item->ip & item->mask) && (opt & item->opt) > 0)
        {
            if (item->duration > 0 && (now - item->add_time > item->duration))
            {
                item->used = 0;
                continue;
            }
            if (strlen(item->file) == 0)
                return 0;
            if (filename == NULL)
                return -1;
            if (strncmp(item->file, filename, strlen(item->file)) == 0)
                return 0;
        }
    }
    return -1;
}

