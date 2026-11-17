/* doors.c and face_kit's cols.c with sinf and cosf off (by 1e-4) (wave 2 Task 11; rd wave 1 Task 1, PR2: the column
 * renderer's trig, leaf_cols, inward_sincos and edge_pts, moved to cols.c, so the perturbation must reach it there):
 * test_dw_doors_libm checks that the kept leaves' still geometry does not move, as it must not when the device's newlib
 * and the host's glibc differ by an ulp. The perturbed functions are test_dw_doors.c's (DW_DOORS_LIBM_PERTURB). */
#include <math.h>
float perturbed_sinf(float x);
float perturbed_cosf(float x);
#define sinf perturbed_sinf
#define cosf perturbed_cosf
#include "cols.c"
#undef N
#undef C
#undef F
#undef D2R
#include "doors.c"
