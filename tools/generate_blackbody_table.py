#!/usr/bin/env python3
"""
Generate the 91-entry blackbody RGB scaling table used by KelvinShift's
GammaController (macOS, Swift) and GammaService (Windows, C++) to warm or
cool the display via the GPU gamma ramp.

This is an independent derivation from primary, public-domain sources.
No third-party color-temperature implementation was consulted.

Method
------
1. PLANCK'S LAW gives the spectral radiance of an ideal blackbody
   radiator at absolute temperature T:

       B(lambda, T) = (2 h c^2 / lambda^5) / (exp(h c / (lambda k T)) - 1)

   h, c, k are the SI-defined Planck constant, speed of light, and
   Boltzmann constant (2019 redefinition; all three are now exact).

2. CIE 1931 2-degree STANDARD OBSERVER color matching functions
   x_bar(lambda), y_bar(lambda), z_bar(lambda) (Commission internationale
   de l'eclairage, 1931 -- published tabular data, not subject to
   copyright) project the spectral radiance onto CIE tristimulus values
   by integration over the visible band 380-780 nm:

       X = integral of B(lambda, T) * x_bar(lambda) d lambda
       (similarly Y, Z)

   We use composite Simpson's rule on the 5 nm tabulation (O(h^4)
   accuracy) -- see https://en.wikipedia.org/wiki/Simpson%27s_rule.

3. The sRGB LINEAR TRANSFORM from IEC 61966-2-1 maps (X, Y, Z) to
   linear sRGB primaries with D65 white point.

4. VON KRIES CHROMATIC ADAPTATION (von Kries 1902, applied directly
   in sRGB space) -- the display is already calibrated to D65 white,
   so we are not displaying an absolute T-Kelvin stimulus; we are
   adapting the D65 display so that white *appears* lit by a T-Kelvin
   illuminant. The von Kries transform models this as independent
   linear gain on each channel. We compute the reference linear sRGB
   at 6500 K, divide every row by that reference channel-wise, and
   clip to [0, 1]. The row at 6500 K becomes exactly (1, 1, 1) -- no
   display tint -- which is precisely what CGSetDisplayTransferByTable
   (macOS) and SetDeviceGammaRamp (Windows) expect as the identity
   gamma ramp.

Output: a 91-row table for 1000 K -> 10000 K in 100 K steps, printed
as a Swift array block and a C++ array block, ready to paste into the
corresponding source files.

Re-running with the same inputs is deterministic.

Run: python3 tools/generate_blackbody_table.py
"""

import math


# -- Physical constants (SI, 2019 redefinition; all exact) -------------------
H_PLANCK    = 6.62607015e-34   # J*s
C_LIGHT     = 2.99792458e8     # m/s
K_BOLTZMANN = 1.380649e-23     # J/K


# -- CIE 1931 2-degree standard observer color matching functions ------------
# 5 nm tabulation, 380-780 nm. Published 1931 by the CIE; tabular data,
# public domain. Reproduced verbatim in Wyszecki & Stiles, "Color Science"
# (1982); Hunt, "The Reproduction of Colour" (6e, 2004); CIE 15:2018; etc.
# Format: (wavelength_nm, x_bar, y_bar, z_bar).
CIE_1931_2DEG_5NM = (
    (380, 0.001368, 0.000039, 0.006450),
    (385, 0.002236, 0.000064, 0.010550),
    (390, 0.004243, 0.000120, 0.020050),
    (395, 0.007650, 0.000217, 0.036210),
    (400, 0.014310, 0.000396, 0.067850),
    (405, 0.023190, 0.000640, 0.110200),
    (410, 0.043510, 0.001210, 0.207400),
    (415, 0.077630, 0.002180, 0.371300),
    (420, 0.134380, 0.004000, 0.645600),
    (425, 0.214770, 0.007300, 1.039050),
    (430, 0.283900, 0.011600, 1.385600),
    (435, 0.328500, 0.016840, 1.622960),
    (440, 0.348280, 0.023000, 1.747060),
    (445, 0.348060, 0.029800, 1.782600),
    (450, 0.336200, 0.038000, 1.772110),
    (455, 0.318700, 0.048000, 1.744100),
    (460, 0.290800, 0.060000, 1.669200),
    (465, 0.251100, 0.073900, 1.528100),
    (470, 0.195360, 0.090980, 1.287640),
    (475, 0.142100, 0.112600, 1.041900),
    (480, 0.095640, 0.139020, 0.812950),
    (485, 0.057950, 0.169300, 0.616200),
    (490, 0.032010, 0.208020, 0.465180),
    (495, 0.014700, 0.258600, 0.353300),
    (500, 0.004900, 0.323000, 0.272000),
    (505, 0.002400, 0.407300, 0.212300),
    (510, 0.009300, 0.503000, 0.158200),
    (515, 0.029100, 0.608200, 0.111700),
    (520, 0.063270, 0.710000, 0.078250),
    (525, 0.109600, 0.793200, 0.057250),
    (530, 0.165500, 0.862000, 0.042160),
    (535, 0.225750, 0.914850, 0.029840),
    (540, 0.290400, 0.954000, 0.020300),
    (545, 0.359700, 0.980300, 0.013400),
    (550, 0.433450, 0.994950, 0.008750),
    (555, 0.512050, 1.000000, 0.005750),
    (560, 0.594500, 0.995000, 0.003900),
    (565, 0.678400, 0.978600, 0.002750),
    (570, 0.762100, 0.952000, 0.002100),
    (575, 0.842500, 0.915400, 0.001800),
    (580, 0.916300, 0.870000, 0.001650),
    (585, 0.978600, 0.816300, 0.001400),
    (590, 1.026300, 0.757000, 0.001100),
    (595, 1.056700, 0.694900, 0.001000),
    (600, 1.062200, 0.631000, 0.000800),
    (605, 1.045600, 0.566800, 0.000600),
    (610, 1.002600, 0.503000, 0.000340),
    (615, 0.938400, 0.441200, 0.000240),
    (620, 0.854450, 0.381000, 0.000190),
    (625, 0.751400, 0.321000, 0.000100),
    (630, 0.642400, 0.265000, 0.000050),
    (635, 0.541900, 0.217000, 0.000030),
    (640, 0.447900, 0.175000, 0.000020),
    (645, 0.360800, 0.138200, 0.000010),
    (650, 0.283500, 0.107000, 0.000000),
    (655, 0.218700, 0.081600, 0.000000),
    (660, 0.164900, 0.061000, 0.000000),
    (665, 0.121200, 0.044580, 0.000000),
    (670, 0.087400, 0.032000, 0.000000),
    (675, 0.063600, 0.023200, 0.000000),
    (680, 0.046770, 0.017000, 0.000000),
    (685, 0.032900, 0.011920, 0.000000),
    (690, 0.022700, 0.008210, 0.000000),
    (695, 0.015840, 0.005723, 0.000000),
    (700, 0.011359, 0.004102, 0.000000),
    (705, 0.008111, 0.002929, 0.000000),
    (710, 0.005790, 0.002091, 0.000000),
    (715, 0.004109, 0.001484, 0.000000),
    (720, 0.002899, 0.001047, 0.000000),
    (725, 0.002049, 0.000740, 0.000000),
    (730, 0.001440, 0.000520, 0.000000),
    (735, 0.001000, 0.000361, 0.000000),
    (740, 0.000690, 0.000249, 0.000000),
    (745, 0.000476, 0.000172, 0.000000),
    (750, 0.000332, 0.000120, 0.000000),
    (755, 0.000235, 0.000085, 0.000000),
    (760, 0.000166, 0.000060, 0.000000),
    (765, 0.000117, 0.000042, 0.000000),
    (770, 0.000083, 0.000030, 0.000000),
    (775, 0.000059, 0.000021, 0.000000),
    (780, 0.000042, 0.000015, 0.000000),
)
assert len(CIE_1931_2DEG_5NM) == 81  # 80 intervals -> ready for Simpson's rule


# -- sRGB linear transform, D65 white (IEC 61966-2-1) ------------------------
# Standard sRGB primaries: R=(0.6400, 0.3300), G=(0.3000, 0.6000),
# B=(0.1500, 0.0600); white D65=(0.31271, 0.32902). The inverse of the
# primary matrix gives the canonical XYZ -> linear-sRGB transform.
SRGB_FROM_XYZ = (
    ( 3.2406255, -1.5372080, -0.4986286),
    (-0.9689307,  1.8757561,  0.0415175),
    ( 0.0557101, -0.2040211,  1.0569959),
)


# -- Computation -------------------------------------------------------------

def planck(wavelength_nm: float, temperature_k: float) -> float:
    """Spectral radiance B(lambda, T) of an ideal blackbody, W/sr/m^3.

    The overall amplitude cancels in the channel-wise normalization
    below; we evaluate the genuine SI expression so the result has
    clear physical meaning.
    """
    wl_m = wavelength_nm * 1e-9
    return (
        (2.0 * H_PLANCK * C_LIGHT * C_LIGHT) / (wl_m ** 5)
        / (math.exp(H_PLANCK * C_LIGHT / (wl_m * K_BOLTZMANN * temperature_k)) - 1.0)
    )


def xyz_for_temperature(temperature_k: float):
    """CIE (X, Y, Z) for a blackbody at T, via composite Simpson's rule
    on the 5 nm CMF tabulation (81 points, 80 intervals)."""
    radiance = [planck(wl, temperature_k) for wl, *_ in CIE_1931_2DEG_5NM]
    n = len(CIE_1931_2DEG_5NM)
    x_sum = y_sum = z_sum = 0.0
    for i in range(n):
        _, xb, yb, zb = CIE_1931_2DEG_5NM[i]
        bi = radiance[i]
        if i == 0 or i == n - 1:
            w = 1.0
        elif i % 2 == 1:
            w = 4.0
        else:
            w = 2.0
        x_sum += w * bi * xb
        y_sum += w * bi * yb
        z_sum += w * bi * zb
    factor = 5.0 / 3.0  # h / 3 with h = 5 nm
    return x_sum * factor, y_sum * factor, z_sum * factor


def xyz_to_linear_srgb(x: float, y: float, z: float):
    m = SRGB_FROM_XYZ
    return (
        m[0][0] * x + m[0][1] * y + m[0][2] * z,
        m[1][0] * x + m[1][1] * y + m[1][2] * z,
        m[2][0] * x + m[2][1] * y + m[2][2] * z,
    )


def srgb_oetf(x: float) -> float:
    """sRGB opto-electronic transfer function (IEC 61966-2-1).

    Maps linear-light values to the gamma-encoded ("electrical") values
    that the OS gamma ramp interprets. CGSetDisplayTransferByTable on
    macOS and SetDeviceGammaRamp on Windows both operate on the
    display-encoded LUT (which the monitor's intrinsic 2.2-ish gamma
    decodes back to light), not on linear-light scalars. Without this
    encoding step the table would be wildly too saturated.
    """
    if x <= 0.0031308:
        return 12.92 * x
    return 1.055 * (x ** (1.0 / 2.4)) - 0.055


def chromaticity_srgb(temperature_k: float):
    """Linear sRGB for the *chromaticity* of a T-Kelvin blackbody.

    The Planck integral's amplitude scales with T^4 (Stefan-Boltzmann);
    that absolute brightness is meaningless for a display gamma ramp.
    We strip it by normalizing XYZ so Y = 1, leaving only the
    chromaticity, then map to linear sRGB.
    """
    x, y, z = xyz_for_temperature(temperature_k)
    return xyz_to_linear_srgb(x / y, 1.0, z / y)


def main() -> None:
    # Reference: linear sRGB at 6500 K (Y-normalized chromaticity). Per
    # von Kries chromatic adaptation, we divide every row's linear sRGB
    # by this reference channel-wise so 6500 K becomes (1, 1, 1) -- the
    # identity gamma ramp.
    ref_r, ref_g, ref_b = chromaticity_srgb(6500.0)

    rows = []
    for kelvin in range(1000, 10_001, 100):
        r, g, b = chromaticity_srgb(float(kelvin))
        # Step 1: channel-wise von Kries adaptation (ratio against 6500 K
        # so 6500 K becomes the identity).
        r /= ref_r
        g /= ref_g
        b /= ref_b
        # Step 2: clip negatives. Blackbody chromaticities below ~1900 K
        # and above ~25 000 K fall outside the sRGB gamut triangle; the
        # negative-channel residue is the unrepresentable spectral
        # content. Clipping to 0 is the standard "in-gamut projection."
        r = max(0.0, r)
        g = max(0.0, g)
        b = max(0.0, b)
        # Step 3: chromaticity-preserving brightness fit. When any
        # channel exceeds 1.0 (the display's max output), scale all
        # three uniformly so max = 1.0. This preserves hue at the cost
        # of luminance, which is the right trade for a gamma ramp:
        # independent per-channel clipping would shift hue toward
        # whichever channel saturates, producing a different perceived
        # color than the user requested. (Standard ICC v4 "relative
        # colorimetric" intent.)
        m = max(r, g, b)
        if m > 1.0:
            r /= m
            g /= m
            b /= m
        # Step 4: encode for the display LUT (sRGB OETF, IEC 61966-2-1).
        # CGSetDisplayTransferByTable on macOS and SetDeviceGammaRamp on
        # Windows expect display-electrical values, which the monitor's
        # ~2.2 TRC decodes back to linear light.
        rows.append((
            kelvin,
            srgb_oetf(r),
            srgb_oetf(g),
            srgb_oetf(b),
        ))

    print("// -- Swift block -- paste into")
    print("//    macos/Sources/KelvinShift/GammaController.swift")
    print("//    (replaces the body of `blackbodyTable`).")
    for k, r, g, b in rows:
        tag = " (D65)" if k == 6500 else ""
        print(f"        ({r:.8f}, {g:.8f}, {b:.8f}), // {k}K{tag}")

    print()
    print("// -- C++ block -- paste into")
    print("//    win32/src/GammaService.cpp")
    print("//    (replaces the body of `kBlackbody`).")
    for k, r, g, b in rows:
        tag = " (D65)" if k == 6500 else ""
        print(f"    {{{r:.8f}f, {g:.8f}f, {b:.8f}f}}, // {k}K{tag}")


if __name__ == "__main__":
    main()
