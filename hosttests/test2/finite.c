#include <math.h>
#include "land.c"
int land_check(float *amax, float *vmax) {
    int bad = 0; float a = 0, w = 0;
    for (int i = 0; i < NP; i++) { if (!isfinite(A[i]) || !isfinite(CR[i]) || !isfinite(CG[i]) || !isfinite(CB[i])) bad++; if (A[i] > a) a = A[i]; }
    for (int i = 0; i < NV; i++) { if (!isfinite(u[i]) || !isfinite(v[i])) bad++; float s = fabsf(u[i]) + fabsf(v[i]); if (s > w) w = s; }
    *amax = a; *vmax = w; return bad;
}
