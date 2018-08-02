#include <stdio.h>
#include "event.h"


static cb_event_t m_cbfun = NULL;
static void *m_context = NULL;


//上层调用这个接口注册事件回调
void set_event_callback(cb_event_t cbfun, void *context)
{
    m_cbfun = cbfun;
    m_context = context;
}


//server内部调用这个接口抛事件
void send_event(e_sevent type, void *identifier, unsigned long wparam, unsigned long lparam)
{
    if (m_cbfun == NULL)
        return;
    m_cbfun(m_context, type, identifier, wparam, lparam);
}

