#ifndef FIXED_POINT_H_
#define FIXED_POINT_H_

#include <stdint.h>

/*
  For example, we can designate the lowest 14 bits of a signed 32-bit integer as
  fractional bits, so that an integer x represents the real number x/(2^14) . This is called a 17.14
  fixed-point number representation, because there are 17 bits before the decimal point, 14
  bits after it, and one sign bit. A number in 17.14 format represents, at maximum, a value
  of (2^31 − 1)/(2^14) ≈ 131,071.999.

  The following table summarizes how fixed-point arithmetic operations can be imple-
  mented in C. In the table, x and y are fixed-point numbers, n is an integer, fixed-point
  numbers are in signed p.q format where p + q = 31, and f is 1 << q:

  Convert n to fixed point:                     n*f
  Convert x to integer (rounding toward zero):  x/f
  Convert x to integer (rounding to nearest):   (x + f / 2) / f if x >= 0,
                                                (x - f / 2) / f if x <= 0.
  Add x and y:                                  x+y
  Subtract y from x:                            x-y
  Add x and n:                                  x+n*f
  Subtract n from x:                            x-n*f
  Multiply x by y:                              ((int64_t) x) * y / f
  Multiply x by n:                              x*n
  Divide x by y:                                ((int64_t) x) * f / y
  Divide x by n:                                x/n
*/

/*
  Some problem:

  Multiplying two fixed-point values has two complications. First, the decimal point of
  the result is q bits too far to the left. Consider that (59/60)(59/60) should be slightly less
  than 1, but 16, 111 × 16, 111 = 259,564,321 is much greater than 214 = 16,384. Shifting q
  bits right, we get 259, 564, 321/214 = 15,842, or about 0.97, the correct answer. Second,
  the multiplication can overflow even though the answer is representable. For example, 64
  in 17.14 format is 64 × 214 = 1,048,576 and its square 642 = 4,096 is well within the 17.14
  range, but 1, 048, 5762 = 240 , greater than the maximum signed 32-bit integer value 231 − 1.
  An easy solution is to do the multiplication as a 64-bit operation. The product of x and y
  is then ((int64_t) x) * y / f.

  Dividing two fixed-point values has opposite issues. The decimal point will be too far
  to the right, which we fix by shifting the dividend q bits to the left before the division.
  The left shift discards the top q bits of the dividend, which we can again fix by doing the
  division in 64 bits. Thus, the quotient when x is divided by y is ((int64_t) x) * f / y.
*/

#define Q 14
//#define F 1 << Q    why wrong when compute ((x + F / 2) / F)? 答：要加括号
#define F (1 << Q)    /* 1 << Q(Q=14)是16384 */

typedef int fixedpoint;

fixedpoint convert_int_to_fixedpoint (int n);
int convert_fixedpoint_to_int (fixedpoint fixedpoint);

fixedpoint fixedpoint_add (fixedpoint x, fixedpoint y);
fixedpoint fixedpoint_add_int (fixedpoint x, int n);
fixedpoint fixedpoint_subtract (fixedpoint x, fixedpoint y);
fixedpoint fixedpoint_subtract_int (fixedpoint x, int n);
fixedpoint fixedpoint_multiply (fixedpoint x, fixedpoint y);
fixedpoint fixedpoint_multiply_int (fixedpoint x, int n);
fixedpoint fixedpoint_divide (fixedpoint x, fixedpoint y);
fixedpoint fixedpoint_divide_int (fixedpoint x, int n);

#endif /* FIXED_POINT_H_ */
