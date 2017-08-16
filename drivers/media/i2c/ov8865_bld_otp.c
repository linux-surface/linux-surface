/*
 * Support for OmniVision ov8865 8MP camera sensor.
 *
 * Copyright (c) 2014 Intel Corporation. All Rights Reserved.
 *
 * ov8865 Data format conversion
 *
 * include AWB&LSC of light source table and AF table 
 *
 */

#include "ov8865_bld_otp.h"

#ifdef __KERNEL__
static u32 ov8865_otp_save(const u8 *databuf, u32 data_size, const u8 *filp_name)
{
	struct file *fp=NULL;
	mm_segment_t fs;
	loff_t pos;

	fp=filp_open(filp_name,O_CREAT|O_RDWR,0644);
	if(IS_ERR(fp))
		return -1;

	fs = get_fs();
	set_fs(KERNEL_DS);
	pos = 0;
	vfs_write(fp, databuf, data_size, &pos);
	set_fs(fs);

	filp_close(fp,NULL);

	return 0;
}
static int
ov8865_read_otp(struct i2c_client *client, u16 len, u16 reg, u8 *val)
{
	struct i2c_msg msg[2];
	u16 data[OV8865_SHORT_MAX];
	int err;

	if (!client->adapter) {
		v4l2_err(client, "%s error, no client->adapter\n", __func__);
		return -ENODEV;
	}

	memset(msg, 0, sizeof(msg));
	memset(data, 0, sizeof(data));

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = I2C_MSG_LENGTH;
	msg[0].buf = (u8 *)data;
	/* high byte goes first */
	data[0] = cpu_to_be16(reg);

	msg[1].addr = client->addr;
	msg[1].len = len;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = (u8 *)val;

	err = i2c_transfer(client->adapter, msg, 2);
	if (err < 0)
		goto error;

	return 0;

error:
	dev_err(&client->dev, "read from offset 0x%x error %d", reg, err);
	return err;
}

static int ov8865_otp_read(struct i2c_client *client, u8 *ov8865_data_ptr, u32 *ov8865_size)
{
	int ret;
	int address_start = SNR_OTP_START;
	int address_end = SNR_OTP_END;

	printk("ov8865 OTP debug\n");

	// read otp into buffer
	ov8865_write_reg(client, OV8865_8BIT, 0x3d84, 0xc0); // program disable, manual mode
	//partial mode OTP write start address
	ov8865_write_reg(client, OV8865_8BIT, 0x3d88, (address_start>>8));
	ov8865_write_reg(client, OV8865_8BIT, 0x3d89, (address_start & 0xff));
	// partial mode OTP write end address
	ov8865_write_reg(client, OV8865_8BIT, 0x3d8A, (address_end>>8));
	ov8865_write_reg(client, OV8865_8BIT, 0x3d8B, (address_end & 0xff));
	ov8865_write_reg(client, OV8865_8BIT, 0x3d81, 0x01); // read otp
	usleep_range(10000, 10100);


	ret = ov8865_read_otp(client, address_end - address_start + 1, address_start, ov8865_data_ptr);
	if (ret) {
		OTP_LOG("ov8865 read otp failed");
		return -1;
	}

	ov8865_write_reg(client, OV8865_8BIT, 0x3d81, 0x00); //stop read otp
	*ov8865_size = address_end - address_start + 1;
	printk("ov8865 OTP read done\n");
	return 0;
}
#endif

static void dump_ov8865_module_info(const u8 *ov8865_data_ptr) {
	module_info_t *info;

	info = (module_info_t *) &ov8865_data_ptr[ov8865_data_group_pos.module_info];

	OTP_LOG("module information:\n");
	OTP_LOG("mid:%d calibration vsersion:%d \n", info->mid, info->calibration_version);
	OTP_LOG("year:%d month:%d day:%d\n", info->year, info->month, info->day);
	OTP_LOG("IDs of sensor:%d lens:%d vcm:%d ic:%d IR/BG:%d\n", info->sensor_id,
			info->lens_id, info->vcm_id, info->driver_ic_id, info->IR_BG_id);
	OTP_LOG("color temp:%d AF/FF:%d light:%d\n", info->color_ctc, info->af_flag, info->light_type);
}

static int check_ov8865_calibration_ver13(const u8 *ov8865_data_ptr)
{
	unsigned char major_version, minor_version;

	major_version = ov8865_data_ptr[ov8865_data_group_pos.awb_lsc_light_source_one];
	minor_version = ov8865_data_ptr[ov8865_data_group_pos.awb_lsc_light_source_one + 1];

	OTP_LOG("calibration major:minor -- %d : %d\n", major_version, minor_version);
	if ((major_version == 1) && (minor_version == 3)) {
		OTP_LOG("This is ver1.3 calibration data\n");
		return 1;
	}

	return 0;

}

static int ov8865_otp_trans(const unsigned char *ov8865_data_ptr, const int ov8865_size, unsigned char *otp_data_ptr, int *otp_size)
{
	int ret = 0;

	if(ov8865_data_ptr == NULL || ov8865_size == 0)
		return ENULL_OV8865;

	if(otp_data_ptr == NULL || otp_size == 0)
		return ENULL_OTP;

	/* initialize ov8865_data_group_pos */
	ov8865_data_group_pos.module_info = 0;
	ov8865_data_group_pos.awb_lsc_light_source_one = 0;
	ov8865_data_group_pos.awb_lsc_light_source_two = 0;
	ov8865_data_group_pos.af = 0;
	/* check the whole ov8865 data whether is correction */
	ret = ov8865_data_check(ov8865_data_ptr,ov8865_size);
	if(ret)
		return ret;
	/* initialize otp_data_ptr to ready for copy */

	memset(otp_data_ptr, 0, *otp_size);
	ret = otp_data_copy(ov8865_data_ptr,ov8865_size, otp_data_ptr, otp_size);
	if(ret)
		return ret;

	return ret;
}
/* lsc address definition*/
static ov8865_group_address_t lsc_awb_light_src_one_group_addr[2] = {
	{0x20, 0x9F},
	{0xBF, 0x9F}
};

static ov8865_group_address_t lsc_awb_light_src_two_group_addr[2] = {
	{0x15F, 0x9F},
	{0x1FE, 0x9F}
};
/*
8865 CRC_16 Checksum 计算以下三组数据总和：
1. 5000色温下LSC和AWB 有效数据的一组: 第一组：0x7030~0x70CB  第二组：0x70CF~0x716A 
2. 3000色温下LSC和AWB 有效数据的一组: 第一组：0x7172~0x7173 0x7176~0x720A  第二组：0x7211~0x7212 0x7215~0x72A9
3. AF有效数据的一组：第一组：0x72AC~0x72B6  第二组：0x72B8~0x72C2 

*/
static int check_all_calibration_data(const u8 *ov8865_data_ptr, u32 offset, u32 len)
{
	u32 i = 0;
	u32 sum = 0, otp_sum;
	u8 tmp[DATA_BUF_SIZE];
	u32 tmp_len=0;

	memset(tmp,0,DATA_BUF_SIZE);
	/* LSC information of light source 1 */
	memcpy(tmp+tmp_len, ov8865_data_ptr+ov8865_data_group_pos.awb_lsc_light_source_one,
		OV8865_AWB_LSC_LIGHT_SOURCE_GROUP_LEN-3);
	tmp_len += OV8865_AWB_LSC_LIGHT_SOURCE_GROUP_LEN-3;

	/* LSC information of light source 2 */
	memcpy(tmp+tmp_len, ov8865_data_ptr+ov8865_data_group_pos.awb_lsc_light_source_two + 3, 2);
	tmp_len += 2;

		/* LSC information of light source 2 */
	memcpy(tmp+tmp_len, ov8865_data_ptr+ov8865_data_group_pos.awb_lsc_light_source_two+ 7, OV8865_AWB_LSC_LIGHT_SOURCE_GROUP_LEN - 10);
	tmp_len += OV8865_AWB_LSC_LIGHT_SOURCE_GROUP_LEN - 10;

	/* AWB information of light source 1 */
	memcpy(tmp+tmp_len, ov8865_data_ptr+ov8865_data_group_pos.af, OV8865_AF_GROUP_LEN - 1);
	tmp_len += OV8865_AF_GROUP_LEN - 1;

	/* CRC_16 checksum */
	for(i=0,sum=0;i<tmp_len;i++)  {
		sum += tmp[i];
		//OTP_LOG("data: %2x sum:%4x NO:%3d \n", tmp[i], sum, i); 
	}

	sum = sum & 0xffff;
	otp_sum = (ov8865_data_ptr[offset+len-2] << 8) + ov8865_data_ptr[offset+len-3];
	OTP_LOG("sum:%x otp_sum:%x sum_read1:%x sum_read2:%x\n", sum, otp_sum, ov8865_data_ptr[offset+len-2], ov8865_data_ptr[offset+len-3]);
	if(sum != otp_sum) {
		OTP_LOG("%s %d overall checksum failed\n", __func__, __LINE__);
		return -1;
	}

	OTP_LOG("all data sum check successfully!\n");
	return 0;
}

static int ov8865_data_check(const unsigned char *ov8865_data_ptr, int ov8865_size)
{
	int offset = 0;
	int ret = 0;
	/* check ov8865 module information table */
	offset = OV8865_MODULE_INFORMATION_OFFSET;
	ret = check_ov8865_Module_Information(ov8865_data_ptr,offset);
	if(ret == -1)
		return ret;
	else
		ov8865_data_group_pos.module_info = ret;/* record the valid position */
	dump_ov8865_module_info(ov8865_data_ptr);

	/* check ov8865 AWB and LSC of light source 1 table */
	offset = OV8865_AWB_LSC_LIGHT_SOURCE_ONE_OFFSET;
	ret = check_ov8865_AWB_LSC_light_source(ov8865_data_ptr,offset);
	if(ret == -1)
		return ret;
	else
		ov8865_data_group_pos.awb_lsc_light_source_one = ret;
	/* check ov8865 AWB and LSC of light source 2 table */
	offset = OV8865_AWB_LSC_LIGHT_SOURCE_TWO_OFFSET;
	ret = check_ov8865_AWB_LSC_light_source(ov8865_data_ptr,offset);
	if(ret == -1)
		return ret;
	else
		ov8865_data_group_pos.awb_lsc_light_source_two = ret;
	/* check ov8865 AF table */
	if(check_ov8865_calibration_ver13(ov8865_data_ptr) == 1) {
		offset = OV8865_AF_OFFSET_VER13;
		OTP_LOG("This is ver1.3 calibration AF flag offset:%x\n", offset);
	} else {
		offset = OV8865_AF_OFFSET_NON_VER13;
		OTP_LOG("This is not ver1.3 calibration AF flag offset:%x\n", offset);
	}
	ret = check_ov8865_AF(ov8865_data_ptr, offset);
	if(ret == -1)
		return ret;
	else
		ov8865_data_group_pos.af = ret;

	ret = check_all_calibration_data(ov8865_data_ptr, ov8865_data_group_pos.awb_lsc_light_source_two, lsc_awb_light_src_two_group_addr[0].len);
	if (ret == -1)
		return ret;

	return 0;
}

static ov8865_group_address_t module_info_group_addr[2] = {
	{0x01, 0x0F},
	{0x10, 0x0F},
};
static int check_ov8865_Module_Information(const unsigned char *ov8865_data_ptr, int offset)
{
    unsigned char flag = ov8865_data_ptr[offset];
	int i;
	int ret;

	OTP_LOG("%s %d group check offset:%x flag:%x\n", __func__, __LINE__, offset, flag);
	for (i = 1; i >= 0; i--) {
		if ((flag >> ((3 - i) << 1)) & 0x01) break;
	}

	if (i < 0) {
		OTP_LOG("no valid module info found\n");
		return -1;
	}
	OTP_LOG("Module info found:%d start:0x%x len:0x%x\n", i,
				module_info_group_addr[i].start,
				module_info_group_addr[i].len);

	ret = check_ov8865_Group(ov8865_data_ptr, module_info_group_addr[i].start, module_info_group_addr[i].len);
	if (ret) {
		OTP_LOG("module info sum check failed\n");
		return -1;
	}

	return module_info_group_addr[i].start;
}

static int check_ov8865_AWB_LSC_light_source(const unsigned char *ov8865_data_ptr, int offset)
{
    unsigned char flag = ov8865_data_ptr[offset];
	ov8865_group_address_t *group_addr;
	int i;
	int ret;

	OTP_LOG("%s %d offset:%x flag:%x\n", __func__, __LINE__, offset, flag);

	if (offset == OV8865_AWB_LSC_LIGHT_SOURCE_TWO_OFFSET) {
		group_addr = lsc_awb_light_src_two_group_addr;
	} else {
		group_addr = lsc_awb_light_src_one_group_addr;
	}

	for (i = 1; i >= 0; i--) {
		if ((flag >> ((3 - i) << 1)) & 0x01) break;
	}

	if (i < 0) {
		OTP_LOG("no valid awb info found\n");
		return -1;
	}

	OTP_LOG("awb found:%d start:0x%x len:0x%x\n", i,
				group_addr[i].start,
				group_addr[i].len);
	ret = check_ov8865_Group(ov8865_data_ptr, group_addr[i].start, group_addr[i].len);
	if (ret) {
		OTP_LOG("awb sum check failed\n");
		return -1;
	}

	return group_addr[i].start;
}

static ov8865_group_address_t af_info_group_addr_v13[2] = {
	{0x29C, 0x0C},
	{0x2A8, 0x0C},
};

static ov8865_group_address_t af_info_group_addr_non_v13[2] = {
	{0x29E, 0x0C},
	{0x2AA, 0x0C},
};

static int check_ov8865_AF(const unsigned char *ov8865_data_ptr, int offset)
{
    unsigned char flag = ov8865_data_ptr[offset];
	int i;
	int ret;

	ov8865_group_address_t * af_info_group_addr;

	if (check_ov8865_calibration_ver13(ov8865_data_ptr) == 1) {
		af_info_group_addr = &af_info_group_addr_v13[0];
		OTP_LOG("This is ver1.3 calibration AF Group 0 start:%x\n", af_info_group_addr[0].start);
	} else {
		af_info_group_addr = &af_info_group_addr_non_v13[0];
		OTP_LOG("This is not ver1.3 calibration AF Group 0 start:%x\n", af_info_group_addr[0].start);
	}

	OTP_LOG("%s %d group check offset:%x flag:%x\n", __func__, __LINE__, offset, flag);
	for (i = 1; i >= 0; i--) {
		if ((flag >> ((3 - i) << 1)) & 0x01) break;
	}

	if (i < 0) {
		OTP_LOG("no valid AF info found\n");
		return -1;
	}
	OTP_LOG("AF info found:%d start:0x%x len:0x%x\n", i,
				af_info_group_addr[i].start,
				af_info_group_addr[i].len);

	ret = check_ov8865_Group(ov8865_data_ptr, af_info_group_addr[i].start, af_info_group_addr[i].len);
	if (ret) {
		OTP_LOG("module info sum check failed\n");
		return -1;
	}

	return af_info_group_addr[i].start;
}

static int check_ov8865_Group(const unsigned char *ov8865_data_ptr, int offset, int len)
{
	int i = 0;
	u32 sum = 0;
	/* calculated(sum) from all bytes before checksum */
	for(i=0;i<len-1;i++) {
		sum += ov8865_data_ptr[offset+i];
		//OTP_LOG("i:%d data:%x sum:%x\n", i, ov8865_data_ptr[offset+i], sum);
	}

	sum = sum % 255 + 1;

	if(sum != ov8865_data_ptr[offset+i]) {
		OTP_LOG("sum:%x needed:%x\n", sum, ov8865_data_ptr[offset+i]);
		return -1;
	}

	return 0;
}

static uint16_t get_nvm_checksum(const uint8_t *a_data_ptr, int32_t a_size)
{
	uint16_t crc = 0;
	int32_t i;

	for (i = 0; i < a_size; i++)
	{
		uint8_t index = crc ^ a_data_ptr[i];
		crc = (crc >> 8) ^ crc16table[index];
	}

	return crc;
}

static int otp_data_copy(const unsigned char *ov8865_data_ptr, const int ov8865_size, unsigned char *otp_data_ptr, int *otp_size)
{
	int ret = 0;

	*otp_size = 0;
	ret = otp_Module_Information_copy(ov8865_data_ptr,ov8865_size,otp_data_ptr,otp_size); 
	if(ret == -1)
	   return ret;

	ret = otp_AF_copy(ov8865_data_ptr,ov8865_size,otp_data_ptr,otp_size);
	if(ret == -1)
	   return ret;

	ret = otp_AWB_LSC_light_copy(ov8865_data_ptr,ov8865_size,otp_data_ptr,otp_size);
	if(ret == -1)
	   return ret;

	ret = otp_crc(otp_data_ptr,otp_size);
	if(ret == -1)
	   return ret;

	return 0;
}

static int otp_AWB_LSC_light_copy(const unsigned char *ov8865_data_ptr, const int ov8865_size, unsigned char *otp_data_ptr, int *otp_size)
{
	int ret = 0;
	/* AWB and LSC of light source 1 */

	ret = otp_AWB_LSC_light_source_copy(ov8865_data_ptr, otp_data_ptr,otp_size);
	if(ret)
		return -1;

	return 0;
}

static int otp_AWB_LSC_light_source_copy(const unsigned char *ov8865_data_ptr, unsigned char *otp_data_ptr, int *otp_size)
{
	u32 otp_offset = 0;
	u32 ov8865_offset = 0;


	/* otp_data[0]:major_version and; otp_data[1]:mino_version; otp_data[2]:n_lights */
	otp_offset = 0;
	ov8865_offset = ov8865_data_group_pos.awb_lsc_light_source_one;
	memcpy(otp_data_ptr+otp_offset,ov8865_data_ptr+ov8865_offset,2);

    /* focus copy */
	otp_offset += 2;
	ov8865_offset = ov8865_data_group_pos.af;
	memcpy(otp_data_ptr+otp_offset, ov8865_data_ptr+ov8865_offset, 11);

	/* otp_data[2]:n_lights */
	ov8865_offset = ov8865_data_group_pos.awb_lsc_light_source_one;
	otp_offset += 11;
	ov8865_offset += 2;
	memcpy(otp_data_ptr+otp_offset,ov8865_data_ptr+ov8865_offset,1);
	/* otp_data[3]:cie_x1 of light source 1; otp_data[4]:cie_x1 of light source 2*/
	/* for hack*/
	otp_offset += 1;
	ov8865_offset += 1;
	memcpy(otp_data_ptr+otp_offset,ov8865_data_ptr+ov8865_offset,1);

	otp_offset += 1;
	ov8865_offset = ov8865_data_group_pos.awb_lsc_light_source_two+3;
	memcpy(otp_data_ptr+otp_offset,ov8865_data_ptr+ov8865_offset,1);

	/* otp_data[5]:cie_y1 of light source 1; otp_data[6]:cie_y1 of light source 2*/
	otp_offset += 1;
	ov8865_offset = ov8865_data_group_pos.awb_lsc_light_source_one+4;
	memcpy(otp_data_ptr+otp_offset,ov8865_data_ptr+ov8865_offset,1);

	otp_offset += 1;
	ov8865_offset = ov8865_data_group_pos.awb_lsc_light_source_two+4;
	memcpy(otp_data_ptr+otp_offset,ov8865_data_ptr+ov8865_offset,1);

	/* otp_data[7]:LSC_width; otp_data[8]: LSC_height */
	/* LSC of light source 1:lsc_fraq_bits; LSCgrid */
	otp_offset += 1;
	ov8865_offset = ov8865_data_group_pos.awb_lsc_light_source_one + 5;
	memcpy(otp_data_ptr+otp_offset,ov8865_data_ptr+ov8865_offset,OV8865_LSC_LIGHT_SOURCE_GROUP_LEN-1);

	/* LSC of light source 2:lsc_fraq_bits; LSCgrid */
	otp_offset += OV8865_LSC_LIGHT_SOURCE_GROUP_LEN-1;
	ov8865_offset = ov8865_data_group_pos.awb_lsc_light_source_two + 5 +2;
	memcpy(otp_data_ptr+otp_offset,ov8865_data_ptr+ov8865_offset,OV8865_LSC_LIGHT_SOURCE_GROUP_LEN-3);

	/* AWB of light source 1&2 */
	/* AWB_ch1_source_1 */
	otp_offset += OV8865_LSC_LIGHT_SOURCE_GROUP_LEN-3;
	ov8865_offset = ov8865_data_group_pos.awb_lsc_light_source_one+5 + OV8865_LSC_LIGHT_SOURCE_GROUP_LEN-1;
	memcpy(otp_data_ptr+otp_offset,ov8865_data_ptr+ov8865_offset,2);

	/* AWB_ch1_source_2 */
	otp_offset += 2;
	ov8865_offset = ov8865_data_group_pos.awb_lsc_light_source_two+5 +OV8865_LSC_LIGHT_SOURCE_GROUP_LEN-1;
	memcpy(otp_data_ptr+otp_offset,ov8865_data_ptr+ov8865_offset,2);

	/* AWB_ch2_source_1 */
	otp_offset += 2;
	ov8865_offset = ov8865_data_group_pos.awb_lsc_light_source_one+7+ OV8865_LSC_LIGHT_SOURCE_GROUP_LEN-1;
	memcpy(otp_data_ptr+otp_offset,ov8865_data_ptr+ov8865_offset,2);

	/* AWB_ch2_source_2 */
	otp_offset += 2;
	ov8865_offset = ov8865_data_group_pos.awb_lsc_light_source_two+7+ OV8865_LSC_LIGHT_SOURCE_GROUP_LEN-1;
	memcpy(otp_data_ptr+otp_offset,ov8865_data_ptr+ov8865_offset,2);

	/* AWB_ch3_source_1 */
	otp_offset += 2;
	ov8865_offset = ov8865_data_group_pos.awb_lsc_light_source_one+9+ OV8865_LSC_LIGHT_SOURCE_GROUP_LEN-1;
	memcpy(otp_data_ptr+otp_offset,ov8865_data_ptr+ov8865_offset,2);

	/* AWB_ch3_source_2 */
	otp_offset += 2;
	ov8865_offset = ov8865_data_group_pos.awb_lsc_light_source_two+9+ OV8865_LSC_LIGHT_SOURCE_GROUP_LEN-1;
	memcpy(otp_data_ptr+otp_offset,ov8865_data_ptr+ov8865_offset,2);

	/* AWB_ch4_source_1 */
	otp_offset += 2;
	ov8865_offset = ov8865_data_group_pos.awb_lsc_light_source_one+0x0B+ OV8865_LSC_LIGHT_SOURCE_GROUP_LEN-1;
	memcpy(otp_data_ptr+otp_offset,ov8865_data_ptr+ov8865_offset,2);

	/* AWB_ch4_source_2 */
	otp_offset += 2;
	ov8865_offset = ov8865_data_group_pos.awb_lsc_light_source_two+0x0B+ OV8865_LSC_LIGHT_SOURCE_GROUP_LEN-1;
	memcpy(otp_data_ptr+otp_offset,ov8865_data_ptr+ov8865_offset,2);

	*otp_size = otp_offset + 2;

	return 0;
}

static int otp_AF_copy(const unsigned char *ov8865_data_ptr, const int ov8865_size, unsigned char *otp_data_ptr, int *otp_size)
{
	/* offset 2bytes in otp_data */
	int otp_offset = 2;
	int ov8865_offset = ov8865_data_group_pos.af;
	/* OV8865_AF_GROUP_LEN include ov8865_data checksum(1byte) */
	memcpy(otp_data_ptr+otp_offset,ov8865_data_ptr+ov8865_offset,OV8865_AF_GROUP_LEN-1);
	/* length of otp_data */
	*otp_size += OV8865_AF_GROUP_LEN-1;

	return 0;
}

static int otp_crc(unsigned char *otp_data_ptr, int *otp_size)
{
	uint16_t crc = get_nvm_checksum((uint8_t*)otp_data_ptr,*otp_size);
	unsigned char crc_buf[2];

	memset(crc_buf, 0, 2);
	/* Little endian */
	crc_buf[0] = (unsigned char)crc;
	crc_buf[1] = (unsigned char)(crc>>8);
	memcpy(otp_data_ptr+(*otp_size),crc_buf,2);
	*otp_size += 2;

	return 0;
}

static int otp_Module_Information_copy(const unsigned char *ov8865_data_ptr, const int ov8865_size, unsigned char *otp_data_ptr, int *otp_size)
{
	/* There's nothing to copy */
	return 0;
}

