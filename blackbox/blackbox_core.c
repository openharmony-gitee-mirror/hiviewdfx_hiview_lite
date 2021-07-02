/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "blackbox.h"
#include "blackbox_common.h"
#include "blackbox_detector.h"
#include "ohos_types.h"
#include "ohos_init.h"
#include "securec.h"
#include <memory.h>
#include <pthread.h>
#include <los_list.h>
#include <los_sem.h>
#include <los_task.h>
#ifdef PLATFORM_HI3861
#include <hi_reset.h>
#endif
#include "hiview_log.h"

/******************local macroes*********************/
#define LOG_ROOT_DIR_WAIT_TIME 1000
#define LOG_ROOT_DIR_WAIT_COUNT 1

/******************local prototypes******************/
struct BBoxOps {
    LOS_DL_LIST opsList;
    struct ModuleOps ops;
};

/******************local variables*******************/
static LOS_DL_LIST_HEAD(g_opsList);
static unsigned int g_opsListSem;

/******************function definitions*******************/
static void FormatErrorInfo(struct ErrorInfo *info,
    const char event[EVENT_MAX_LEN],
    const char module[MODULE_MAX_LEN],
    const char errorDesc[ERROR_DESC_MAX_LEN])
{
    if (info == NULL || event == NULL || module == NULL || errorDesc == NULL) {
        BBOX_PRINT_ERR("info: %p, event: %p, module: %p, errorDesc: %p\n",
            info, event, module, errorDesc);
        return;
    }

    (void)memset_s(info, sizeof(*info), 0, sizeof(*info));
    strncpy_s(info->event, sizeof(info->event), event,
        Min(strlen(event), sizeof(info->event) - 1));
    strncpy_s(info->module, sizeof(info->module), module,
        Min(strlen(module), sizeof(info->module) - 1));
    strncpy_s(info->errorDesc, sizeof(info->errorDesc), errorDesc,
        Min(strlen(errorDesc), sizeof(info->errorDesc) - 1));
}

static void WaitForLogRootDir(const char *rootDir)
{
    int i = 0;

    if (rootDir == NULL) {
        BBOX_PRINT_ERR("rootDir: %p\n", rootDir);
        return;
    }
    BBOX_PRINT_INFO("wait for log root dir [%s] begin!\n", rootDir);
    while (i++ < LOG_ROOT_DIR_WAIT_COUNT) {
        LOS_Msleep(LOG_ROOT_DIR_WAIT_TIME);
    }
    BBOX_PRINT_INFO("wait for log root dir [%s] end!\n", rootDir);
}

static void SaveBasicErrorInfo(const char *filePath, struct ErrorInfo *info)
{
    char *buf;

    if (filePath == NULL || info == NULL) {
        BBOX_PRINT_ERR("filePath: %p, info: %p!\n", filePath, info);
        return;
    }

    buf = malloc(ERROR_INFO_MAX_LEN);
    if (buf == NULL) {
        BBOX_PRINT_ERR("malloc failed!\n");
        return;
    }
    (void)memset_s(buf, ERROR_INFO_MAX_LEN, 0, ERROR_INFO_MAX_LEN);
    (void)snprintf_s(buf, ERROR_INFO_MAX_LEN, ERROR_INFO_MAX_LEN - 1,
        ERROR_INFO_HEADER ERROR_INFO_HEADER_FORMAT,
        info->event, info->module, info->errorDesc);
    *(buf + ERROR_INFO_MAX_LEN - 1) = '\0';
    (void)FullWriteFile(filePath, buf, strlen(buf), 0);
    free(buf);
    BBOX_PRINT_INFO("[%s] starts uploading event [%s]\n",
        info->module, info->event);
    (void)UploadEventByFile(filePath);
    BBOX_PRINT_INFO("[%s] ends uploading event [%s]\n",
        info->module, info->event);
}

static void* SaveLastLog(void *param)
{
    struct ErrorInfo *info;
    struct BBoxOps *ops;

    info = malloc(sizeof(*info));
    if (info == NULL) {
        BBOX_PRINT_ERR("malloc failed!\n");
        return NULL;
    }

    WaitForLogRootDir(LOG_ROOT_PATH);
    if (LOS_SemPend(g_opsListSem, LOS_WAIT_FOREVER) != 0) {
        BBOX_PRINT_ERR("Request g_opsListSem failed!\n");
        free(info);
        return NULL;
    }
    LOS_DL_LIST_FOR_EACH_ENTRY(ops, &g_opsList, struct BBoxOps, opsList) {
        if (ops == NULL) {
            continue;
        }
        if (ops->ops.GetLastLogInfo != NULL && ops->ops.SaveLastLog != NULL) {
            memset(info, 0, sizeof(*info));
            if (ops->ops.GetLastLogInfo(info) != 0) {
                BBOX_PRINT_ERR("[%s] failed to get log info!\n",
                    ops->ops.module);
                    continue;
            }
            BBOX_PRINT_INFO("[%s] starts saving log!\n", ops->ops.module);
            if (ops->ops.SaveLastLog(LOG_ROOT_PATH, info) != 0) {
                BBOX_PRINT_ERR("[%s] failed to save log!\n", ops->ops.module);
            }
        }
    }
    (void)LOS_SemPost(g_opsListSem);
    free(info);

    return NULL;
}

#ifdef BLACKBOX_DEBUG
static void PrintModuleOps(void)
{
    struct BBoxOps *temp;

    BBOX_PRINT_INFO("The following modules have been registered!\n");
    LOS_DL_LIST_FOR_EACH_ENTRY(temp, &g_opsList, struct BBoxOps, opsList) {
        BBOX_PRINT_INFO("module: %s, Dump: %p, Reset: %p, "
            "GetLastLogInfo: %p, SaveLastLog: %p\n",
            temp->ops.module, temp->ops.Dump, temp->ops.Reset,
            temp->ops.GetLastLogInfo, temp->ops.SaveLastLog);
    }
}
#endif

int BBoxRegisterModuleOps(struct ModuleOps *ops)
{
    struct BBoxOps *newOps;
    struct BBoxOps *temp;

    if (ops == NULL) {
        BBOX_PRINT_ERR("ops: %p!\n", ops);
        return -1;
    }

    /* Use malloc to avoid the stack overflow */
    newOps = malloc(sizeof(*newOps));
    if (newOps == NULL) {
        BBOX_PRINT_ERR("malloc failed!\n");
        return -1;
    }
    (void)memset_s(newOps, sizeof(*newOps), 0, sizeof(*newOps));
    (void)memcpy_s(&newOps->ops, sizeof(newOps->ops), ops, sizeof(*ops));
    if (LOS_SemPend(g_opsListSem, LOS_WAIT_FOREVER) != 0) {
        BBOX_PRINT_ERR("Request g_opsListSem failed!\n");
        free(newOps);
        return -1;
    }
    if (LOS_ListEmpty(&g_opsList)) {
        goto __out;
    }
    LOS_DL_LIST_FOR_EACH_ENTRY(temp, &g_opsList, struct BBoxOps, opsList) {
        if (strcmp(temp->ops.module, ops->module) == 0) {
            BBOX_PRINT_ERR("[%s] has been registered!\n", ops->module);
            (void)LOS_SemPost(g_opsListSem);
            free(newOps);
            return -1;
        }
    }

__out:
    BBOX_PRINT_INFO("[%s] is registered successfully!\n", ops->module);
    LOS_ListTailInsert(&g_opsList, &newOps->opsList);
    (void)LOS_SemPost(g_opsListSem);
#ifdef BLACKBOX_DEBUG
    PrintModuleOps();
#endif

    return 0;
}

int BBoxNotifyError(const char event[EVENT_MAX_LEN],
    const char module[MODULE_MAX_LEN],
    const char errorDesc[ERROR_DESC_MAX_LEN],
    int needSysReset)
{
    int findModule = 0;
    struct BBoxOps *ops;
    struct ErrorInfo *info = NULL;

    info = malloc(sizeof(*info));
    if (info == NULL) {
        BBOX_PRINT_ERR("malloc failed!\n");
        return -1;
    }

    if (needSysReset == 0) {
       WaitForLogRootDir(LOG_ROOT_PATH);
       if (LOS_SemPend(g_opsListSem, LOS_NO_WAIT) != 0) {
           BBOX_PRINT_ERR("Request g_opsListSem failed!\n");
           goto __out;
       }
    }

    LOS_DL_LIST_FOR_EACH_ENTRY(ops, &g_opsList, struct BBoxOps, opsList) {
        if (ops == NULL) {
            BBOX_PRINT_ERR("ops: %p!\n", ops);
            continue;
        }
        if (strcmp(ops->ops.module, module) != 0) {
            continue;
        }
        FormatErrorInfo(info, event, module, errorDesc);
        if (ops->ops.Dump == NULL && ops->ops.Reset == NULL) {
            SaveBasicErrorInfo(FAULT_LOG_PATH, info);
            break;
        }
        if (ops->ops.Dump != NULL) {
            BBOX_PRINT_INFO("[%s] starts dumping data!\n", ops->ops.module);
            ops->ops.Dump(LOG_ROOT_PATH, info);
            BBOX_PRINT_INFO("[%s] ends dumping data!\n", ops->ops.module);
        }
        if (ops->ops.Reset != NULL) {
            BBOX_PRINT_INFO("[%s] starts resetting!\n", ops->ops.module);
            ops->ops.Reset(info);
            BBOX_PRINT_INFO("[%s] ends resetting!\n", ops->ops.module);
        }
        findModule = 1;
        break;
    }
    if (needSysReset == 0) {
        (void)LOS_SemPost(g_opsListSem);
    }

__out:
    if (info != NULL) {
        free(info);
    }
    if (needSysReset != 0 && findModule != 0) {
#ifdef PLATFORM_HI3861
        hi_hard_reboot(HI_SYS_REBOOT_CAUSE_CMD);
#else
        LOS_Reboot();
#endif
    }

    return 0;
}

static void BBoxInit(void)
{
    int ret;
    pthread_t taskId;

    if (LOS_BinarySemCreate(1, &g_opsListSem) != LOS_OK) {
        BBOX_PRINT_ERR("Create binary semaphore failed!\n");
        return;
    }
    LOS_ListInit(&g_opsList);
    ret = pthread_create(&taskId, NULL, SaveLastLog, NULL);
    if (ret != 0) {
        BBOX_PRINT_ERR("Falied to create SaveLastLog task, ret: %d\n", ret);
    }
}
CORE_INIT_PRI(BBoxInit, 1);