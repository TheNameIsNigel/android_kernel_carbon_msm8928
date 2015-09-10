/*check sunny and foxconn module*/
/* Copyright (c) 2013, The Linux Foundation. All rights reserved.
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
#include <mach/gpiomux.h>
#include "msm_sensor.h"
#include "msm_sd.h"
#include "camera.h"
#include "msm_cci.h"
#include "msm_camera_io_util.h"
#include "msm_camera_i2c_mux.h"
#include <mach/rpm-regulator.h>
#include <mach/rpm-regulator-smd.h>
#include <linux/regulator/consumer.h>

#include <misc/app_info.h>
#include "./msm.h"
#include "./actuator/msm_actuator.h"

#undef CDBG
//#define IMX214_DEBUG
#ifdef IMX214_DEBUG
#define CDBG(fmt, args...) pr_err(fmt, ##args)
#else
#define CDBG(fmt, args...) do { } while (0)
#endif

#define IMX214_SENSOR_NAME "hw_imx214"

DEFINE_MSM_MUTEX(imx214_mut);

static struct msm_sensor_ctrl_t imx214_s_ctrl;
#define IMX214_SUNNY_MODULE 0 //cam gpio pull low
#define IMX214_FOXCONN_MODULE 1 //cam gpio pull high
#define MAIN_CAM_ID_GPIO 33

static struct msm_sensor_power_setting imx214_power_setting[] = {
     {
		.seq_type = SENSOR_VREG,
		.seq_val = CAM_VIO,
		.config_val = 0,
		.delay = 5,
	},
	{
		.seq_type = SENSOR_VREG,
		.seq_val = CAM_VANA,
		.config_val = 0,
		.delay = 5,
	},
	//change power on sequence and add power down sequence
	{
		.seq_type = SENSOR_VREG,
		.seq_val = CAM_VDIG,
		.config_val = 0,
		.delay = 10,
	},
	{
		.seq_type = SENSOR_CLK,
		.seq_val = SENSOR_CAM_MCLK,
		.config_val = MSM_SENSOR_MCLK_24HZ,
		.delay = 10,
	},

	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_RESET,
		.config_val = GPIO_OUT_HIGH,
		.delay = 10,
	},
	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_AF_PWDM,
		.config_val = GPIO_OUT_HIGH,
		.delay = 5,
	},
	{
		.seq_type = SENSOR_I2C_MUX,
		.seq_val = 0,
		.config_val = 0,
		.delay = 0,
	},
};

static struct msm_sensor_power_setting imx214_power_down_setting[] = {
	{
		.seq_type = SENSOR_I2C_MUX,
		.seq_val = 0,
		.config_val = 0,
		.delay = 1,
	},
	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_AF_PWDM,
		.config_val = GPIO_OUT_HIGH,
		.delay = 5,
	},
	{
		.seq_type = SENSOR_CLK,
		.seq_val = SENSOR_CAM_MCLK,
		.config_val = MSM_SENSOR_MCLK_24HZ,
		.delay = 10,
	},
	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_RESET,
		.config_val = GPIO_OUT_HIGH,
		.delay = 5,
	},
	{
		.seq_type = SENSOR_VREG,
		.seq_val = CAM_VDIG,
		.config_val = 0,
		.delay = 10,
	},
	{
		.seq_type = SENSOR_VREG,
		.seq_val = CAM_VANA,
		.config_val = 0,
		.delay = 1,
	},
     {
		.seq_type = SENSOR_VREG,
		.seq_val = CAM_VIO,
		.config_val = 0,
		.delay = 1,
	},
};

static struct v4l2_subdev_info imx214_subdev_info[] = {
	{
		.code   = V4L2_MBUS_FMT_SBGGR10_1X10,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.fmt    = 1,
		.order    = 0,
	},
};

static const struct i2c_device_id imx214_i2c_id[] = {
	{IMX214_SENSOR_NAME, (kernel_ulong_t)&imx214_s_ctrl},
	{ }
};
static int32_t msm_imx214_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	return msm_sensor_i2c_probe(client, id, &imx214_s_ctrl);
}

static struct i2c_driver imx214_i2c_driver = {
	.id_table = imx214_i2c_id,
	.probe  = msm_imx214_i2c_probe,
	.driver = {
		.name = IMX214_SENSOR_NAME,
	},
};

static struct msm_camera_i2c_client imx214_sensor_i2c_client = {
	.addr_type = MSM_CAMERA_I2C_WORD_ADDR,
};

static const struct of_device_id imx214_dt_match[] = {
	{.compatible = "qcom,hw_imx214", .data = &imx214_s_ctrl},
	{}
};

MODULE_DEVICE_TABLE(of, imx214_dt_match);

static struct platform_driver imx214_platform_driver = {
	.driver = {
		.name = "qcom,hw_imx214",
		.owner = THIS_MODULE,
		.of_match_table = imx214_dt_match,
	},
};

static int32_t imx214_platform_probe(struct platform_device *pdev)
{
	int32_t rc = 0;
	const struct of_device_id *match;
	
	pr_err("%s: %d\n", __func__, __LINE__);
	match = of_match_device(imx214_dt_match, &pdev->dev);
	 if(!match) 
 	{
		pr_err("%s:var match equal null !\n",__func__);
		return 0;
 	}
	rc = msm_sensor_platform_probe(pdev, match->data);
	
	return rc;
}

static int __init imx214_init_module(void)
{
	int32_t rc = 0;

	pr_info("%s:%d\n", __func__, __LINE__);
	rc = platform_driver_probe(&imx214_platform_driver,
		imx214_platform_probe);
	if (!rc)
		return rc;
	pr_err("%s:%d rc %d\n", __func__, __LINE__, rc);
	
	return i2c_add_driver(&imx214_i2c_driver);
}

static void __exit imx214_exit_module(void)
{
	pr_info("%s:%d\n", __func__, __LINE__);
	if (imx214_s_ctrl.pdev) {
		msm_sensor_free_sensor_data(&imx214_s_ctrl);
		platform_driver_unregister(&imx214_platform_driver);
	} else
		i2c_del_driver(&imx214_i2c_driver);
	return;
}

/****************************************************************************
* FunctionName: imx214_match_module;
* Description : add the project name ;
***************************************************************************/
static int imx214_match_module(struct msm_sensor_ctrl_t *s_ctrl)
{
	struct v4l2_subdev *subdev_act[MAX_ACTUATOR_NUMBER] = {NULL};
	struct msm_actuator_ctrl_t *a_ctrl = NULL;
	int i=0;
	int cam_id = 0;
	cam_id = gpio_get_value(MAIN_CAM_ID_GPIO);
	pr_info("%s: cam_id=%d \n", __func__, cam_id);
	
    if(cam_id == IMX214_SUNNY_MODULE)
    {
	    /*add project name for the project menu*/
	    s_ctrl->sensordata->sensor_name = "hw_imx214_sunny";
	    strncpy(s_ctrl->sensordata->sensor_info->sensor_project_name, "23060146FA-IMX-S", strlen("23060146FA-IMX-S")+1);
        app_info_set("camera_main", "hw_imx214_sunny");
    }
    else if(cam_id == IMX214_FOXCONN_MODULE)
    {
        /*add project name for the project menu*/
	    s_ctrl->sensordata->sensor_name = "hw_imx214_foxconn";
	    strncpy(s_ctrl->sensordata->sensor_info->sensor_project_name, "23060146FA-IMX-F", strlen("23060146FA-IMX-F")+1);
        app_info_set("camera_main", "hw_imx214_foxconn");
		/*if it's foxconn module, we need to get the actuator ctrl to change af parameter to index 1*/
		msm_sd_get_actdev(subdev_act);
		for(i=0; i<MAX_ACTUATOR_NUMBER; i++)
		{
			if(NULL != subdev_act[i])
			{
				a_ctrl =  subdev_act[i]->dev_priv;
			}
			if(NULL != a_ctrl)
			{
				a_ctrl->cam_name = ACTUATOR_MAIN_CAM_1;	
			}
		}
    }
	else
	{
		pr_err("%s: cam_id read fail!\n", __func__);
		return 0;
	}
    
	pr_info("%s %d : imx214_match_module sensor_name=%s, sensor_project_name=%s \n",  __func__, __LINE__,
            s_ctrl->sensordata->sensor_name, s_ctrl->sensordata->sensor_info->sensor_project_name);
	pr_info("check module id from camera id PIN:OK \n");
	
	return 0;
}

static int hw_imx214_sensor_match_id(struct msm_sensor_ctrl_t *s_ctrl)
{
	int rc = 0;
	uint16_t chipid = 0;
	struct msm_camera_i2c_client *sensor_i2c_client;
	struct msm_camera_slave_info *slave_info;
	const char *sensor_name;

	if (!s_ctrl) {
		pr_err("%s:%d failed: %p\n",
			__func__, __LINE__, s_ctrl);
		return -EINVAL;
	}
	sensor_i2c_client = s_ctrl->sensor_i2c_client;
	slave_info = s_ctrl->sensordata->slave_info;
	sensor_name = s_ctrl->sensordata->sensor_name;

	if (!sensor_i2c_client || !slave_info || !sensor_name) {
		pr_err("%s:%d failed: %p %p %p\n",
			__func__, __LINE__, sensor_i2c_client, slave_info,
			sensor_name);
		return -EINVAL;
	}

	rc = sensor_i2c_client->i2c_func_tbl->i2c_read(
		sensor_i2c_client, slave_info->sensor_id_reg_addr,
		&chipid, MSM_CAMERA_I2C_WORD_DATA);
	if (rc < 0) {
		pr_err("%s: %s: read id failed\n", __func__, sensor_name);
		return rc;
	}

	pr_info("%s: read id: %x expected id %x:\n", __func__, chipid,
		slave_info->sensor_id);
 
	if (chipid != slave_info->sensor_id) {
		pr_err("msm_sensor_match_id chip id doesnot match\n");
		return -ENODEV;
	}
	return rc;
}


static struct msm_sensor_fn_t imx214_sensor_func_tbl = {
	.sensor_config = msm_sensor_config,
	.sensor_power_up = msm_sensor_power_up,
	.sensor_power_down = msm_sensor_power_down,
	.sensor_match_id = hw_imx214_sensor_match_id,//msm_sensor_match_id,
#ifdef CONFIG_HUAWEI_KERNEL_CAMERA	
	.sensor_match_module = imx214_match_module,
#endif
};

static struct msm_sensor_ctrl_t imx214_s_ctrl = {
	.sensor_i2c_client = &imx214_sensor_i2c_client,
	.power_setting_array.power_setting = imx214_power_setting,
	.power_setting_array.size = ARRAY_SIZE(imx214_power_setting),
	.power_setting_array.power_down_setting = imx214_power_down_setting,
	.power_setting_array.size_down = ARRAY_SIZE(imx214_power_down_setting),
	.msm_sensor_mutex = &imx214_mut,
	.sensor_v4l2_subdev_info = imx214_subdev_info,
	.sensor_v4l2_subdev_info_size = ARRAY_SIZE(imx214_subdev_info),
	.func_tbl = &imx214_sensor_func_tbl,

};

module_init(imx214_init_module);
module_exit(imx214_exit_module);
MODULE_DESCRIPTION("Sony 13M Bayer sensor");
MODULE_LICENSE("GPL v2");
