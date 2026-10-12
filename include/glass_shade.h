#ifndef BARNY_GLASS_SHADE_H
#define BARNY_GLASS_SHADE_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

/* Key light for the droplet specular: from above, slightly left, matching
   the diagonal sheen of the glass frame. */
#define GLASS_SPEC_LX    (-0.30)
#define GLASS_SPEC_LY    (-0.954)
#define GLASS_RIM_CHROMA 0.28 /* vertical colour dispersion of the rim */
#define GLASS_NO_COUNTER (-0.0)

/* x^10, the specular exponent, without a libm call: pow() ran on every lit
   pixel of the droplet rim for an exponent fixed at compile time. */
static inline double
glass_pow10(double x)
{
	double x2 = x * x;
	double x4 = x2 * x2;
	double x8 = x4 * x4;

	return x8 * x2;
}

static inline double
glass_smoothstep01(double t)
{
	return t * t * (3.0 - 2.0 * t);
}

static inline double
glass_lit(double facing)
{
	return 0.45 + 0.55 * (facing > 0.15 ? facing : 0.15);
}

static inline double
glass_dome(double p, double zone)
{
	return zone * (sqrt(2.0 * p - p * p) - p);
}

static inline bool
glass_sdf_normal_aa(double d, double dl, double dr, double du, double dv,
                    double *nnx, double *nny, double *aa)
{
	double nlen = sqrt((dr - dl) * (dr - dl) + (dv - du) * (dv - du));

	if (nlen > 1e-6) {
		*nnx = (dr - dl) / nlen;
		*nny = (dv - du) / nlen;
		*aa  = 0.5 - d / (nlen * 0.5);
	} else {
		*aa = d < 0.0 ? 1.0 : 0.0;
	}
	if (*aa <= 0.0)
		return false;
	if (*aa > 1.0)
		*aa = 1.0;

	return true;
}

static inline void
glass_tint(double *fr, double *fg, double *fb, double aw, double rim,
           double ty, double spec, double cntr)
{
	*fr  = *fr * (1.0 - aw) + 255.0 * aw;
	*fg  = *fg * (1.0 - aw) + 255.0 * aw;
	*fb  = *fb * (1.0 - aw) + 255.0 * aw;
	*fr += (rim * (1.0 - GLASS_RIM_CHROMA * ty) + spec + cntr * 0.85)
	       * 255.0;
	*fg += (rim + spec + cntr * 0.92) * 255.0;
	*fb += (rim * (1.0 + GLASS_RIM_CHROMA * ty) + spec + cntr) * 255.0;
	if (*fr > 255.0)
		*fr = 255.0;
	if (*fg > 255.0)
		*fg = 255.0;
	if (*fb > 255.0)
		*fb = 255.0;
}

static inline void
glass_put(uint8_t *px, double fr, double fg, double fb, double ap)
{
	px[0] = (uint8_t)(fb * ap);
	px[1] = (uint8_t)(fg * ap);
	px[2] = (uint8_t)(fr * ap);
	px[3] = (uint8_t)(255.0 * ap);
}

#endif
