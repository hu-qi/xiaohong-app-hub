/*
MIT License

Copyright (c) 2026 Shenzhen Open Source Co-Creation Technology Co., Ltd. (AtomGit)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "task_entry.h"
//#include "settings.h"
#include "log.h"

#define CREATE_TASK_DEBUG   0  // print task create error message to console

uint32_t create_task(task_config_t* task_conf, void *arg)
{
    uint32_t task_id = 0;
    if ((task_conf == NULL) ||
        (task_conf->name == NULL) || 
//      (task_conf->stack_size < TASK_STACK_SIZE_MIN) ||
        (task_conf->func == NULL)) {
        return task_id;
    }

    osThreadAttr_t task_attr = {0};
    task_attr.name       = task_conf->name;
    task_attr.attr_bits  = 0U;
    task_attr.cb_mem     = NULL;
    task_attr.cb_size    = 0U;
    task_attr.stack_mem  = NULL;
    task_attr.stack_size = task_conf->stack_size;
    task_attr.priority   = task_conf->priority;
    task_attr.tz_module  = 0U;

    task_id = (uint32_t) osThreadNew((osThreadFunc_t)task_conf->func, arg, &task_attr);

#if CREATE_TASK_DEBUG
    log_debug("[CreatTsk] =================================================================\r\n");
    log_debug("[CreatTsk] osThreadNew [%s]: name[%s], stack_size[%d], priority[%d]\r\n",
        (task_id == 0)?"NG":"OK", task_conf->name, task_conf->stack_size, task_conf->priority);
#endif
    for (uint8_t i = 0; (task_id == 0) && (i < 3); i++) {
        sleep(1);
        task_id = (uint32_t) osThreadNew((osThreadFunc_t)task_conf->func, arg, &task_attr);
        log_error("[CreatTsk] osThreadNew [%s]: name[%s], retry[%d] #######################\r\n",
                (task_id == 0)?"NG":"OK", task_conf->name, i);
    }
    if (task_id == 0) {
        log_fatal("[CreatTsk] CRITICAL ERROR: restart_system(1) ..................................\r\n");
//      restart_system(1);
    }
#if CREATE_TASK_DEBUG
    log_debug("[CreatTsk] =================================================================\r\n");
#endif
    return task_id;
}

/*
void restart_system(uint8_t delay_seconds)
{
    if (delay_seconds > 0) {
        sleep(delay_seconds);
    }
    cpu_utils_set_system_status_by_cause(0);
    hal_reboot_chip();
}
*/