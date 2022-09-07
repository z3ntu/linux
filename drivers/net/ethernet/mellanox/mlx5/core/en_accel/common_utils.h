/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. */
#ifndef __MLX5E_COMMON_UTILS_H__
#define __MLX5E_COMMON_UTILS_H__

#include "en.h"

struct mlx5e_set_transport_static_params_wqe {
	struct mlx5_wqe_ctrl_seg ctrl;
	struct mlx5_wqe_umr_ctrl_seg uctrl;
	struct mlx5_mkey_seg mkc;
	struct mlx5_wqe_transport_static_params_seg params;
};

/* macros for transport_static_params handling */
#define MLX5E_TRANSPORT_SET_STATIC_PARAMS_WQEBBS \
	(DIV_ROUND_UP(sizeof(struct mlx5e_set_transport_static_params_wqe), MLX5_SEND_WQE_BB))

#define MLX5E_TRANSPORT_FETCH_SET_STATIC_PARAMS_WQE(sq, pi) \
	((struct mlx5e_set_transport_static_params_wqe *)\
	 mlx5e_fetch_wqe(&(sq)->wq, pi, sizeof(struct mlx5e_set_transport_static_params_wqe)))

#define MLX5E_TRANSPORT_STATIC_PARAMS_WQE_SZ \
	(sizeof(struct mlx5e_set_transport_static_params_wqe))

#define MLX5E_TRANSPORT_STATIC_PARAMS_DS_CNT \
	(DIV_ROUND_UP(MLX5E_TRANSPORT_STATIC_PARAMS_WQE_SZ, MLX5_SEND_WQE_DS))

#define MLX5E_TRANSPORT_STATIC_PARAMS_OCTWORD_SIZE \
	(MLX5_ST_SZ_BYTES(transport_static_params) / MLX5_SEND_WQE_DS)

#endif /* __MLX5E_COMMON_UTILS_H__ */
