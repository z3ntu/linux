/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_HELPER_MACROS_H_
#define _LINUX_HELPER_MACROS_H_

#include <linux/math.h>

#define __find_closest(x, a, as, op)					\
({									\
	typeof(as) __fc_i, __fc_as = (as) - 1;				\
	typeof(x) __fc_x = (x);						\
	typeof(*a) const *__fc_a = (a);					\
	for (__fc_i = 0; __fc_i < __fc_as; __fc_i++) {			\
		if (__fc_x op DIV_ROUND_CLOSEST(__fc_a[__fc_i] +	\
						__fc_a[__fc_i + 1], 2))	\
			break;						\
	}								\
	(__fc_i);							\
})

/**
 * find_closest - locate the closest element in a sorted array
 * @x: The reference value.
 * @a: The array in which to look for the closest element. Must be sorted
 *  in ascending order.
 * @as: Size of 'a'.
 *
 * Returns the index of the element closest to 'x'.
 */
#define find_closest(x, a, as) __find_closest(x, a, as, <=)

/**
 * find_closest_descending - locate the closest element in a sorted array
 * @x: The reference value.
 * @a: The array in which to look for the closest element. Must be sorted
 *  in descending order.
 * @as: Size of 'a'.
 *
 * Similar to find_closest() but 'a' is expected to be sorted in descending
 * order.
 */
#define find_closest_descending(x, a, as) __find_closest(x, a, as, >=)

/**
 * is_insidevar - check if the @ptr points inside the @var memory range.
 * @ptr:	the pointer to a memory address.
 * @var:	the variable which address and size identify the memory range.
 *
 * Evaluates to true if the address in @ptr lies within the memory
 * range allocated to @var.
 */
#define is_insidevar(ptr, var)						\
	((uintptr_t)(ptr) >= (uintptr_t)(var) &&			\
	 (uintptr_t)(ptr) <  (uintptr_t)(var) + sizeof(var))

/**
 * find_closest_smaller - locate the closest smaller element in a sorted array
 * @x: The reference value.
 * @a: The array in which to look for the closest smaller element. Must be
 *  sorted in ascending order.
 * @as: Size of 'a'.
 *
 * Returns the index of the element closest to and smaller than 'x', or -1
 * if no element smaller than 'x' exists in the array.
 */
#define find_closest_smaller(x, a, as)					\
({									\
	typeof(as) __fcs_i;						\
	typeof(x) __fcs_x = (x);					\
	typeof(*a) const *__fcs_a = (a);				\
	for (__fcs_i = 0; __fcs_i < (as); __fcs_i++) {			\
		if (__fcs_x < __fcs_a[__fcs_i])				\
			break;						\
	}								\
	(__fcs_i - 1);							\
})

#endif
