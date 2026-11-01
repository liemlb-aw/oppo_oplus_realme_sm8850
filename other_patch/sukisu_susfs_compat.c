#include <linux/cred.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/fs_struct.h>
#include <linux/string.h>
#include <linux/susfs.h>
#include "feature/sucompat.h"
#include "policy/allowlist.h"
#include "uapi/supercall.h"

int ksu_handle_faccessat(int *dfd, struct filename **filename, int *mode,
                       int *flags)
{
    if (!ksu_su_compat_enabled || !ksu_is_allow_uid_for_current(current_uid().val))
        return 0;
    if (!filename || IS_ERR_OR_NULL(*filename) || !(*filename)->name)
        return 0;
    if (strcmp((*filename)->name, "/system/bin/su") || current_chrooted())
        return 0;

    memcpy((void *)(*filename)->name, "/system/bin/sh", sizeof("/system/bin/sh"));
    return 0;
}

int ksu_handle_sys_reboot(int magic1, int magic2, unsigned int cmd, void __user **arg)
{
    if (magic1 != KSU_INSTALL_MAGIC1 || magic2 != SUSFS_MAGIC ||
        current_uid().val != 0)
        return -EINVAL;

    switch (cmd) {
#ifdef CONFIG_KSU_SUSFS_SUS_PATH
    case CMD_SUSFS_ADD_SUS_PATH:
        susfs_add_sus_path(arg);
        return 0;
    case CMD_SUSFS_ADD_SUS_PATH_LOOP:
        susfs_add_sus_path_loop(arg);
        return 0;
#endif
#ifdef CONFIG_KSU_SUSFS_SUS_MOUNT
    case CMD_SUSFS_HIDE_SUS_MNTS_FOR_NON_SU_PROCS:
        susfs_set_hide_sus_mnts_for_non_su_procs(arg);
        return 0;
#endif
#ifdef CONFIG_KSU_SUSFS_SUS_KSTAT
    case CMD_SUSFS_ADD_SUS_KSTAT:
    case CMD_SUSFS_ADD_SUS_KSTAT_STATICALLY:
        susfs_add_sus_kstat(arg);
        return 0;
    case CMD_SUSFS_UPDATE_SUS_KSTAT:
        susfs_update_sus_kstat(arg);
        return 0;
#endif
#ifdef CONFIG_KSU_SUSFS_SPOOF_UNAME
    case CMD_SUSFS_SET_UNAME:
        susfs_set_uname(arg);
        return 0;
#endif
#ifdef CONFIG_KSU_SUSFS_ENABLE_LOG
    case CMD_SUSFS_ENABLE_LOG:
        susfs_enable_log(arg);
        return 0;
#endif
#ifdef CONFIG_KSU_SUSFS_SPOOF_CMDLINE_OR_BOOTCONFIG
    case CMD_SUSFS_SET_CMDLINE_OR_BOOTCONFIG:
        susfs_set_cmdline_or_bootconfig(arg);
        return 0;
#endif
#ifdef CONFIG_KSU_SUSFS_OPEN_REDIRECT
    case CMD_SUSFS_ADD_OPEN_REDIRECT:
        susfs_add_open_redirect(arg);
        return 0;
#endif
#ifdef CONFIG_KSU_SUSFS_SUS_MAP
    case CMD_SUSFS_ADD_SUS_MAP:
        susfs_add_sus_map(arg);
        return 0;
#endif
    case CMD_SUSFS_ENABLE_AVC_LOG_SPOOFING:
        susfs_set_avc_log_spoofing(arg);
        return 0;
    case CMD_SUSFS_SHOW_ENABLED_FEATURES:
        susfs_get_enabled_features(arg);
        return 0;
    case CMD_SUSFS_SHOW_VARIANT:
        susfs_show_variant(arg);
        return 0;
    case CMD_SUSFS_SHOW_VERSION:
        susfs_show_version(arg);
        return 0;
    default:
        return -EINVAL;
    }
}