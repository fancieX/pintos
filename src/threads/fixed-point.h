#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#include <stdint.h>

/* The scaling factor f = 2**14 */
#define F (1 << 14)

/* Convert integer n to fixed point */
#define INT_TO_FP(n) ((n) * (F))

/* Convert fixed point x to integer (rounding toward zero) */
#define FP_TO_INT_ZERO(x) ((x) / (F))

/* Convert fixed point x to integer (rounding to nearest) */
#define FP_TO_INT_ROUND(x) ((x) >= 0 ? (((x) + (F) / 2) / (F)) : (((x) - (F) / 2) / (F)))

/* Add fixed point x and y */
#define FP_ADD(x, y) ((x) + (y))

/* Subtract fixed point y from x */
#define FP_SUB(x, y) ((x) - (y))

/* Add fixed point x and integer n */
#define FP_ADD_INT(x, n) ((x) + (n) * (F))

/* Subtract integer n from fixed point x */
#define FP_SUB_INT(x, n) ((x) - (n) * (F))

/* Multiply fixed point x by fixed point y. 
   Cast to int64_t to prevent overflow before dividing by F */
#define FP_MULT(x, y) ((int) (((int64_t) (x)) * (y) / (F)))

/* Multiply fixed point x by integer n */
#define FP_MULT_INT(x, n) ((x) * (n))

/* Divide fixed point x by fixed point y */
#define FP_DIV(x, y) ((int) ((((int64_t) (x)) * (F)) / (y)))

/* Divide fixed point x by integer n */
#define FP_DIV_INT(x, n) ((x) / (n))

#endif /* threads/fixed-point.h */