#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/vmalloc.h>
#include <linux/intel_ipts_if.h>

#include "ipts.h"
#include "ipts-resource.h"
#include "ipts-binary-spec.h"
#include "ipts-state.h"
#include "ipts-msg-handler.h"
#include "ipts-gfx.h"

#define MAX_IOCL_FILE_NAME_LEN		80
#define MAX_IOCL_FILE_PATH_LEN		256

#pragma pack(1)
typedef struct bin_data_file_info {
    u32 io_buffer_type;
    u32 flags;
    char file_name[MAX_IOCL_FILE_NAME_LEN];
} bin_data_file_info_t;

typedef struct bin_fw_info {
	char fw_name[MAX_IOCL_FILE_NAME_LEN];

	/* list of parameters to load a kernel */
	s32 vendor_output;	/* output index. -1 for no use */
	u32 num_of_data_files;
	bin_data_file_info_t data_file[];
} bin_fw_info_t;

typedef struct bin_fw_list {
	u32 num_of_fws;
	bin_fw_info_t fw_info[];
} bin_fw_list_t;
#pragma pack()

/* OpenCL kernel */
typedef struct bin_workload {
	int cmdbuf_index;
	int iobuf_input;
	int iobuf_output[MAX_NUM_OUTPUT_BUFFERS];
} bin_workload_t;

typedef struct bin_buffer {
	unsigned int handle;
	intel_ipts_mapbuffer_t *buf;
	bool no_unmap;	/* only releasing vendor kernel unmaps output buffers */
} bin_buffer_t;

typedef struct bin_alloc_info {
	bin_buffer_t *buffs;
	int num_of_allocations;
	int num_of_outputs;

	int num_of_buffers;
} bin_alloc_info_t;

typedef struct bin_guc_wq_item {
	unsigned int batch_offset;
	unsigned int size;
	char data[];
} bin_guc_wq_item_t;

typedef struct bin_kernel_info {
	bin_workload_t *wl;
	bin_alloc_info_t *alloc_info;
	bin_guc_wq_item_t *guc_wq_item;
	ipts_bin_bufid_patch_t bufid_patch;

	bool is_vendor; /* 1: vendor, 0: postprocessing */
} bin_kernel_info_t;

typedef struct bin_kernel_list {
	intel_ipts_mapbuffer_t *bufid_buf;
	int num_of_kernels;
	bin_kernel_info_t kernels[];
} bin_kernel_list_t;

typedef struct bin_parse_info {
	u8 *data;
	int size;
	int parsed;

	bin_fw_info_t *fw_info;

	/* only used by postprocessing */
	bin_kernel_info_t *vendor_kernel;
	u32 interested_vendor_output; /* interested vendor output index */
} bin_parse_info_t;

#define BDW_SURFACE_BASE_ADDRESS		0x6101000e
#define SURFACE_STATE_OFFSET_WORD		4
#define SBA_OFFSET_BYTES			16384
#define LASTSUBMITID_DEFAULT_VALUE		-1

#define IPTS_FW_PATH_FMT			"intel/ipts/%s"
#define IPTS_FW_CONFIG_FILE			"intel/ipts/ipts_fw_config.bin"

MODULE_FIRMWARE(IPTS_FW_CONFIG_FILE);

#define IPTS_INPUT_ON				((u32)1 << IPTS_INPUT)
#define IPTS_OUTPUT_ON				((u32)1 << IPTS_OUTPUT)
#define IPTS_CONFIGURATION_ON			((u32)1 << IPTS_CONFIGURATION)
#define IPTS_CALIBRATION_ON			((u32)1 << IPTS_CALIBRATION)
#define IPTS_FEATURE_ON				((u32)1 << IPTS_FEATURE)

#define	DATA_FILE_FLAG_SHARE			0x00000001
#define	DATA_FILE_FLAG_ALLOC_CONTIGUOUS		0x00000002

static int bin_read_fw(ipts_info_t *ipts, const char *fw_name,
						u8* data, int size)
{
	const struct firmware *fw = NULL;
	char fw_path[MAX_IOCL_FILE_PATH_LEN];
	int ret = 0;

	snprintf(fw_path, MAX_IOCL_FILE_PATH_LEN, IPTS_FW_PATH_FMT, fw_name);
	ret = request_firmware(&fw, fw_path, &ipts->cldev->dev);
	if (ret) {
		ipts_err(ipts, "cannot read fw %s\n", fw_path);
		return ret;
	}

	if (fw->size > size) {
		ipts_dbg(ipts, "too small buffer to contain fw data\n");
		ret = -EINVAL;
		goto rel_return;
	}

	memcpy(data, fw->data, fw->size);

rel_return:
	release_firmware(fw);

	return ret;
}


static bin_data_file_info_t* bin_get_data_file_info(bin_fw_info_t* fw_info,
							u32 io_buffer_type)
{
	int i;

	for (i = 0; i < fw_info->num_of_data_files; i++) {
		if (fw_info->data_file[i].io_buffer_type == io_buffer_type)
			break;
	}

	if (i == fw_info->num_of_data_files)
		return NULL;

	return &fw_info->data_file[i];
}

static inline bool is_shared_data(const bin_data_file_info_t *data_file)
{
	if (data_file)
		return (!!(data_file->flags & DATA_FILE_FLAG_SHARE));

	return false;
}

static inline bool is_alloc_cont_data(const bin_data_file_info_t *data_file)
{
	if (data_file)
		return (!!(data_file->flags & DATA_FILE_FLAG_ALLOC_CONTIGUOUS));

	return false;
}

static inline bool is_parsing_vendor_kernel(const bin_parse_info_t *parse_info)
{
	/* vendor_kernel == null while loading itself(vendor kernel) */
	return parse_info->vendor_kernel == NULL;
}

static int bin_read_allocation_list(ipts_info_t *ipts,
					bin_parse_info_t *parse_info,
					bin_alloc_info_t *alloc_info)
{
	ipts_bin_alloc_list_t *alloc_list;
	int alloc_idx, parallel_idx, num_of_parallels, buf_idx, num_of_buffers;
	int parsed, size;

	parsed = parse_info->parsed;
	size = parse_info->size;

	alloc_list = (ipts_bin_alloc_list_t *)&parse_info->data[parsed];

	/* validation check */
	if (sizeof(alloc_list->num) > size - parsed)
		return -EINVAL;

	/* read the number of aloocations */
	parsed += sizeof(alloc_list->num);

	/* validation check */
	if (sizeof(alloc_list->alloc[0]) * alloc_list->num > size - parsed)
		return -EINVAL;

	num_of_parallels = ipts_get_num_of_parallel_buffers(ipts);
	num_of_buffers = num_of_parallels * alloc_list->num + num_of_parallels;

	alloc_info->buffs = vmalloc(sizeof(bin_buffer_t) * num_of_buffers);
	if (alloc_info->buffs == NULL)
		return -ENOMEM;

	memset(alloc_info->buffs, 0, sizeof(bin_buffer_t) * num_of_buffers);
	for (alloc_idx = 0; alloc_idx < alloc_list->num; alloc_idx++) {
		for (parallel_idx = 0; parallel_idx < num_of_parallels;
								parallel_idx++) {
			buf_idx = alloc_idx + (parallel_idx * alloc_list->num);
			alloc_info->buffs[buf_idx].handle =
					alloc_list->alloc[alloc_idx].handle;

		}

		parsed += sizeof(alloc_list->alloc[0]);
	}

	parse_info->parsed = parsed;
	alloc_info->num_of_allocations = alloc_list->num;
	alloc_info->num_of_buffers = num_of_buffers;

	ipts_dbg(ipts, "number of allocations = %d, buffers = %d\n",
						alloc_info->num_of_allocations,
						alloc_info->num_of_buffers);

	return 0;
}

static void patch_SBA(u32 *buf_addr, u64 gpu_addr, int size)
{
	u64 *stateBase;
	u64 SBA;
	u32 inst;
	int i;

	SBA = gpu_addr + SBA_OFFSET_BYTES;

	for (i = 0; i < size/4; i++) {
		inst = buf_addr[i];
		if (inst == BDW_SURFACE_BASE_ADDRESS) {
			stateBase = (u64*)&buf_addr[i + SURFACE_STATE_OFFSET_WORD];
			*stateBase |= SBA;
			*stateBase |= 0x01; // enable
			break;
		}
	}
}

static int bin_read_cmd_buffer(ipts_info_t *ipts,
					bin_parse_info_t *parse_info,
					bin_alloc_info_t *alloc_info,
					bin_workload_t *wl)
{
	ipts_bin_cmdbuf_t *cmd;
	intel_ipts_mapbuffer_t *buf;
	int cmdbuf_idx, size, parsed, parallel_idx, num_of_parallels;

	size = parse_info->size;
	parsed = parse_info->parsed;

	cmd = (ipts_bin_cmdbuf_t *)&parse_info->data[parsed];

	if (sizeof(cmd->size) > size - parsed)
		return -EINVAL;

	parsed += sizeof(cmd->size);
	if (cmd->size > size - parsed)
		return -EINVAL;

	ipts_dbg(ipts, "cmd buf size = %d\n", cmd->size);
		
	num_of_parallels = ipts_get_num_of_parallel_buffers(ipts);
	/* command buffers are located after the other allocations */
	cmdbuf_idx = num_of_parallels * alloc_info->num_of_allocations;
	for (parallel_idx = 0; parallel_idx < num_of_parallels; parallel_idx++) {
		buf = ipts_map_buffer(ipts, cmd->size, 0);
		if (buf == NULL)
			return -ENOMEM;

		ipts_dbg(ipts, "cmd_idx[%d] = %d, g:0x%p, c:0x%p\n", parallel_idx,
					cmdbuf_idx, buf->gfx_addr, buf->cpu_addr);

		memcpy((void *)buf->cpu_addr, &(cmd->data[0]), cmd->size);
		patch_SBA(buf->cpu_addr, (u64)buf->gfx_addr, cmd->size);
		alloc_info->buffs[cmdbuf_idx].buf = buf;
		wl[parallel_idx].cmdbuf_index = cmdbuf_idx;

		cmdbuf_idx++;
	}

	parsed += cmd->size;
	parse_info->parsed = parsed;

	return 0;
}

static int bin_find_alloc(ipts_info_t *ipts,
					bin_alloc_info_t *alloc_info,
					u32 handle)
{
	int i;

	for (i = 0; i < alloc_info->num_of_allocations; i++) {
		if (alloc_info->buffs[i].handle == handle)
			return i;
	}

	return -1;
}

static intel_ipts_mapbuffer_t* bin_get_vendor_kernel_output(
						bin_parse_info_t *parse_info,
						int parallel_idx)
{
	bin_kernel_info_t *vendor = parse_info->vendor_kernel;
	bin_alloc_info_t *alloc_info;
	int buf_idx, vendor_output_idx;

	alloc_info = vendor->alloc_info;
	vendor_output_idx = parse_info->interested_vendor_output;

	if (vendor_output_idx >= alloc_info->num_of_outputs)
		return NULL;

	buf_idx = vendor->wl[parallel_idx].iobuf_output[vendor_output_idx];
	return alloc_info->buffs[buf_idx].buf;
}

static int bin_read_res_list(ipts_info_t *ipts,
					bin_parse_info_t *parse_info,
					bin_alloc_info_t *alloc_info,
					bin_workload_t *wl)
{
	ipts_bin_res_list_t *res_list;
	ipts_bin_res_t *res;
	intel_ipts_mapbuffer_t *buf;
	bin_data_file_info_t *data_file;
	u8 *bin_data;
	int i, size, parsed, parallel_idx, num_of_parallels, output_idx = -1;
	int buf_idx, num_of_alloc;
	u32 buf_size, flags, io_buf_type;
	bool initialize;
	
	parsed = parse_info->parsed;
	size = parse_info->size;
	bin_data = parse_info->data;

	res_list = (ipts_bin_res_list_t *)&parse_info->data[parsed];
	if (sizeof(res_list->num) > (size - parsed))
		return -EINVAL;
	parsed += sizeof(res_list->num);
	num_of_parallels = ipts_get_num_of_parallel_buffers(ipts);

	ipts_dbg(ipts, "number of resources %u\n", res_list->num);
	for (i = 0; i < res_list->num; i++) {
		initialize = false; 
		io_buf_type = 0;
		flags = 0;

		/* initial data */
		data_file = NULL;

		res = (ipts_bin_res_t *)(&(bin_data[parsed]));
		if (sizeof(res[0]) > (size - parsed)) {
			return -EINVAL;
		}

		ipts_dbg(ipts, "Resource(%d):handle 0x%08x type %u init %u"
				" size %u alsigned %u\n",
				i, res->handle, res->type, res->initialize,
				res->size, res->aligned_size);
                parsed += sizeof(res[0]);

		if (res->initialize) {
			if (res->size > (size - parsed)) {
				return -EINVAL;
			}
			parsed += res->size;
		}

		initialize = res->initialize;
		if (initialize && res->size > sizeof(ipts_bin_io_header_t)) {
			ipts_bin_io_header_t *io_hdr;
			io_hdr = (ipts_bin_io_header_t *)(&res->data[0]);
			if (strncmp(io_hdr->str, "INTELTOUCH", 10) == 0) {
				data_file = bin_get_data_file_info(
							parse_info->fw_info,
							(u32)io_hdr->type);
				switch (io_hdr->type) {
				case IPTS_INPUT:
					ipts_dbg(ipts, "input detected\n");
					io_buf_type = IPTS_INPUT_ON;
					flags = IPTS_BUF_FLAG_CONTIGUOUS;
					break;
				case IPTS_OUTPUT:
					ipts_dbg(ipts, "output detected\n");
					io_buf_type = IPTS_OUTPUT_ON;
					output_idx++;
					break;
				default:
					if ((u32)io_hdr->type > 31) {
						ipts_err(ipts,
							"invalid io buffer : %u\n",
							(u32)io_hdr->type);
						continue;
					}

					if (is_alloc_cont_data(data_file))
						flags = IPTS_BUF_FLAG_CONTIGUOUS;

					io_buf_type = ((u32)1 << (u32)io_hdr->type);
					ipts_dbg(ipts, "special io buffer %u\n",
								io_hdr->type);
					break;
				}

				initialize = false;
			}
		}

		num_of_alloc = alloc_info->num_of_allocations;
		buf_idx = bin_find_alloc(ipts, alloc_info, res->handle);
		if (buf_idx == -1) {
			ipts_dbg(ipts, "cannot find alloc info\n");
			return -EINVAL;
		}
		for (parallel_idx = 0; parallel_idx < num_of_parallels;
					parallel_idx++, buf_idx += num_of_alloc) {
			if (!res->aligned_size)
				continue;

			if (!(parallel_idx == 0 ||
				    (io_buf_type && !is_shared_data(data_file))))
				continue;

			buf_size = res->aligned_size;
			if (io_buf_type & IPTS_INPUT_ON) {
				buf_size = max_t(u32,
						ipts->device_info.frame_size,
						buf_size);
				wl[parallel_idx].iobuf_input = buf_idx;
			} else if (io_buf_type & IPTS_OUTPUT_ON) {
				wl[parallel_idx].iobuf_output[output_idx] = buf_idx;

				if (!is_parsing_vendor_kernel(parse_info) &&
								output_idx > 0) {
					ipts_err(ipts,
						"postproc with more than one inout"
						" is not supported : %d\n", output_idx);
					return -EINVAL;
				}
			}

			if (!is_parsing_vendor_kernel(parse_info) &&
						io_buf_type & IPTS_OUTPUT_ON) {
				buf = bin_get_vendor_kernel_output(
								parse_info,
								parallel_idx);
				alloc_info->buffs[buf_idx].no_unmap = true;
			} else
				buf = ipts_map_buffer(ipts, buf_size, flags);

			if (buf == NULL) {
				ipts_dbg(ipts, "ipts_map_buffer failed\n");
				return -ENOMEM;
			}

			if (initialize) {
				memcpy((void *)buf->cpu_addr, &(res->data[0]),
								res->size);
			} else {
				if (data_file && strlen(data_file->file_name)) {
					bin_read_fw(ipts, data_file->file_name,
                                                        buf->cpu_addr, buf_size);
				} else if (is_parsing_vendor_kernel(parse_info) ||
						!(io_buf_type & IPTS_OUTPUT_ON)) {
					memset((void *)buf->cpu_addr, 0, res->size);
				}
			}

			alloc_info->buffs[buf_idx].buf = buf;
		}
	}

        alloc_info->num_of_outputs = output_idx + 1;
	parse_info->parsed = parsed;

	return 0;
}

static int bin_read_patch_list(ipts_info_t *ipts,
					bin_parse_info_t *parse_info,
					bin_alloc_info_t *alloc_info,
					bin_workload_t *wl)
{
	ipts_bin_patch_list_t *patch_list;
	ipts_bin_patch_t *patch;
	intel_ipts_mapbuffer_t *cmd = NULL;
	u8 *batch;
	int parsed, size, i, parallel_idx, num_of_parallels, cmd_idx, buf_idx;
	unsigned int gtt_offset;

	parsed = parse_info->parsed;
	size = parse_info->size;
	patch_list = (ipts_bin_patch_list_t *)&parse_info->data[parsed];

	if (sizeof(patch_list->num) > (size - parsed)) {
		return -EFAULT;
	}
	parsed += sizeof(patch_list->num);

	num_of_parallels = ipts_get_num_of_parallel_buffers(ipts);
	patch = (ipts_bin_patch_t *)(&patch_list->patch[0]);
	for (i = 0; i < patch_list->num; i++) {
		if (sizeof(patch_list->patch[0]) > (size - parsed)) {
			return -EFAULT;
		}

		for (parallel_idx = 0; parallel_idx < num_of_parallels;
								parallel_idx++) {
			cmd_idx = wl[parallel_idx].cmdbuf_index;
			buf_idx = patch[i].index + parallel_idx *
						alloc_info->num_of_allocations;

			if (alloc_info->buffs[buf_idx].buf == NULL) {
				/* buffer shared */
				buf_idx = patch[i].index;
			}

			cmd = alloc_info->buffs[cmd_idx].buf;
			batch = (char *)(u64)cmd->cpu_addr;

			gtt_offset = 0;
			if(alloc_info->buffs[buf_idx].buf != NULL) {
				gtt_offset = (u32)(u64)
					alloc_info->buffs[buf_idx].buf->gfx_addr;
			} 
			gtt_offset += patch[i].alloc_offset;

			batch += patch[i].patch_offset;
			*(u32*)batch = gtt_offset;
		}

		parsed += sizeof(patch_list->patch[0]);
	}

	parse_info->parsed = parsed;

	return 0;
}

static int bin_read_guc_wq_item(ipts_info_t *ipts,
					bin_parse_info_t *parse_info,
					bin_guc_wq_item_t **guc_wq_item)
{
	ipts_bin_guc_wq_info_t *bin_guc_wq;
	bin_guc_wq_item_t *item;
	u8 *wi_data;
	int size, parsed, hdr_size, wi_size;
	int i, batch_offset;
	
	parsed = parse_info->parsed;
	size = parse_info->size;
	bin_guc_wq = (ipts_bin_guc_wq_info_t *)&parse_info->data[parsed];

	wi_size = bin_guc_wq->size;
	wi_data = bin_guc_wq->data;
	batch_offset = bin_guc_wq->batch_offset;
	ipts_dbg(ipts, "wi size = %d, bt offset = %d\n", wi_size, batch_offset);
	for (i = 0; i < wi_size / sizeof(u32); i++) {
		ipts_dbg(ipts, "wi[%d] = 0x%08x\n", i, *((u32*)wi_data + i));
	}
	hdr_size = sizeof(bin_guc_wq->size) + sizeof(bin_guc_wq->batch_offset);

	if (hdr_size > (size - parsed)) {
		return -EINVAL;
	}
	parsed += hdr_size;

	item = vmalloc(sizeof(bin_guc_wq_item_t) + wi_size);
	if (item == NULL)
		return -ENOMEM;

	item->size = wi_size;
	item->batch_offset = batch_offset;
	memcpy(item->data, wi_data, wi_size);

	*guc_wq_item = item;

	parsed += wi_size;
	parse_info->parsed = parsed;

	return 0;
}

static int bin_setup_guc_workqueue(ipts_info_t *ipts,
					bin_kernel_list_t *kernel_list)
{
	bin_alloc_info_t *alloc_info;
	bin_workload_t *wl;
	bin_kernel_info_t *kernel;
	u8 *wq_start, *wq_addr, *wi_data;
	bin_buffer_t *bin_buf;
	int wq_size, wi_size, parallel_idx, cmd_idx, k_idx, iter_size;
	int i, num_of_parallels, batch_offset, k_num, total_workload;
	
	wq_addr = (u8*)ipts->resource.wq_info.wq_addr;
	wq_size = ipts->resource.wq_info.wq_size;
	num_of_parallels = ipts_get_num_of_parallel_buffers(ipts);
	total_workload = ipts_get_wq_item_size(ipts);
	k_num = kernel_list->num_of_kernels;

	iter_size = total_workload * num_of_parallels;
	if (wq_size % iter_size) {
		ipts_err(ipts, "wq item cannot fit into wq\n");
		return -EINVAL;
	}

	wq_start = wq_addr;
	for (parallel_idx = 0; parallel_idx < num_of_parallels;
							parallel_idx++) {
		kernel = &kernel_list->kernels[0];
		for (k_idx = 0; k_idx < k_num; k_idx++, kernel++) {
			wl = kernel->wl;
			alloc_info = kernel->alloc_info;

			batch_offset = kernel->guc_wq_item->batch_offset;
			wi_size = kernel->guc_wq_item->size;
			wi_data = &kernel->guc_wq_item->data[0];
			
			cmd_idx = wl[parallel_idx].cmdbuf_index;
			bin_buf = &alloc_info->buffs[cmd_idx];

			/* Patch the WQ Data with proper batch buffer offset */
			*(u32*)(wi_data + batch_offset) =
				(u32)(unsigned long)(bin_buf->buf->gfx_addr);

			memcpy(wq_addr, wi_data, wi_size);

			wq_addr += wi_size;
		}
	}

	for (i = 0; i < (wq_size / iter_size) - 1; i++) {
		memcpy(wq_addr, wq_start, iter_size);
		wq_addr += iter_size;
	}

	return 0;
}

static int bin_read_bufid_patch(ipts_info_t *ipts,
					bin_parse_info_t *parse_info,
					ipts_bin_bufid_patch_t *bufid_patch)
{
	ipts_bin_bufid_patch_t *patch;
	int size, parsed;

	parsed = parse_info->parsed;
	size = parse_info->size;
	patch = (ipts_bin_bufid_patch_t *)&parse_info->data[parsed];

	if (sizeof(ipts_bin_bufid_patch_t) > (size - parsed)) {
		ipts_dbg(ipts, "invalid bufid info\n");
		return -EINVAL;
	}
	parsed += sizeof(ipts_bin_bufid_patch_t);

	memcpy(bufid_patch, patch, sizeof(ipts_bin_bufid_patch_t));

	parse_info->parsed = parsed;

	return 0;
}

static int bin_setup_bufid_buffer(ipts_info_t *ipts, bin_kernel_list_t *kernel_list)
{
	intel_ipts_mapbuffer_t *buf, *cmd_buf;
	bin_kernel_info_t *last_kernel;
	bin_alloc_info_t *alloc_info;
	bin_workload_t *wl;
	u8 *batch;
	int parallel_idx, num_of_parallels, cmd_idx;
	u32 mem_offset, imm_offset;

	buf = ipts_map_buffer(ipts, PAGE_SIZE, 0);
	if (!buf) {
		return -ENOMEM;
	}

	last_kernel = &kernel_list->kernels[kernel_list->num_of_kernels - 1];

	mem_offset = last_kernel->bufid_patch.mem_offset;
	imm_offset = last_kernel->bufid_patch.imm_offset;
	wl = last_kernel->wl;
	alloc_info = last_kernel->alloc_info;

	/* Initialize the buffer with default value */
        *((u32*)buf->cpu_addr) = LASTSUBMITID_DEFAULT_VALUE;
	ipts->current_buffer_index = LASTSUBMITID_DEFAULT_VALUE;
	ipts->last_buffer_completed = LASTSUBMITID_DEFAULT_VALUE;
	ipts->last_submitted_id = (int*)buf->cpu_addr;

	num_of_parallels = ipts_get_num_of_parallel_buffers(ipts);
	for (parallel_idx = 0; parallel_idx < num_of_parallels; parallel_idx++) {
		cmd_idx = wl[parallel_idx].cmdbuf_index;
		cmd_buf = alloc_info->buffs[cmd_idx].buf;
		batch = (u8*)(u64)cmd_buf->cpu_addr;

		*((u32*)(batch + mem_offset)) = (u32)(u64)(buf->gfx_addr);
                *((u32*)(batch + imm_offset)) = parallel_idx;
	}

	kernel_list->bufid_buf = buf;

	return 0;
}

static void unmap_buffers(ipts_info_t *ipts, bin_alloc_info_t *alloc_info)
{
	bin_buffer_t *buffs;
	int i, num_of_buffers;

	num_of_buffers = alloc_info->num_of_buffers;
	buffs = &alloc_info->buffs[0];

	for (i = 0; i < num_of_buffers; i++) {
		if (buffs[i].no_unmap != true && buffs[i].buf != NULL)
			ipts_unmap_buffer(ipts, buffs[i].buf);
	}
}

static int load_kernel(ipts_info_t *ipts, bin_parse_info_t *parse_info,
						bin_kernel_info_t *kernel)
{
	ipts_bin_header_t *hdr;
	bin_workload_t *wl;
	bin_alloc_info_t *alloc_info;
	bin_guc_wq_item_t *guc_wq_item = NULL;
	ipts_bin_bufid_patch_t bufid_patch;
	int num_of_parallels, ret;

	num_of_parallels = ipts_get_num_of_parallel_buffers(ipts);

        /* check header version and magic numbers */
	hdr = (ipts_bin_header_t *)parse_info->data;
	if (hdr->version != IPTS_BIN_HEADER_VERSION ||
					strncmp(hdr->str, "IOCL", 4) != 0) {
		ipts_err(ipts, "binary header is not correct version = %d, "
				"string = %c%c%c%c\n", hdr->version,
				hdr->str[0], hdr->str[1],
				hdr->str[2], hdr->str[3] );
		return -EINVAL;
	}

	parse_info->parsed = sizeof(ipts_bin_header_t);
	wl = vmalloc(sizeof(bin_workload_t) * num_of_parallels);
	if (wl == NULL)
		return -ENOMEM;
	memset(wl, 0, sizeof(bin_workload_t) * num_of_parallels);

	alloc_info = vmalloc(sizeof(bin_alloc_info_t));
	if (alloc_info == NULL) {
		vfree(wl);
		return -ENOMEM;
	}
	memset(alloc_info, 0, sizeof(bin_alloc_info_t));

        ipts_dbg(ipts, "kernel setup(size : %d)\n", parse_info->size);

	ret = bin_read_allocation_list(ipts, parse_info, alloc_info);
	if (ret) {
        	ipts_dbg(ipts, "error read_allocation_list\n");
		goto setup_error;
	}

	ret = bin_read_cmd_buffer(ipts, parse_info, alloc_info, wl);
	if (ret) {
        	ipts_dbg(ipts, "error read_cmd_buffer\n");
		goto setup_error;
	}

	ret = bin_read_res_list(ipts, parse_info, alloc_info, wl);
	if (ret) {
        	ipts_dbg(ipts, "error read_res_list\n");
		goto setup_error;
	}

	ret = bin_read_patch_list(ipts, parse_info, alloc_info, wl);
	if (ret) {
        	ipts_dbg(ipts, "error read_patch_list\n");
		goto setup_error;
	}

	ret = bin_read_guc_wq_item(ipts, parse_info, &guc_wq_item);
	if (ret) {
        	ipts_dbg(ipts, "error read_guc_workqueue\n");
		goto setup_error;
	}

	memset(&bufid_patch, 0, sizeof(bufid_patch));
	ret = bin_read_bufid_patch(ipts, parse_info, &bufid_patch);
	if (ret) {
        	ipts_dbg(ipts, "error read_bufid_patch\n");
		goto setup_error;
	}

	kernel->wl = wl;
	kernel->alloc_info = alloc_info;
	kernel->is_vendor = is_parsing_vendor_kernel(parse_info);
	kernel->guc_wq_item = guc_wq_item;
	memcpy(&kernel->bufid_patch, &bufid_patch, sizeof(bufid_patch));

        return 0;

setup_error:
	vfree(guc_wq_item);

	unmap_buffers(ipts, alloc_info);

	vfree(alloc_info->buffs);
	vfree(alloc_info);
	vfree(wl);

	return ret;
}

void bin_setup_input_output(ipts_info_t *ipts, bin_kernel_list_t *kernel_list)
{
	bin_kernel_info_t *vendor_kernel;
	bin_workload_t *wl;
	intel_ipts_mapbuffer_t *buf;
	bin_alloc_info_t *alloc_info;
	int parallel_idx, num_of_parallels, i, buf_idx;

	vendor_kernel = &kernel_list->kernels[0];

	wl = vendor_kernel->wl;
	alloc_info = vendor_kernel->alloc_info;
	ipts->resource.num_of_outputs = alloc_info->num_of_outputs;
	num_of_parallels = ipts_get_num_of_parallel_buffers(ipts);

	for (parallel_idx = 0; parallel_idx < num_of_parallels; parallel_idx++) {
		buf_idx = wl[parallel_idx].iobuf_input;
		buf = alloc_info->buffs[buf_idx].buf;

		ipts_dbg(ipts, "in_buf[%d](%d) c:%p, p:%p, g:%p\n",
					parallel_idx, buf_idx, (void*)buf->cpu_addr,
					(void*)buf->phy_addr, (void*)buf->gfx_addr);

		ipts_set_input_buffer(ipts, parallel_idx, buf->cpu_addr,
								buf->phy_addr);

		for (i = 0; i < alloc_info->num_of_outputs; i++) {
			buf_idx = wl[parallel_idx].iobuf_output[i];
			buf = alloc_info->buffs[buf_idx].buf;

			ipts_dbg(ipts, "out_buf[%d][%d] c:%p, p:%p, g:%p\n",
					parallel_idx, i, (void*)buf->cpu_addr,
					(void*)buf->phy_addr, (void*)buf->gfx_addr);

			ipts_set_output_buffer(ipts, parallel_idx, i,
					buf->cpu_addr, buf->phy_addr);
		}
	}
}

static void unload_kernel(ipts_info_t *ipts, bin_kernel_info_t *kernel)
{
	bin_alloc_info_t *alloc_info = kernel->alloc_info;
	bin_guc_wq_item_t *guc_wq_item = kernel->guc_wq_item;

	if (guc_wq_item) {
		vfree(guc_wq_item);
	}

	if (alloc_info) {
		unmap_buffers(ipts, alloc_info);

		vfree(alloc_info->buffs);
		vfree(alloc_info);
	}
}

static int setup_kernel(ipts_info_t *ipts, bin_fw_list_t *fw_list)
{
	bin_kernel_list_t *kernel_list = NULL;
	bin_kernel_info_t *kernel = NULL;
	const struct firmware *fw = NULL;
	bin_workload_t *wl;
	bin_fw_info_t *fw_info;
	char *fw_name, *fw_data;
	bin_parse_info_t parse_info;
	int ret = 0, kernel_idx = 0, num_of_kernels = 0;
	int vendor_output_idx, total_workload = 0;
	char fw_path[MAX_IOCL_FILE_PATH_LEN];

	num_of_kernels = fw_list->num_of_fws;
	kernel_list = vmalloc(sizeof(*kernel) * num_of_kernels + sizeof(*kernel_list));
	if (kernel_list == NULL)
		return -ENOMEM;

	memset(kernel_list, 0, sizeof(*kernel) * num_of_kernels + sizeof(*kernel_list));
	kernel_list->num_of_kernels = num_of_kernels;
	kernel = &kernel_list->kernels[0];

	fw_data = (char *)&fw_list->fw_info[0];
	for (kernel_idx = 0; kernel_idx < num_of_kernels; kernel_idx++) {
		fw_info = (bin_fw_info_t *)fw_data;
		fw_name = &fw_info->fw_name[0];
		vendor_output_idx = fw_info->vendor_output;
		snprintf(fw_path, MAX_IOCL_FILE_PATH_LEN, IPTS_FW_PATH_FMT, fw_name);
		ret = request_firmware(&fw, (const char *)fw_path, &ipts->cldev->dev);
		if (ret) {
			ipts_err(ipts, "cannot read fw %s\n", fw_path);
			goto error_exit;
		}

		parse_info.data = (u8*)fw->data;
		parse_info.size = fw->size;
		parse_info.parsed = 0;
		parse_info.fw_info = fw_info;
		parse_info.vendor_kernel = (kernel_idx == 0) ? NULL : &kernel[0];
		parse_info.interested_vendor_output = vendor_output_idx;

		ret = load_kernel(ipts, &parse_info, &kernel[kernel_idx]);
		if (ret) {
			ipts_err(ipts, "do_setup_kernel error : %d\n", ret);
			release_firmware(fw);
			goto error_exit;
		}

		release_firmware(fw);

		total_workload += kernel[kernel_idx].guc_wq_item->size;

		/* advance to the next kernel */
		fw_data += sizeof(bin_fw_info_t);
		fw_data += sizeof(bin_data_file_info_t) * fw_info->num_of_data_files;
	}

	ipts_set_wq_item_size(ipts, total_workload);
	
	ret = bin_setup_guc_workqueue(ipts, kernel_list);
	if (ret) {
        	ipts_dbg(ipts, "error setup_guc_workqueue\n");
		goto error_exit;
	}

	ret = bin_setup_bufid_buffer(ipts, kernel_list);
	if (ret) {
        	ipts_dbg(ipts, "error setup_lastbubmit_buffer\n");
		goto error_exit;
	}

	bin_setup_input_output(ipts, kernel_list);

	/* workload is not needed during run-time so free them */
	for (kernel_idx = 0; kernel_idx < num_of_kernels; kernel_idx++) {
		wl = kernel[kernel_idx].wl;
		vfree(wl);
	}

	ipts->kernel_handle = (u64)kernel_list;

	return 0;

error_exit:

	for (kernel_idx = 0; kernel_idx < num_of_kernels; kernel_idx++) {
		wl = kernel[kernel_idx].wl;
		vfree(wl);
		unload_kernel(ipts, &kernel[kernel_idx]);
	}

	vfree(kernel_list);

	return ret;
}


static void release_kernel(ipts_info_t *ipts)
{
	bin_kernel_list_t *kernel_list;
	bin_kernel_info_t *kernel;
	int k_idx, k_num;

	kernel_list = (bin_kernel_list_t *)ipts->kernel_handle;
	k_num = kernel_list->num_of_kernels;
	kernel = &kernel_list->kernels[0];

	for (k_idx = 0; k_idx < k_num; k_idx++) {
		unload_kernel(ipts, kernel);
		kernel++;
	}	

	ipts_unmap_buffer(ipts, kernel_list->bufid_buf);

	vfree(kernel_list);
	ipts->kernel_handle = 0;
}

int ipts_init_kernels(ipts_info_t *ipts)
{
	const struct firmware *config_fw = NULL;
	const char *config_fw_path = IPTS_FW_CONFIG_FILE;
	bin_fw_list_t *fw_list;
	int ret;

	ret = ipts_open_gpu(ipts);
	if (ret) {
		ipts_err(ipts, "open gpu error : %d\n", ret);
		return ret;
	}

	ret = request_firmware(&config_fw, config_fw_path, &ipts->cldev->dev);
	if (ret) {
		ipts_err(ipts, "request firmware error : %d\n", ret);
		goto close_gpu;
	}

	fw_list = (bin_fw_list_t *)config_fw->data;
	ret = setup_kernel(ipts, fw_list);
	if (ret) {
		ipts_err(ipts, "setup kernel error : %d\n", ret);
		goto close_firmware;
	}

	release_firmware(config_fw);

	return ret;

close_firmware:
	release_firmware(config_fw);

close_gpu:
	ipts_close_gpu(ipts);

	return ret;
}

void ipts_release_kernels(ipts_info_t *ipts)
{
	release_kernel(ipts);
	ipts_close_gpu(ipts);
}
