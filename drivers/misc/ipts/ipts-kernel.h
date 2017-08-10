/*
 *
 * Intel Precise Touch & Stylus Linux driver
 * Copyright (c) 2016, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#ifndef _ITPS_GFX_H
#define _ITPS_GFX_H

int ipts_init_kernels(ipts_info_t *ipts);
void ipts_release_kernels(ipts_info_t *ipts);

#endif
