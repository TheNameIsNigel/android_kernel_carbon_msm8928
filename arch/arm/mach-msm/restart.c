/* Copyright (c) 2010-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/reboot.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/cpu.h>
#include <linux/interrupt.h>
#include <linux/mfd/pmic8058.h>
#include <linux/mfd/pmic8901.h>
#include <linux/mfd/pm8xxx/misc.h>
#include <linux/qpnp/power-on.h>

#include <asm/mach-types.h>
#include <asm/cacheflush.h>

#include <mach/msm_iomap.h>
#include <mach/restart.h>
#include <mach/socinfo.h>
#include <mach/irqs.h>
#include <mach/scm.h>
#include "msm_watchdog.h"
#include "timer.h"
#include "wdog_debug.h"

#ifdef CONFIG_HUAWEI_KERNEL
#include <linux/fcntl.h>
#include <linux/syscalls.h>

#define MISC_DEVICE "/dev/block/platform/msm_sdcc.1/by-name/misc"
#define USB_UPDATE_POLL_TIME  2000
struct bootloader_message {
    char command[32];
    char status[32];
    char recovery[768];
    char stage[32];
};
#endif

#ifdef CONFIG_HUAWEI_KERNEL
#include <linux/huawei_apanic.h>
#endif
#ifdef CONFIG_HUAWEI_FEATURE_NFF
#include <linux/huawei_boot_log.h>
extern void*  boot_log_virt ;
#endif

#define WDT0_RST	0x38
#define WDT0_EN		0x40
#define WDT0_BARK_TIME	0x4C
#define WDT0_BITE_TIME	0x5C

#define PSHOLD_CTL_SU (MSM_TLMM_BASE + 0x820)

#define RESTART_REASON_ADDR 0x65C
#define DLOAD_MODE_ADDR     0x0
#define EMERGENCY_DLOAD_MODE_ADDR    0xFE0
#define EMERGENCY_DLOAD_MAGIC1    0x322A4F99
#define EMERGENCY_DLOAD_MAGIC2    0xC67E4350
#define EMERGENCY_DLOAD_MAGIC3    0x77777777
//Added magic mumber for sdupdate and usbupdate
#define SDUPDATE_FLAG_MAGIC_NUM  0x77665528
#define USBUPDATE_FLAG_MAGIC_NUM  0x77665523
#define SD_UPDATE_RESET_FLAG   "sdupdate"
#define USB_UPDATE_RESET_FLAG   "usbupdate"
#define SCM_IO_DISABLE_PMIC_ARBITER	1

#ifdef CONFIG_MSM_RESTART_V2
#define use_restart_v2()	1
#else
#define use_restart_v2()	0
#endif

#ifdef CONFIG_HUAWEI_KERNEL
#define RESTART_FLAG_ADDR    0x800
#define RESTART_FLAG_MAGIC_NUM    0x20890206
#define restart_flag_addr     (MSM_IMEM_BASE + RESTART_FLAG_ADDR)
#ifdef CONFIG_HUAWEI_KERNEL
#define QFUSE_MAGIC_NUM  0xF4C3D2C1
#define QFUSE_MAGIC_OFFSET  0x24
#endif
#endif
#ifdef CONFIG_FEATURE_HUAWEI_EMERGENCY_DATA
#define MOUNTFAIL_MAGIC_NUM 0x77665527
#endif
#ifdef CONFIG_HUAWEI_DEBUG_MODE
extern char *saved_command_line;
#endif

static int restart_mode;
void *restart_reason;

int pmic_reset_irq;
static void __iomem *msm_tmr0_base;

#ifdef CONFIG_MSM_DLOAD_MODE
static int in_panic;
static void *dload_mode_addr;
static bool dload_mode_enabled;
static void *emergency_dload_mode_addr;

/* Download mode master kill-switch */
static int dload_set(const char *val, struct kernel_param *kp);
#ifndef CONFIG_HUAWEI_KERNEL_DEBUG
/* disable ramdump by default*/
static int download_mode = 0;
#else
static int download_mode = 1;
#endif
module_param_call(download_mode, dload_set, param_get_int,
			&download_mode, 0644);
static int panic_prep_restart(struct notifier_block *this,
			      unsigned long event, void *ptr)
{
	in_panic = 1;
	return NOTIFY_DONE;
}

static struct notifier_block panic_blk = {
	.notifier_call	= panic_prep_restart,
};

static void set_dload_mode(int on)
{
	if (dload_mode_addr) {
		__raw_writel(on ? 0xE47B337D : 0, dload_mode_addr);
		__raw_writel(on ? 0xCE14091A : 0,
		       dload_mode_addr + sizeof(unsigned int));
		mb();
		dload_mode_enabled = on;
	}
}

#ifdef CONFIG_HUAWEI_KERNEL
void clear_dload_mode(void)
{
	set_dload_mode(0);
}
#endif
#ifdef CONFIG_HUAWEI_FEATURE_NFF
static void clear_bootup_flag(void)
{
    /*if the HUAWEI_BOOT_LOG_ADDR can be used, donot map again*/
    uint32_t *reboot_flag_addr = NULL;
    if(NULL == boot_log_virt )
    {
         reboot_flag_addr = (uint32_t *)ioremap_nocache(HUAWEI_BOOT_LOG_ADDR,HUAWEI_BOOT_LOG_SIZE);
    }
    else
    {
        reboot_flag_addr = boot_log_virt ;
    }
    if(NULL != reboot_flag_addr)
    {
        __raw_writel( 0x00000000, reboot_flag_addr);
        mb();
    }
    return;
}
#endif
static bool get_dload_mode(void)
{
	return dload_mode_enabled;
}

#ifdef CONFIG_HUAWEI_KERNEL
/*This function write usb_update sign to the misc partion,
   if write successfull, then hard restart the device*/
void huawei_restart(void)
{
    struct bootloader_message boot;
    int fd  = 0;
    
    memset((void*)&boot, 0x0, sizeof(struct bootloader_message));    
    strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
    strcpy(boot.recovery, "recovery\n");
    strcat(boot.recovery, "--");
    strcat(boot.recovery, "usb_update");

    fd = sys_open(MISC_DEVICE,O_RDWR,0);
    if( fd < 0 )
    {
        pr_err("open the devices %s fail",MISC_DEVICE);
        return ;
    }
    if(sys_write((unsigned int )fd, (char*)&boot, sizeof(boot)) < 0)
    {
        sys_close(fd);
        pr_err("write to the devices %s fail",MISC_DEVICE);
        return;
    }
    sys_close(fd);
    sys_sync();
    kernel_restart(NULL);
}
/*This function poll the address restart_reason,if 
   there is the magic SDUPDATE_FLAG_MAGIC_NUM, restart
   the devices.,the magic is written by modem when the
   user click the usb update tool to update*/
int usb_update_thread(void *__unused)
{
    unsigned int  dload_magic = 0;
    for(;;)
    {
        if(NULL != restart_reason)
        {
             dload_magic = __raw_readl(restart_reason);
        }
        else
        {
             pr_info("restart_reason is null,wait for ready\n");
        }
        if(SDUPDATE_FLAG_MAGIC_NUM == dload_magic)
        {
            pr_info("update mode, restart to usb update\n");
            huawei_restart();
        }
        msleep(USB_UPDATE_POLL_TIME);
    }
    return 0;
}
#endif

static void enable_emergency_dload_mode(void)
{
	if (emergency_dload_mode_addr) {
		__raw_writel(EMERGENCY_DLOAD_MAGIC1,
				emergency_dload_mode_addr);
		__raw_writel(EMERGENCY_DLOAD_MAGIC2,
				emergency_dload_mode_addr +
				sizeof(unsigned int));
		__raw_writel(EMERGENCY_DLOAD_MAGIC3,
				emergency_dload_mode_addr +
				(2 * sizeof(unsigned int)));

		/* Need disable the pmic wdt, then the emergency dload mode
		 * will not auto reset. */
		qpnp_pon_wd_config(0);
		mb();
	}
}

static int dload_set(const char *val, struct kernel_param *kp)
{
	int ret;
	int old_val = download_mode;

	ret = param_set_int(val, kp);

	if (ret)
		return ret;

	/* If download_mode is not zero or one, ignore. */
	if (download_mode >> 1) {
		download_mode = old_val;
		return -EINVAL;
	}

	set_dload_mode(download_mode);

	return 0;
}
#else
#define set_dload_mode(x) do {} while (0)

static void enable_emergency_dload_mode(void)
{
	printk(KERN_ERR "dload mode is not enabled on target\n");
}

static bool get_dload_mode(void)
{
	return false;
}
#endif

void msm_set_restart_mode(int mode)
{
	restart_mode = mode;
}
EXPORT_SYMBOL(msm_set_restart_mode);

static bool scm_pmic_arbiter_disable_supported;
/*
 * Force the SPMI PMIC arbiter to shutdown so that no more SPMI transactions
 * are sent from the MSM to the PMIC.  This is required in order to avoid an
 * SPMI lockup on certain PMIC chips if PS_HOLD is lowered in the middle of
 * an SPMI transaction.
 */
static void halt_spmi_pmic_arbiter(void)
{
	if (scm_pmic_arbiter_disable_supported) {
		pr_crit("Calling SCM to disable SPMI PMIC arbiter\n");
		scm_call_atomic1(SCM_SVC_PWR, SCM_IO_DISABLE_PMIC_ARBITER, 0);
	}
}

static void __msm_power_off(int lower_pshold)
{
	printk(KERN_CRIT "Powering off the SoC\n");
#ifdef CONFIG_MSM_DLOAD_MODE
	set_dload_mode(0);
#endif
	pm8xxx_reset_pwr_off(0);
	qpnp_pon_system_pwr_off(PON_POWER_OFF_SHUTDOWN);

	if (lower_pshold) {
		if (!use_restart_v2()) {
			__raw_writel(0, PSHOLD_CTL_SU);
		} else {
			halt_spmi_pmic_arbiter();
			__raw_writel(0, MSM_MPM2_PSHOLD_BASE);
		}

		mdelay(10000);
		printk(KERN_ERR "Powering off has failed\n");
	}
	return;
}

static void msm_power_off(void)
{
	/* MSM initiated power off, lower ps_hold */
#ifdef CONFIG_HUAWEI_KERNEL
	/*clear hardware reset magic number to imem*/
	__raw_writel(0, HW_RESET_LOG_MAGIC_NUM_ADDR);
	printk("clear hardware reset magic number when power off\n");
#endif
	__msm_power_off(1);
}

static void cpu_power_off(void *data)
{
	int rc;

	pr_err("PMIC Initiated shutdown %s cpu=%d\n", __func__,
						smp_processor_id());
	if (smp_processor_id() == 0) {
		/*
		 * PMIC initiated power off, do not lower ps_hold, pmic will
		 * shut msm down
		 */
		__msm_power_off(0);

		pet_watchdog();
		pr_err("Calling scm to disable arbiter\n");
		/* call secure manager to disable arbiter and never return */
		rc = scm_call_atomic1(SCM_SVC_PWR,
						SCM_IO_DISABLE_PMIC_ARBITER, 1);

		pr_err("SCM returned even when asked to busy loop rc=%d\n", rc);
		pr_err("waiting on pmic to shut msm down\n");
	}

	preempt_disable();
	while (1)
		;
}

static irqreturn_t resout_irq_handler(int irq, void *dev_id)
{
	pr_warn("%s PMIC Initiated shutdown\n", __func__);
	oops_in_progress = 1;
	smp_call_function_many(cpu_online_mask, cpu_power_off, NULL, 0);
	if (smp_processor_id() == 0)
		cpu_power_off(NULL);
	preempt_disable();
	while (1)
		;
	return IRQ_HANDLED;
}

#ifdef CONFIG_HUAWEI_KERNEL
static void qfuse_handle(const char *cmd)
{
    char *fuse_data_p = NULL;
    unsigned int rdata = 0;

    if(NULL == cmd)
    {
        return;
    }
    
    //write qfuse magic
    __raw_writel(QFUSE_MAGIC_NUM, (dload_mode_addr+QFUSE_MAGIC_OFFSET)); /*addr 0x2A03F000 */

    //get r0
    fuse_data_p = strstr(cmd, "r0=");
    rdata = 0;
    if(NULL != fuse_data_p)
    {
        fuse_data_p = fuse_data_p+3;
        if(NULL != fuse_data_p)
        {
            rdata = simple_strtoul(fuse_data_p, NULL, 16);
        }
    }
    //write r0
    pr_err("qfuse_handle r0 = 0x%x\n", rdata);
    __raw_writel(rdata, (dload_mode_addr+QFUSE_MAGIC_OFFSET+4));
    pr_err("qfuse_handle dload_mode_addr r0 = 0x%x\n", *((unsigned int*)(dload_mode_addr+QFUSE_MAGIC_OFFSET+4)));

    //get r1
    fuse_data_p = strstr(cmd, "r1=");
    rdata = 0;
    if(NULL != fuse_data_p)
    {
        fuse_data_p = fuse_data_p+3;
        if(NULL != fuse_data_p)
        {
            rdata = simple_strtoul(fuse_data_p, NULL, 16);
        }
    }
    //write r1
    pr_err("qfuse_handle r1 = 0x%x\n", rdata);
    __raw_writel(rdata, (dload_mode_addr+QFUSE_MAGIC_OFFSET+8));
    pr_err("qfuse_handle dload_mode_addr r1 = 0x%x\n", *((unsigned int*)(dload_mode_addr+QFUSE_MAGIC_OFFSET+8)));

    //get r2
    fuse_data_p = strstr(cmd, "r2=");
    rdata = 0;
    if(NULL != fuse_data_p)
    {
        fuse_data_p = fuse_data_p+3;
        if(NULL != fuse_data_p)
        {
            rdata = simple_strtoul(fuse_data_p, NULL, 16);
        }
    }

    //write r2
    pr_err("qfuse_handle r2 = 0x%x\n", rdata);
    __raw_writel(rdata, (dload_mode_addr+QFUSE_MAGIC_OFFSET+12));
    pr_err("qfuse_handle dload_mode_addr r2 = 0x%x\n", *((unsigned int*)(dload_mode_addr+QFUSE_MAGIC_OFFSET+12)));

    //get r3
    fuse_data_p = strstr(cmd, "r3=");
    rdata = 0;
    if(NULL != fuse_data_p)
    {
        fuse_data_p = fuse_data_p+3;
        if(NULL != fuse_data_p)
        {
            rdata = simple_strtoul(fuse_data_p, NULL, 16);
        }
    }
    //write r3
    pr_err("qfuse_handle r3 = 0x%x\n", rdata);
    __raw_writel(rdata, (dload_mode_addr+QFUSE_MAGIC_OFFSET+16));
    pr_err("qfuse_handle dload_mode_addr r3 = 0x%x\n", *((unsigned int*)(dload_mode_addr+QFUSE_MAGIC_OFFSET+16)));

    pr_err("qfuse_handle dload_mode_addr magic = 0x%x\n", *((unsigned int*)(dload_mode_addr+QFUSE_MAGIC_OFFSET)));

    //enter recovery mode
    //__raw_writel(0x77665502, restart_reason); 

    mb();
}
#endif
static void msm_restart_prepare(const char *cmd)
{
#ifdef CONFIG_MSM_DLOAD_MODE

	/* This looks like a normal reboot at this point. */
	set_dload_mode(0);

	/* Write download mode flags if we're panic'ing */
	set_dload_mode(in_panic);

	/* Write download mode flags if restart_mode says so */
	if (restart_mode == RESTART_DLOAD)
		set_dload_mode(1);

	/* Kill download mode if master-kill switch is set */
	if (!download_mode)
		set_dload_mode(0);
#endif

#ifdef CONFIG_HUAWEI_KERNEL
	/*clear hardware reset magic number to imem*/
	__raw_writel(0, HW_RESET_LOG_MAGIC_NUM_ADDR);
	printk("clear hardware reset magic number when reboot\n");
#endif
#ifdef CONFIG_HUAWEI_KERNEL
	__raw_writel(RESTART_FLAG_MAGIC_NUM, restart_flag_addr);
#endif
	pm8xxx_reset_pwr_off(1);
#ifdef CONFIG_HUAWEI_FEATURE_NFF
    clear_bootup_flag();
#endif     
	/* Hard reset the PMIC unless memory contents must be maintained. */
	if (get_dload_mode() || (cmd != NULL && cmd[0] != '\0'))
		qpnp_pon_system_pwr_off(PON_POWER_OFF_WARM_RESET);
	else
		qpnp_pon_system_pwr_off(PON_POWER_OFF_HARD_RESET);

	if (cmd != NULL) {
		if (!strncmp(cmd, "bootloader", 10)) {
			__raw_writel(0x77665500, restart_reason);
		} else if (!strncmp(cmd, "recovery", 8)) {
			__raw_writel(0x77665502, restart_reason);
		} else if (!strcmp(cmd, "rtc")) {
			__raw_writel(0x77665503, restart_reason);
		} else if (!strncmp(cmd, "oem-", 4)) {
			unsigned long code;
			code = simple_strtoul(cmd + 4, NULL, 16) & 0xff;
			__raw_writel(0x6f656d00 | code, restart_reason);
		} else if (!strncmp(cmd, "edl", 3)) {
			enable_emergency_dload_mode();
#ifdef CONFIG_HUAWEI_KERNEL
		} else if (!strncmp(cmd, "huawei_dload", 12)) {
			__raw_writel(0x77665503, restart_reason);
        //Added adb reboot sdupdate/usbupdate command support
        } else if(!strncmp(cmd, SD_UPDATE_RESET_FLAG, strlen(SD_UPDATE_RESET_FLAG))) {
            __raw_writel(SDUPDATE_FLAG_MAGIC_NUM, restart_reason);
        } else if(!strncmp(cmd, USB_UPDATE_RESET_FLAG, strlen(USB_UPDATE_RESET_FLAG))) {
            __raw_writel(USBUPDATE_FLAG_MAGIC_NUM, restart_reason);
#endif
#ifdef CONFIG_HUAWEI_KERNEL
		}  else if (!strncmp(cmd, "huawei_rtc", 10)) {
					   __raw_writel(0x77665524, restart_reason);
#endif
#ifdef CONFIG_HUAWEI_KERNEL
		}  else if (!strncmp(cmd, "emergency_restart", 17)) {
			printk("do nothing\n");
#endif
#ifdef CONFIG_FEATURE_HUAWEI_EMERGENCY_DATA
		} else if (!strncmp(cmd, "mountfail", strlen("mountfail"))) {
		    __raw_writel(MOUNTFAIL_MAGIC_NUM, restart_reason);
#endif
#ifdef CONFIG_HUAWEI_KERNEL
        } else if(!strncmp(cmd, "qfuse", 5)) {
            qfuse_handle(cmd);
#endif
		} else {
			__raw_writel(0x77665501, restart_reason);
		}
	}

	flush_cache_all();
	outer_flush_all();
}

void msm_restart(char mode, const char *cmd)
{
	printk(KERN_NOTICE "Going down for restart now\n");

	msm_restart_prepare(cmd);

	if (!use_restart_v2()) {
		__raw_writel(0, msm_tmr0_base + WDT0_EN);
		if (!(machine_is_msm8x60_fusion() ||
		      machine_is_msm8x60_fusn_ffa())) {
			mb();
			 /* Actually reset the chip */
			__raw_writel(0, PSHOLD_CTL_SU);
			mdelay(5000);
			pr_notice("PS_HOLD didn't work, falling back to watchdog\n");
		}

		__raw_writel(1, msm_tmr0_base + WDT0_RST);
		__raw_writel(5*0x31F3, msm_tmr0_base + WDT0_BARK_TIME);
		__raw_writel(0x31F3, msm_tmr0_base + WDT0_BITE_TIME);
		__raw_writel(1, msm_tmr0_base + WDT0_EN);
	} else {
		/* Needed to bypass debug image on some chips */
		msm_disable_wdog_debug();
		halt_spmi_pmic_arbiter();
		__raw_writel(0, MSM_MPM2_PSHOLD_BASE);
	}

	mdelay(10000);
	printk(KERN_ERR "Restarting has failed\n");
}

static int __init msm_pmic_restart_init(void)
{
	int rc;

	if (use_restart_v2())
		return 0;

	if (pmic_reset_irq != 0) {
		rc = request_any_context_irq(pmic_reset_irq,
					resout_irq_handler, IRQF_TRIGGER_HIGH,
					"restart_from_pmic", NULL);
		if (rc < 0)
			pr_err("pmic restart irq fail rc = %d\n", rc);
	} else {
		pr_warn("no pmic restart interrupt specified\n");
	}

	return 0;
}

late_initcall(msm_pmic_restart_init);

static int __init msm_restart_init(void)
{
#ifdef CONFIG_MSM_DLOAD_MODE
	atomic_notifier_chain_register(&panic_notifier_list, &panic_blk);
	dload_mode_addr = MSM_IMEM_BASE + DLOAD_MODE_ADDR;
	emergency_dload_mode_addr = MSM_IMEM_BASE +
		EMERGENCY_DLOAD_MODE_ADDR;
#ifdef CONFIG_HUAWEI_DEBUG_MODE		
	if(strstr(saved_command_line,"huawei_debug_mode=1")!=NULL
	    || strstr(saved_command_line,"emcno=1")!=NULL)
	{
		download_mode = 1;
	}
#endif
	set_dload_mode(download_mode);
#endif
	msm_tmr0_base = msm_timer_get_timer0_base();
	restart_reason = MSM_IMEM_BASE + RESTART_REASON_ADDR;
	pm_power_off = msm_power_off;

	if (scm_is_call_available(SCM_SVC_PWR, SCM_IO_DISABLE_PMIC_ARBITER) > 0)
		scm_pmic_arbiter_disable_supported = true;

	return 0;
}
early_initcall(msm_restart_init);
