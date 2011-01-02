#include "fixed-point.h"

fixedpoint
convert_int_to_fixedpoint (int n)
{
  return n * F;
}

int
convert_fixedpoint_to_int (fixedpoint x)
{
  /* rounding to nearest */
  if (x >= 0)
    {
      return ((x + F / 2) / F);
    }
  else
    {
      return ((x - F / 2) / F);
    }
}

fixedpoint
fixedpoint_add (fixedpoint x, fixedpoint y)
{
  return x + y;
}

fixedpoint
fixedpoint_add_int (fixedpoint x, int n)
{
  return x + convert_int_to_fixedpoint (n);
}

fixedpoint
fixedpoint_subtract (fixedpoint x, fixedpoint y)
{
  return x - y;
}

fixedpoint
fixedpoint_subtract_int (fixedpoint x, int n)
{
  return x - n * F;
}

fixedpoint
fixedpoint_multiply (fixedpoint x, fixedpoint y)
{
  return ((int64_t) x) * y / F;
}

fixedpoint
fixedpoint_multiply_int (fixedpoint x, int n)
{
  return x * n;
}

fixedpoint
fixedpoint_divide (fixedpoint x, fixedpoint y)
{
  return ((int64_t) x) * F / y;
}

fixedpoint
fixedpoint_divide_int (fixedpoint x, int n)
{
  return x / n;
}
