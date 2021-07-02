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

#ifndef BLACKBOX_H
#define BLACKBOX_H

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif //__cplusplus
#endif //__cplusplus

#ifndef __user
#define __user
#endif

#define PATH_MAX_LEN            256
#define EVENT_MAX_LEN           32
#define MODULE_MAX_LEN          32
#define ERROR_DESC_MAX_LEN      512
#define MODULE_SYSTEM           "SYSTEM"
#define EVENT_SYSREBOOT         "SYSREBOOT"
#define EVENT_LONGPRESS         "LONGPRESS"
#define EVENT_COMBINATIONKEY    "COMBINATIONKEY"
#define EVENT_SUBSYSREBOOT      "SUBSYSREBOOT"
#define EVENT_POWEROFF          "POWEROFF"
#define EVENT_PANIC             "PANIC"
#define EVENT_SYS_WATCHDOG      "SYSWATCHDOG"
#define EVENT_HUNGTASK          "HUNGTASK"
#define EVENT_BOOTFAIL          "BOOTFAIL"

struct ErrorInfo {
    char event[EVENT_MAX_LEN];
    char module[MODULE_MAX_LEN];
    char errorDesc[ERROR_DESC_MAX_LEN];
};

struct ModuleOps {
    char module[MODULE_MAX_LEN];
    void (*Dump)(const char *logDir, struct ErrorInfo *info);
    void (*Reset)(struct ErrorInfo *info);
    int (*GetLastLogInfo)(struct ErrorInfo *info);
    int (*SaveLastLog)(const char *logDir, struct ErrorInfo *info);
};

int BBoxRegisterModuleOps(struct ModuleOps *ops);
int BBoxNotifyError(const char event[EVENT_MAX_LEN],
    const char module[MODULE_MAX_LEN],
    const char errorDesc[ERROR_DESC_MAX_LEN],
    int needSysReset);
int BBoxDriverInit(void);

#ifdef __cplusplus
#if __cplusplus
}
#endif //__cplusplus
#endif //__cplusplus

#endif 