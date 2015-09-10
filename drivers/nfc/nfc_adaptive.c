/*
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
   for more details.


   Copyright (C) 2011-2013  Huawei Corporation
*/
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#define NFC_TYPE_UNKNOWN "Unknown"

typedef enum
{
    NFC_FEATURE_UNKNOWN = 0x0,
    NFC_DEV_TYPE = 0x1,
    NFC_CHIP_TYPE = 0x2,
    NFC_FEATURE_MAX = 0xffff
}nfc_feature;

struct proc_dir_entry    *nfc_dir;

/**
 * Get nfc feature type.
 * @param feature_type.
 * @return string of feature type.
 */
const char *get_nfc_feature_type(nfc_feature feature_type)
{  
    int feature_type_len;
    const char *nfc_feature_type = NFC_TYPE_UNKNOWN;
    struct device_node *dp = NULL;
	
    dp = of_find_node_by_path("/huawei_nfc_info");
    if(!of_device_is_available(dp))
    {
          printk("node huawei_nfc_info is not available!\n");
          return NFC_TYPE_UNKNOWN;
    }
	
    if(feature_type == NFC_DEV_TYPE)
    {
          /* get nfc dev type from the device feature configuration (.dtsi file) */
          nfc_feature_type = of_get_property(dp,"nfc,dev-type", &feature_type_len);
          if(NULL == nfc_feature_type)
          {
               printk("NFC get dev type fail.\n");
               return NFC_TYPE_UNKNOWN;
          }
    }
    else if(feature_type == NFC_CHIP_TYPE)
    {
          /* get nfc chip type from the device feature configuration (.dtsi file) */
          nfc_feature_type = of_get_property(dp,"nfc,chip-type", &feature_type_len);
          if(NULL == nfc_feature_type)
          {
              printk("NFC get chip type fail.\n");
              return NFC_TYPE_UNKNOWN;
          }
    }
    else
    {
          printk("NFC feature type invalid.\n");
          return NFC_TYPE_UNKNOWN;
    }

	    
    return nfc_feature_type;
}

/**
 * Read the data via the proc interface.
 * @param page Buffer for writing data.
 * @param start Not used.
 * @param offset Not used.
 * @param count Not used.
 * @param eof Whether or not there is more data to be read.
 * @param data Not used.
 * @return The number of bytes written.
 */
static int nfc_read_proc_device_type(char *page, char **start, off_t offset,
                    int count, int *eof, void *data)
{
    const char *nfc_device_type = NULL;
    
    *eof = 1;
	
    nfc_device_type = get_nfc_feature_type(NFC_DEV_TYPE);

    return snprintf(page, strlen(nfc_device_type)+1,"%s", nfc_device_type);

}

/**
 * Read the data via the proc interface.
 * @param page Buffer for writing data.
 * @param start Not used.
 * @param offset Not used.
 * @param count Not used.
 * @param eof Whether or not there is more data to be read.
 * @param data Not used.
 * @return The number of bytes written.
 */
static int nfc_read_proc_chip_type(char *page, char **start, off_t offset,
                    int count, int *eof, void *data)
{
    const char *nfc_chip_type = NULL;
    
    *eof = 1;
	
    nfc_chip_type = get_nfc_feature_type(NFC_CHIP_TYPE);

    return snprintf(page, strlen(nfc_chip_type)+1,"%s", nfc_chip_type);

}

/**
 * Initializes the module.
 * @return On success, 0. On error, -1
 *
 */
static int __init nfc_adaptive_init(void)
{
    int retval = 0;
    struct proc_dir_entry *ent = NULL;

    printk("nfc_adaptive_init start\n");

    /* create device_feature directory for wifi chip info */
    nfc_dir = proc_mkdir("nfc_feature", NULL);
    if (NULL == nfc_dir)
    {
        printk("Unable to create /proc/nfc_feature directory");
        retval =  -ENOMEM;
        goto fail;
    }

    /* Creating read/write "devtype" entry*/
    ent = create_proc_entry("devtype", 0, nfc_dir);
    if (NULL == ent) 
    {
        printk("Unable to create /proc/nfc_feature/devtype entry");
        retval = -ENOMEM;
        goto fail;
    }

    ent->read_proc = nfc_read_proc_device_type;

   /* Creating read/write "chiptype" entry*/
    ent = create_proc_entry("chiptype", 0, nfc_dir);
    if (NULL == ent) 
    {
        printk("Unable to create /proc/nfc_feature/chiptype entry");
        retval = -ENOMEM;
        goto fail;
    }

    ent->read_proc = nfc_read_proc_chip_type;


    printk("nfc_adaptive_init sucess!\n");
  
    return 0;

fail:
    remove_proc_entry("chiptype", nfc_dir);
    remove_proc_entry("devtype", nfc_dir);
    remove_proc_entry("nfc_feature", 0);

    return retval;
}


/**
 * Cleans up the module.
 */
static void __exit nfc_adaptive_exit(void)
{    
    remove_proc_entry("chiptype", nfc_dir);
    remove_proc_entry("devtype", nfc_dir);
    remove_proc_entry("nfc_feature", 0);
}



module_init(nfc_adaptive_init);
module_exit(nfc_adaptive_exit);

MODULE_DESCRIPTION("NFC Adaptive");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
