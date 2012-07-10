/*====================================================================*
 -  Copyright (C) 2001 Leptonica.  All rights reserved.
 -  This software is distributed in the hope that it will be
 -  useful, but with NO WARRANTY OF ANY KIND.
 -  No author or distributor accepts responsibility to anyone for the
 -  consequences of using this software, or for whether it serves any
 -  particular purpose or works at all, unless he or she says so in
 -  writing.  Everyone is granted permission to copy, modify and
 -  redistribute this source code, for commercial or non-commercial
 -  purposes, with the following restrictions: (1) the origin of this
 -  source code must not be misrepresented; (2) modified versions must
 -  be plainly marked as such; and (3) this notice may not be removed
 -  or altered from any source or modified source distribution.
 *====================================================================*/

/*
 *  grayquant.c
 *                     
 *      Thresholding from 8 bpp to 1 bpp
 *
 *          Floyd-Steinberg dithering to binary
 *              PIX    *pixDitherToBinary()
 *              PIX    *pixDitherToBinarySpec()
 *
 *          Simple (pixelwise) binarization with fixed threshold
 *              PIX    *pixThresholdToBinary()
 *
 *          Binarization with variable threshold
 *              PIX    *pixVarThresholdToBinary()
 *
 *          Slower implementation of Floyd-Steinberg dithering, using LUTs
 *              PIX    *pixDitherToBinaryLUT()
 *
 *          Generate a binary mask from pixels of particular values
 *              PIX    *pixGenerateMaskByValue()
 *              PIX    *pixGenerateMaskByBand()
 *
 *      Thresholding from 8 bpp to 2 bpp
 *
 *          Dithering to 2 bpp
 *              PIX      *pixDitherTo2bpp()
 *              PIX      *pixDitherTo2bppSpec()
 *
 *          Simple (pixelwise) thresholding to 2 bpp with optional cmap
 *              PIX      *pixThresholdTo2bpp()
 *
 *      Simple (pixelwise) thresholding from 8 bpp to 4 bpp
 *              PIX      *pixThresholdTo4bpp()
 *
 *      Simple (pixelwise) quantization on 8 bpp grayscale
 *              PIX      *pixThresholdOn8bpp()
 *
 *      Arbitrary (pixelwise) thresholding from 8 bpp to 2, 4 or 8 bpp
 *              PIX      *pixThresholdGrayArb()
 *
 *      Quantization tables for linear thresholds of grayscale images
 *              l_int32  *makeGrayQuantIndexTable()
 *              l_int32  *makeGrayQuantTargetTable()
 *
 *      Quantization table for arbitrary thresholding of grayscale images
 *              l_int32   makeGrayQuantTableArb()
 *              l_int32   makeGrayQuantColormapArb()
 *
 *      Thresholding from 32 bpp rgb to 1 bpp
 *      (really color quantization, but it's better placed in this file)
 *              PIX      *pixGenerateMaskByBand32()
 *              PIX      *pixGenerateMaskByDiscr32()
 *
 *      Histogram-based grayscale quantization
 *              PIX      *pixGrayQuantFromHisto()
 *       static l_int32   numaFillCmapFromHisto()
 *
 *      Color quantize grayscale image using existing colormap
 *              PIX      *pixGrayQuantFromCmap()
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "allheaders.h"


/*------------------------------------------------------------------*
 *             Binarization by Floyd-Steinberg dithering            *
 *------------------------------------------------------------------*/
/*!
 *  pixDitherToBinary()
 *
 *      Input:  pixs
 *      Return: pixd (dithered binary), or null on error
 *
 *  The Floyd-Steinberg error diffusion dithering algorithm
 *  binarizes an 8 bpp grayscale image to a threshold of 128.
 *  If a pixel has a value above 127, it is binarized to white
 *  and the excess (below 255) is subtracted from three 
 *  neighboring pixels in the fractions 3/8 to (i, j+1),
 *  3/8 to (i+1, j) and 1/4 to (i+1,j+1), truncating to 0
 *  if necessary.  Likewise, if it the pixel has a value 
 *  below 128, it is binarized to black and the excess above 0
 *  is added to the neighboring pixels, truncating to 255 if necessary.
 *
 *  This function differs from straight dithering in that it allows
 *  clipping of grayscale to 0 or 255 if the values are
 *  sufficiently close, without distribution of the excess.
 *  This uses default values to specify the range of lower
 *  and upper values (near 0 and 255, rsp) that are clipped
 *  to black and white without propagating the excess.
 *  Not propagating the excess has the effect of reducing the
 *  snake patterns in parts of the image that are nearly black or white;
 *  however, it also prevents the attempt to reproduce gray for those values.
 *
 *  The implementation is straightforward.  It uses a pair of
 *  line buffers to avoid changing pixs.  It is about 2x faster
 *  than the implementation using LUTs.
 */
LEPTONICA_EXPORT PIX *
pixDitherToBinary(PIX  *pixs)
{
    PROCNAME("pixDitherToBinary");

    if (!pixs)
        return (PIX *)ERROR_PTR("pixs not defined", procName, NULL);
    if (pixGetDepth(pixs) != 8)
        return (PIX *)ERROR_PTR("must be 8 bpp for dithering", procName, NULL);

    return pixDitherToBinarySpec(pixs, DEFAULT_CLIP_LOWER_1,
                                 DEFAULT_CLIP_UPPER_1);
}


/*!
 *  pixDitherToBinarySpec()
 *
 *      Input:  pixs
 *              lowerclip (lower clip distance to black; use 0 for default)
 *              upperclip (upper clip distance to white; use 0 for default)
 *      Return: pixd (dithered binary), or null on error
 *
 *  Notes:
 *      (1) See comments above in pixDitherToBinary() for details.
 *      (2) The input parameters lowerclip and upperclip specify the range
 *          of lower and upper values (near 0 and 255, rsp) that are
 *          clipped to black and white without propagating the excess.
 *          For that reason, lowerclip and upperclip should be small numbers.
 */
LEPTONICA_EXPORT PIX *
pixDitherToBinarySpec(PIX     *pixs,
                      l_int32  lowerclip,
                      l_int32  upperclip)
{
l_int32    w, h, d, wplt, wpld;
l_uint32  *datat, *datad;
l_uint32  *bufs1, *bufs2;
PIX       *pixt, *pixd;

    PROCNAME("pixDitherToBinarySpec");

    if (!pixs)
        return (PIX *)ERROR_PTR("pixs not defined", procName, NULL);
    pixGetDimensions(pixs, &w, &h, &d);
    if (d != 8)
        return (PIX *)ERROR_PTR("must be 8 bpp for dithering", procName, NULL);
    if (lowerclip < 0 || lowerclip > 255)
        return (PIX *)ERROR_PTR("invalid value for lowerclip", procName, NULL);
    if (upperclip < 0 || upperclip > 255)
        return (PIX *)ERROR_PTR("invalid value for upperclip", procName, NULL);

    if ((pixd = pixCreate(w, h, 1)) == NULL)
        return (PIX *)ERROR_PTR("pixd not made", procName, NULL);
    pixCopyResolution(pixd, pixs);
    datad = pixGetData(pixd);
    wpld = pixGetWpl(pixd);

        /* Remove colormap if it exists */
    pixt = pixRemoveColormap(pixs, REMOVE_CMAP_TO_GRAYSCALE);
    datat = pixGetData(pixt);
    wplt = pixGetWpl(pixt);

        /* Two line buffers, 1 for current line and 2 for next line */
    if ((bufs1 = (l_uint32 *)CALLOC(wplt, sizeof(l_uint32))) == NULL)
        return (PIX *)ERROR_PTR("bufs1 not made", procName, NULL);
    if ((bufs2 = (l_uint32 *)CALLOC(wplt, sizeof(l_uint32))) == NULL)
        return (PIX *)ERROR_PTR("bufs2 not made", procName, NULL);

    ditherToBinaryLow(datad, w, h, wpld, datat, wplt, bufs1, bufs2,
                      lowerclip, upperclip);

    FREE(bufs1);
    FREE(bufs2);
    pixDestroy(&pixt);

    return pixd;
}


/*------------------------------------------------------------------*
 *       Simple (pixelwise) binarization with fixed threshold       *
 *------------------------------------------------------------------*/
/*!
 *  pixThresholdToBinary()
 *
 *      Input:  pixs (4 or 8 bpp)
 *              threshold value
 *      Return: pixd (1 bpp), or null on error
 *
 *  Notes:
 *      (1) If the source pixel is less than the threshold value,
 *          the dest will be 1; otherwise, it will be 0
 */
LEPTONICA_REAL_EXPORT PIX *
pixThresholdToBinary(PIX     *pixs,
                     l_int32  thresh)
{
l_int32    d, w, h, wplt, wpld;
l_uint32  *datat, *datad;
PIX       *pixt, *pixd;

    PROCNAME("pixThresholdToBinary");

    if (!pixs)
        return (PIX *)ERROR_PTR("pixs not defined", procName, NULL);
    pixGetDimensions(pixs, &w, &h, &d);
    if (d != 4 && d != 8)
        return (PIX *)ERROR_PTR("pixs must be 4 or 8 bpp", procName, NULL);
    if (thresh < 0)
        return (PIX *)ERROR_PTR("thresh must be non-negative", procName, NULL);
    if (d == 4 && thresh > 16)
        return (PIX *)ERROR_PTR("4 bpp thresh not in {0-16}", procName, NULL);
    if (d == 8 && thresh > 256)
        return (PIX *)ERROR_PTR("8 bpp thresh not in {0-256}", procName, NULL);

    if ((pixd = pixCreate(w, h, 1)) == NULL)
        return (PIX *)ERROR_PTR("pixd not made", procName, NULL);
    pixCopyResolution(pixd, pixs);
    datad = pixGetData(pixd);
    wpld = pixGetWpl(pixd);

        /* Remove colormap if it exists.  If there is a colormap,
         * pixt will be 8 bpp regardless of the depth of pixs. */
    pixt = pixRemoveColormap(pixs, REMOVE_CMAP_TO_GRAYSCALE);
    datat = pixGetData(pixt);
    wplt = pixGetWpl(pixt);
    if (pixGetColormap(pixs) && d == 4) {  /* promoted to 8 bpp */
        d = 8;
        thresh *= 16;
    }

    thresholdToBinaryLow(datad, w, h, wpld, datat, d, wplt, thresh);
    pixDestroy(&pixt);
    return pixd;
}


/*------------------------------------------------------------------*
 *                Binarization with variable threshold              *
 *------------------------------------------------------------------*/
/*!
 *  pixVarThresholdToBinary()
 *
 *      Input:  pixs (8 bpp)
 *              pixg (8 bpp; contains threshold values for each pixel)
 *      Return: pixd (1 bpp), or null on error
 *
 *  Notes:
 *      (1) If the pixel in pixs is less than the corresponding pixel
 *          in pixg, the dest will be 1; otherwise it will be 0.
 */
LEPTONICA_EXPORT PIX *
pixVarThresholdToBinary(PIX  *pixs,
                        PIX  *pixg)
{
l_int32    i, j, vals, valg, w, h, d, wpls, wplg, wpld;
l_uint32  *datas, *datag, *datad, *lines, *lineg, *lined;
PIX       *pixd;

    PROCNAME("pixVarThresholdToBinary");

    if (!pixs)
        return (PIX *)ERROR_PTR("pixs not defined", procName, NULL);
    if (!pixg)
        return (PIX *)ERROR_PTR("pixg not defined", procName, NULL);
    if (!pixSizesEqual(pixs, pixg))
        return (PIX *)ERROR_PTR("pix sizes not equal", procName, NULL);
    pixGetDimensions(pixs, &w, &h, &d);
    if (d != 8)
        return (PIX *)ERROR_PTR("pixs must be 8 bpp", procName, NULL);

    pixd = pixCreate(w, h, 1);
    datad = pixGetData(pixd);
    wpld = pixGetWpl(pixd);
    datas = pixGetData(pixs);
    wpls = pixGetWpl(pixs);
    datag = pixGetData(pixg);
    wplg = pixGetWpl(pixg);
    for (i = 0; i < h; i++) {
        lines = datas + i * wpls;
        lineg = datag + i * wplg;
        lined = datad + i * wpld;
        for (j = 0; j < w; j++) {
            vals = GET_DATA_BYTE(lines, j);
            valg = GET_DATA_BYTE(lineg, j);
            if (vals < valg)
                SET_DATA_BIT(lined, j);
        }
    }

    return pixd;
}


/*--------------------------------------------------------------------*
 *    Slower implementation of binarization by dithering using LUTs   *
 *--------------------------------------------------------------------*/
/*!
 *  pixDitherToBinaryLUT()
 *
 *      Input:  pixs
 *              lowerclip (lower clip distance to black; use -1 for default)
 *              upperclip (upper clip distance to white; use -1 for default)
 *      Return: pixd (dithered binary), or null on error
 *
 *  This implementation is deprecated.  You should use pixDitherToBinary().
 *
 *  See comments in pixDitherToBinary()
 *
 *  This implementation additionally uses three lookup tables to
 *  generate the output pixel value and the excess or deficit
 *  carried over to the neighboring pixels.
 */
LEPTONICA_EXPORT PIX *
pixDitherToBinaryLUT(PIX     *pixs,
                     l_int32  lowerclip,
                     l_int32  upperclip)
{
l_int32    w, h, d, wplt, wpld;
l_int32   *tabval, *tab38, *tab14;
l_uint32  *datat, *datad;
l_uint32  *bufs1, *bufs2;
PIX       *pixt, *pixd;

    PROCNAME("pixDitherToBinaryLUT");

    if (!pixs)
        return (PIX *)ERROR_PTR("pixs not defined", procName, NULL);
    pixGetDimensions(pixs, &w, &h, &d);
    if (d != 8)
        return (PIX *)ERROR_PTR("must be 8 bpp for dithering", procName, NULL);
    if (lowerclip < 0)
        lowerclip = DEFAULT_CLIP_LOWER_1;
    if (upperclip < 0)
        upperclip = DEFAULT_CLIP_UPPER_1;

    if ((pixd = pixCreate(w, h, 1)) == NULL)
        return (PIX *)ERROR_PTR("pixd not made", procName, NULL);
    pixCopyResolution(pixd, pixs);
    datad = pixGetData(pixd);
    wpld = pixGetWpl(pixd);

        /* Remove colormap if it exists */
    pixt = pixRemoveColormap(pixs, REMOVE_CMAP_TO_GRAYSCALE);
    datat = pixGetData(pixt);
    wplt = pixGetWpl(pixt);

        /* Two line buffers, 1 for current line and 2 for next line */
    if ((bufs1 = (l_uint32 *)CALLOC(wplt, sizeof(l_uint32))) == NULL)
        return (PIX *)ERROR_PTR("bufs1 not made", procName, NULL);
    if ((bufs2 = (l_uint32 *)CALLOC(wplt, sizeof(l_uint32))) == NULL)
        return (PIX *)ERROR_PTR("bufs2 not made", procName, NULL);

        /* 3 lookup tables: 1-bit value, (3/8)excess, and (1/4)excess */
    make8To1DitherTables(&tabval, &tab38, &tab14, lowerclip, upperclip);

    ditherToBinaryLUTLow(datad, w, h, wpld, datat, wplt, bufs1, bufs2,
                         tabval, tab38, tab14);

    FREE(bufs1);
    FREE(bufs2);
    FREE(tabval);
    FREE(tab38);
    FREE(tab14);
    pixDestroy(&pixt);

    return pixd;
}


/*--------------------------------------------------------------------*
 *       Generate a binary mask from pixels of particular value(s)    *
 *--------------------------------------------------------------------*/
/*!
 *  pixGenerateMaskByValue()
 *
 *      Input:  pixs (4 or 8 bpp, or colormapped)
 *              val (of pixels for which we set 1 in dest)
 *              usecmap (1 to retain cmap values; 0 to convert to gray)
 *      Return: pixd (1 bpp), or null on error
 *
 *  Notes:
 *      (1) @val is the gray value of the pixels that we are selecting.
 *      (2) If pixs is colormapped, @usecmap determines if the colormap
 *          values are used, or if the colormap is removed to gray and
 *          the gray values are used.  For the latter, it generates
 *          an approximate grayscale value for each pixel, and then looks
 *          for gray pixels with the value @val.
 */
LEPTONICA_EXPORT PIX *
pixGenerateMaskByValue(PIX     *pixs,
                       l_int32  val,
                       l_int32  usecmap)
{
l_int32    i, j, w, h, d, wplg, wpld;
l_uint32  *datag, *datad, *lineg, *lined;
PIX       *pixg, *pixd;

    PROCNAME("pixGenerateMaskByValue");

    if (!pixs)
        return (PIX *)ERROR_PTR("pixs not defined", procName, NULL);
    d = pixGetDepth(pixs);
    if (d != 4 && d != 8)
        return (PIX *)ERROR_PTR("not 4 or 8 bpp", procName, NULL);

    if (!usecmap && pixGetColormap(pixs))
        pixg = pixRemoveColormap(pixs, REMOVE_CMAP_TO_GRAYSCALE);
    else
        pixg = pixClone(pixs);
    pixGetDimensions(pixg, &w, &h, &d);
    if (d == 4 && (val < 0 || val > 15)) {
        pixDestroy(&pixg);
        return (PIX *)ERROR_PTR("val out of 4 bpp range", procName, NULL);
    }
    if (d == 8 && (val < 0 || val > 255)) {
        pixDestroy(&pixg);
        return (PIX *)ERROR_PTR("val out of 8 bpp range", procName, NULL);
    }

    pixd = pixCreate(w, h, 1);
    pixCopyResolution(pixd, pixg);
    datag = pixGetData(pixg);
    wplg = pixGetWpl(pixg);
    datad = pixGetData(pixd);
    wpld = pixGetWpl(pixd);
    for (i = 0; i < h; i++) {
        lineg = datag + i * wplg;
        lined = datad + i * wpld;
        for (j = 0; j < w; j++) {
            if (d == 4) {
                if (GET_DATA_QBIT(lineg, j) == val)
                    SET_DATA_BIT(lined, j);
            }
            else {  /* d == 8 */
                if (GET_DATA_BYTE(lineg, j) == val)
                    SET_DATA_BIT(lined, j);
            }
        }
    }

    pixDestroy(&pixg);
    return pixd;
}


/*!
 *  pixGenerateMaskByBand()
 *
 *      Input:  pixs (4 or 8 bpp, or colormapped)
 *              lower, upper (two pixel values from which a range, either
 *                            between (inband) or outside of (!inband),
 *                            determines which pixels in pixs cause us to
 *                            set a 1 in the dest mask)
 *              inband (1 for finding pixels in [lower, upper];
 *                      0 for finding pixels in [0, lower) union (upper, 255])
 *              usecmap (1 to retain cmap values; 0 to convert to gray)
 *      Return: pixd (1 bpp), or null on error
 *
 *  Notes:
 *      (1) Generates a 1 bpp mask pixd, the same size as pixs, where
 *          the fg pixels in the mask are those either within the specified
 *          band (for inband == 1) or outside the specified band
 *          (for inband == 0).
 *      (2) If pixs is colormapped, @usecmap determines if the colormap
 *          values are used, or if the colormap is removed to gray and
 *          the gray values are used.  For the latter, it generates
 *          an approximate grayscale value for each pixel, and then looks
 *          for gray pixels with the value @val.
 */
LEPTONICA_EXPORT PIX *
pixGenerateMaskByBand(PIX     *pixs,
                      l_int32  lower,
                      l_int32  upper,
                      l_int32  inband,
                      l_int32  usecmap)
{
l_int32    i, j, w, h, d, wplg, wpld, val;
l_uint32  *datag, *datad, *lineg, *lined;
PIX       *pixg, *pixd;

    PROCNAME("pixGenerateMaskByBand");

    if (!pixs)
        return (PIX *)ERROR_PTR("pixs not defined", procName, NULL);
    d = pixGetDepth(pixs);
    if (d != 4 && d != 8)
        return (PIX *)ERROR_PTR("not 4 or 8 bpp", procName, NULL);
    if (lower < 0 || lower > upper)
        return (PIX *)ERROR_PTR("lower < 0 or lower > upper!", procName, NULL);

    if (!usecmap && pixGetColormap(pixs))
        pixg = pixRemoveColormap(pixs, REMOVE_CMAP_TO_GRAYSCALE);
    else
        pixg = pixClone(pixs);
    pixGetDimensions(pixg, &w, &h, &d);
    if (d == 4 && upper > 15) {
        pixDestroy(&pixg);
        return (PIX *)ERROR_PTR("d == 4 and upper > 15", procName, NULL);
    }
    if (d == 8 && upper > 255) {
        pixDestroy(&pixg);
        return (PIX *)ERROR_PTR("d == 8 and upper > 255", procName, NULL);
    }

    pixd = pixCreate(w, h, 1);
    pixCopyResolution(pixd, pixg);
    datag = pixGetData(pixg);
    wplg = pixGetWpl(pixg);
    datad = pixGetData(pixd);
    wpld = pixGetWpl(pixd);
    for (i = 0; i < h; i++) {
        lineg = datag + i * wplg;
        lined = datad + i * wpld;
        for (j = 0; j < w; j++) {
            if (d == 4)
                val = GET_DATA_QBIT(lineg, j);
            else  /* d == 8 */
                val = GET_DATA_BYTE(lineg, j);
            if (inband) {
                if (val >= lower && val <= upper)
                    SET_DATA_BIT(lined, j);
            } 
            else {  /* out of band */
                if (val < lower || val > upper)
                    SET_DATA_BIT(lined, j);
            }
        }
    }

    pixDestroy(&pixg);
    return pixd;
}


/*------------------------------------------------------------------*
 *                Thresholding to 2 bpp by dithering                *
 *------------------------------------------------------------------*/
/*!
 *  pixDitherTo2bpp()
 *
 *      Input:  pixs (8 bpp)
 *              cmapflag (1 to generate a colormap)
 *      Return: pixd (dithered 2 bpp), or null on error
 *
 *  An analog of the Floyd-Steinberg error diffusion dithering
 *  algorithm is used to "dibitize" an 8 bpp grayscale image
 *  to 2 bpp, using equally spaced gray values of 0, 85, 170, and 255,
 *  which are served by thresholds of 43, 128 and 213.
 *  If cmapflag == 1, the colormap values are set to 0, 85, 170 and 255.
 *  If a pixel has a value between 0 and 42, it is dibitized
 *  to 0, and the excess (above 0) is added to the
 *  three neighboring pixels, in the fractions 3/8 to (i, j+1),
 *  3/8 to (i+1, j) and 1/4 to (i+1, j+1), truncating to 255 if
 *  necessary.  If a pixel has a value between 43 and 127, it is
 *  dibitized to 1, and the excess (above 85) is added to the three
 *  neighboring pixels as before.  If the value is below 85, the
 *  excess is subtracted.  With a value between 128
 *  and 212, it is dibitized to 2, with the excess on either side
 *  of 170 distributed as before.  Finally, with a value between
 *  213 and 255, it is dibitized to 3, with the excess (below 255)
 *  subtracted from the neighbors.  We always truncate to 0 or 255.
 *  The details can be seen in the lookup table generation.
 *
 *  This function differs from straight dithering in that it allows
 *  clipping of grayscale to 0 or 255 if the values are
 *  sufficiently close, without distribution of the excess.
 *  This uses default values (from pix.h) to specify the range of lower
 *  and upper values (near 0 and 255, rsp) that are clipped to black
 *  and white without propagating the excess.
 *  Not propagating the excess has the effect of reducing the snake
 *  patterns in parts of the image that are nearly black or white;
 *  however, it also prevents any attempt to reproduce gray for those values.
 *
 *  The implementation uses 3 lookup tables for simplicity, and
 *  a pair of line buffers to avoid modifying pixs.
 */
LEPTONICA_EXPORT PIX *
pixDitherTo2bpp(PIX     *pixs,
                l_int32  cmapflag)
{
    PROCNAME("pixDitherTo2bpp");

    if (!pixs)
        return (PIX *)ERROR_PTR("pixs not defined", procName, NULL);
    if (pixGetDepth(pixs) != 8)
        return (PIX *)ERROR_PTR("must be 8 bpp for dithering", procName, NULL);

    return pixDitherTo2bppSpec(pixs, DEFAULT_CLIP_LOWER_2,
                               DEFAULT_CLIP_UPPER_2, cmapflag);
}


/*!
 *  pixDitherTo2bppSpec()
 *
 *      Input:  pixs (8 bpp)
 *              lowerclip (lower clip distance to black; use 0 for default)
 *              upperclip (upper clip distance to white; use 0 for default)
 *              cmapflag (1 to generate a colormap)
 *      Return: pixd (dithered 2 bpp), or null on error
 *
 *  Notes:
 *      (1) See comments above in pixDitherTo2bpp() for details.
 *      (2) The input parameters lowerclip and upperclip specify the range
 *          of lower and upper values (near 0 and 255, rsp) that are
 *          clipped to black and white without propagating the excess.
 *          For that reason, lowerclip and upperclip should be small numbers.
 */
LEPTONICA_EXPORT PIX *
pixDitherTo2bppSpec(PIX     *pixs,
                    l_int32  lowerclip,
                    l_int32  upperclip,
                    l_int32  cmapflag)
{
l_int32    w, h, d, wplt, wpld;
l_int32   *tabval, *tab38, *tab14;
l_uint32  *datat, *datad;
l_uint32  *bufs1, *bufs2;
PIX       *pixt, *pixd;
PIXCMAP   *cmap;

    PROCNAME("pixDitherTo2bppSpec");

    if (!pixs)
        return (PIX *)ERROR_PTR("pixs not defined", procName, NULL);
    pixGetDimensions(pixs, &w, &h, &d);
    if (d != 8)
        return (PIX *)ERROR_PTR("must be 8 bpp for dithering", procName, NULL);
    if (lowerclip < 0 || lowerclip > 255)
        return (PIX *)ERROR_PTR("invalid value for lowerclip", procName, NULL);
    if (upperclip < 0 || upperclip > 255)
        return (PIX *)ERROR_PTR("invalid value for upperclip", procName, NULL);

    if ((pixd = pixCreate(w, h, 2)) == NULL)
        return (PIX *)ERROR_PTR("pixd not made", procName, NULL);
    pixCopyResolution(pixd, pixs);
    datad = pixGetData(pixd);
    wpld = pixGetWpl(pixd);

        /* If there is a colormap, remove it */
    pixt = pixRemoveColormap(pixs, REMOVE_CMAP_TO_GRAYSCALE);
    datat = pixGetData(pixt);
    wplt = pixGetWpl(pixt);

        /* Two line buffers, 1 for current line and 2 for next line */
    if ((bufs1 = (l_uint32 *)CALLOC(wplt, sizeof(l_uint32))) == NULL)
        return (PIX *)ERROR_PTR("bufs1 not made", procName, NULL);
    if ((bufs2 = (l_uint32 *)CALLOC(wplt, sizeof(l_uint32))) == NULL)
        return (PIX *)ERROR_PTR("bufs2 not made", procName, NULL);

        /* 3 lookup tables: 2-bit value, (3/8)excess, and (1/4)excess */
    make8To2DitherTables(&tabval, &tab38, &tab14, lowerclip, upperclip);

    ditherTo2bppLow(datad, w, h, wpld, datat, wplt, bufs1, bufs2,
                    tabval, tab38, tab14);

    if (cmapflag) {
        cmap = pixcmapCreateLinear(2, 4);
        pixSetColormap(pixd, cmap);
    }

    FREE(bufs1);
    FREE(bufs2);
    FREE(tabval);
    FREE(tab38);
    FREE(tab14);
    pixDestroy(&pixt);

    return pixd;
}


/*--------------------------------------------------------------------*
 *  Simple (pixelwise) thresholding to 2 bpp with optional colormap   *
 *--------------------------------------------------------------------*/
/*!
 *  pixThresholdTo2bpp()
 *
 *      Input:  pixs (8 bpp)
 *              nlevels (equally spaced; must be between 2 and 4)
 *              cmapflag (1 to build colormap; 0 otherwise)
 *      Return: pixd (2 bpp, optionally with colormap), or null on error
 *
 *  Notes:
 *      (1) Valid values for nlevels is the set {2, 3, 4}.
 *      (2) Any colormap on the input pixs is removed to 8 bpp grayscale.
 *      (3) This function is typically invoked with cmapflag == 1.
 *          In the situation where no colormap is desired, nlevels is
 *          ignored and pixs is thresholded to 4 levels.
 *      (4) The target output colors are equally spaced, with the
 *          darkest at 0 and the lightest at 255.  The thresholds are
 *          chosen halfway between adjacent output values.  A table
 *          is built that specifies the mapping from src to dest.
 *      (5) If cmapflag == 1, a colormap of size 'nlevels' is made,
 *          and the pixel values in pixs are replaced by their
 *          appropriate color indices.  The number of holdouts,
 *          4 - nlevels, will be between 0 and 2.
 *      (6) If you don't want the thresholding to be equally spaced,
 *          either first transform the 8 bpp src using pixGammaTRC().
 *          or, if cmapflag == 1, after calling this function you can use
 *          pixcmapResetColor() to change any individual colors.
 *      (7) If a colormap is generated, it will specify (to display
 *          programs) exactly how each level is to be represented in RGB
 *          space.  When representing text, 3 levels is far better than
 *          2 because of the antialiasing of the single gray level,
 *          and 4 levels (black, white and 2 gray levels) is getting
 *          close to the perceptual quality of a (nearly continuous)
 *          grayscale image.  With 2 bpp, you can set up a colormap
 *          and allocate from 2 to 4 levels to represent antialiased text.
 *          Any left over colormap entries can be used for coloring regions.
 *          For the same number of levels, the file size of a 2 bpp image
 *          is about 10% smaller than that of a 4 bpp result for the same
 *          number of levels.  For both 2 bpp and 4 bpp, using 4 levels you
 *          get compression far better than that of jpeg, because the
 *          quantization to 4 levels will remove the jpeg ringing in the
 *          background near character edges.
 */
LEPTONICA_EXPORT PIX *
pixThresholdTo2bpp(PIX     *pixs,
                   l_int32  nlevels,
                   l_int32  cmapflag)
{
l_int32   *qtab;
l_int32    w, h, d, wplt, wpld;
l_uint32  *datat, *datad;
PIX       *pixt, *pixd;
PIXCMAP   *cmap;

    PROCNAME("pixThresholdTo2bpp");

    if (!pixs)
        return (PIX *)ERROR_PTR("pixs not defined", procName, NULL);
    pixGetDimensions(pixs, &w, &h, &d);
    if (d != 8)
        return (PIX *)ERROR_PTR("pixs not 8 bpp", procName, NULL);
    if (nlevels < 2 || nlevels > 4)
        return (PIX *)ERROR_PTR("nlevels not in {2, 3, 4}", procName, NULL);

        /* Make the appropriate table */
    if (cmapflag)
        qtab = makeGrayQuantIndexTable(nlevels);
    else
        qtab = makeGrayQuantTargetTable(4, 2);

    if ((pixd = pixCreate(w, h, 2)) == NULL)
        return (PIX *)ERROR_PTR("pixd not made", procName, NULL);
    pixCopyResolution(pixd, pixs);
    datad = pixGetData(pixd);
    wpld = pixGetWpl(pixd);

    if (cmapflag) {   /* hold out (4 - nlevels) cmap entries */
        cmap = pixcmapCreateLinear(2, nlevels);
        pixSetColormap(pixd, cmap);
    }

        /* If there is a colormap in the src, remove it */
    pixt = pixRemoveColormap(pixs, REMOVE_CMAP_TO_GRAYSCALE);
    datat = pixGetData(pixt);
    wplt = pixGetWpl(pixt);

    thresholdTo2bppLow(datad, h, wpld, datat, wplt, qtab);

    if (qtab) FREE(qtab);
    pixDestroy(&pixt);
    return pixd;
}


/*----------------------------------------------------------------------*
 *               Simple (pixelwise) thresholding to 4 bpp               *
 *----------------------------------------------------------------------*/
/*!
 *  pixThresholdTo4bpp()
 *
 *      Input:  pixs (8 bpp, can have colormap)
 *              nlevels (equally spaced; must be between 2 and 16)
 *              cmapflag (1 to build colormap; 0 otherwise)
 *      Return: pixd (4 bpp, optionally with colormap), or null on error
 *
 *  Notes:
 *      (1) Valid values for nlevels is the set {2, ... 16}.
 *      (2) Any colormap on the input pixs is removed to 8 bpp grayscale.
 *      (3) This function is typically invoked with cmapflag == 1.
 *          In the situation where no colormap is desired, nlevels is
 *          ignored and pixs is thresholded to 16 levels.
 *      (4) The target output colors are equally spaced, with the
 *          darkest at 0 and the lightest at 255.  The thresholds are
 *          chosen halfway between adjacent output values.  A table
 *          is built that specifies the mapping from src to dest.
 *      (5) If cmapflag == 1, a colormap of size 'nlevels' is made,
 *          and the pixel values in pixs are replaced by their
 *          appropriate color indices.  The number of holdouts,
 *          16 - nlevels, will be between 0 and 14.
 *      (6) If you don't want the thresholding to be equally spaced,
 *          either first transform the 8 bpp src using pixGammaTRC().
 *          or, if cmapflag == 1, after calling this function you can use
 *          pixcmapResetColor() to change any individual colors.
 *      (7) If a colormap is generated, it will specify, to display
 *          programs, exactly how each level is to be represented in RGB
 *          space.  When representing text, 3 levels is far better than
 *          2 because of the antialiasing of the single gray level,
 *          and 4 levels (black, white and 2 gray levels) is getting
 *          close to the perceptual quality of a (nearly continuous)
 *          grayscale image.  Therefore, with 4 bpp, you can set up a
 *          colormap, allocate a relatively small fraction of the 16
 *          possible values to represent antialiased text, and use the
 *          other colormap entries for other things, such as coloring
 *          text or background.  Two other reasons for using a small number
 *          of gray values for antialiased text are (1) PNG compression
 *          gets worse as the number of levels that are used is increased,
 *          and (2) using a small number of levels will filter out most of
 *          the jpeg ringing that is typically introduced near sharp edges
 *          of text.  This filtering is partly responsible for the improved
 *          compression.
 */
LEPTONICA_EXPORT PIX *
pixThresholdTo4bpp(PIX     *pixs,
                   l_int32  nlevels,
                   l_int32  cmapflag)
{
l_int32   *qtab;
l_int32    w, h, d, wplt, wpld;
l_uint32  *datat, *datad;
PIX       *pixt, *pixd;
PIXCMAP   *cmap;

    PROCNAME("pixThresholdTo4bpp");

    if (!pixs)
        return (PIX *)ERROR_PTR("pixs not defined", procName, NULL);
    pixGetDimensions(pixs, &w, &h, &d);
    if (d != 8)
        return (PIX *)ERROR_PTR("pixs not 8 bpp", procName, NULL);
    if (nlevels < 2 || nlevels > 16)
        return (PIX *)ERROR_PTR("nlevels not in [2,...,16]", procName, NULL);

        /* Make the appropriate table */
    if (cmapflag)
        qtab = makeGrayQuantIndexTable(nlevels);
    else
        qtab = makeGrayQuantTargetTable(16, 4);

    if ((pixd = pixCreate(w, h, 4)) == NULL)
        return (PIX *)ERROR_PTR("pixd not made", procName, NULL);
    pixCopyResolution(pixd, pixs);
    datad = pixGetData(pixd);
    wpld = pixGetWpl(pixd);

    if (cmapflag) {   /* hold out (16 - nlevels) cmap entries */
        cmap = pixcmapCreateLinear(4, nlevels);
        pixSetColormap(pixd, cmap);
    }

        /* If there is a colormap in the src, remove it */
    pixt = pixRemoveColormap(pixs, REMOVE_CMAP_TO_GRAYSCALE);
    datat = pixGetData(pixt);
    wplt = pixGetWpl(pixt);

    thresholdTo4bppLow(datad, h, wpld, datat, wplt, qtab);

    if (qtab) FREE(qtab);
    pixDestroy(&pixt);
    return pixd;
}


/*----------------------------------------------------------------------*
 *    Simple (pixelwise) thresholding on 8 bpp with optional colormap   *
 *----------------------------------------------------------------------*/
/*!
 *  pixThresholdOn8bpp()
 *
 *      Input:  pixs (8 bpp, can have colormap)
 *              nlevels (equally spaced; must be between 2 and 256)
 *              cmapflag (1 to build colormap; 0 otherwise)
 *      Return: pixd (8 bpp, optionally with colormap), or null on error
 *
 *  Notes:
 *      (1) Valid values for nlevels is the set {2,...,256}.
 *      (2) Any colormap on the input pixs is removed to 8 bpp grayscale.
 *      (3) If cmapflag == 1, a colormap of size 'nlevels' is made,
 *          and the pixel values in pixs are replaced by their
 *          appropriate color indices.  Otherwise, the pixel values
 *          are the actual thresholded (i.e., quantized) grayscale values.
 *      (4) If you don't want the thresholding to be equally spaced,
 *          first transform the input 8 bpp src using pixGammaTRC().
 */
LEPTONICA_EXPORT PIX *
pixThresholdOn8bpp(PIX     *pixs,
                   l_int32  nlevels,
                   l_int32  cmapflag)
{
l_int32   *qtab;  /* quantization table */
l_int32    i, j, w, h, wpld, val, newval;
l_uint32  *datad, *lined;
PIX       *pixd;
PIXCMAP   *cmap;

    PROCNAME("pixThresholdOn8bpp");

    if (!pixs)
        return (PIX *)ERROR_PTR("pixs not defined", procName, NULL);
    if (pixGetDepth(pixs) != 8)
        return (PIX *)ERROR_PTR("pixs not 8 bpp", procName, NULL);
    if (nlevels < 2 || nlevels > 256)
        return (PIX *)ERROR_PTR("nlevels not in [2,...,256]", procName, NULL);

    if (cmapflag)
        qtab = makeGrayQuantIndexTable(nlevels);
    else
        qtab = makeGrayQuantTargetTable(nlevels, 8);

        /* Get a new pixd; if there is a colormap in the src, remove it */
    if (pixGetColormap(pixs))
        pixd = pixRemoveColormap(pixs, REMOVE_CMAP_TO_GRAYSCALE);
    else
        pixd = pixCopy(NULL, pixs);

    if (cmapflag) {   /* hold out (256 - nlevels) cmap entries */
        cmap = pixcmapCreateLinear(8, nlevels);
        pixSetColormap(pixd, cmap);
    }

    pixGetDimensions(pixd, &w, &h, NULL);
    datad = pixGetData(pixd);
    wpld = pixGetWpl(pixd);
    for (i = 0; i < h; i++) {
        lined = datad + i * wpld;
        for (j = 0; j < w; j++) {
            val = GET_DATA_BYTE(lined, j);
            newval = qtab[val];
            SET_DATA_BYTE(lined, j, newval);
        }
    }

    if (qtab) FREE(qtab);
    return pixd;
}


/*----------------------------------------------------------------------*
 *     Quantization tables for linear thresholds of grayscale images    *
 *----------------------------------------------------------------------*/
/*!
 *  makeGrayQuantIndexTable()
 *
 *      Input:  nlevels (number of output levels)
 *      Return: table (maps input gray level to colormap index,
 *                     or null on error)
 *  Notes:
 *      (1) 'nlevels' is some number between 2 and 256 (typically 8 or less).
 *      (2) The table is typically used for quantizing 2, 4 and 8 bpp
 *          grayscale src pix, and generating a colormapped dest pix.
 */
LEPTONICA_EXPORT l_int32 *
makeGrayQuantIndexTable(l_int32  nlevels)
{
l_int32   *tab;
l_int32    i, j, thresh;

    PROCNAME("makeGrayQuantIndexTable");

    if ((tab = (l_int32 *)CALLOC(256, sizeof(l_int32))) == NULL)
        return (l_int32 *)ERROR_PTR("calloc fail for tab", procName, NULL);
    for (i = 0; i < 256; i++) {
        for (j = 0; j < nlevels; j++) {
            thresh = 255 * (2 * j + 1) / (2 * nlevels - 2);
            if (i <= thresh) {
                tab[i] = j;
/*                fprintf(stderr, "tab[%d] = %d\n", i, j); */
                break;
            }
        }
    }
    return tab;
}


/*!
 *  makeGrayQuantTargetTable()
 *
 *      Input:  nlevels (number of output levels)
 *              depth (of dest pix, in bpp; 2, 4 or 8 bpp)
 *      Return: table (maps input gray level to thresholded gray level,
 *                     or null on error)
 *
 *  Notes:
 *      (1) nlevels is some number between 2 and 2^(depth)
 *      (2) The table is used in two similar ways:
 *           - for 8 bpp, it quantizes to a given number of target levels
 *           - for 2 and 4 bpp, it thresholds to appropriate target values
 *             that will use the full dynamic range of the dest pix.
 *      (3) For depth = 8, the number of thresholds chosen is
 *          ('nlevels' - 1), and the 'nlevels' values stored in the
 *          table are at the two at the extreme ends, (0, 255), plus
 *          plus ('nlevels' - 2) values chosen at equal intervals between.
 *          For example, for depth = 8 and 'nlevels' = 3, the two
 *          threshold values are 3f and bf, and the three target pixel
 *          values are 0, 7f and ff.
 *      (4) For depth < 8, we ignore nlevels, and always use the maximum
 *          number of levels, which is 2^(depth).
 *          If you want nlevels < the maximum number, you should always
 *          use a colormap.
 */
LEPTONICA_EXPORT l_int32 *
makeGrayQuantTargetTable(l_int32  nlevels,
                         l_int32  depth)
{
l_int32   *tab;
l_int32    i, j, thresh, maxval, quantval;

    PROCNAME("makeGrayQuantTargetTable");

    if ((tab = (l_int32 *)CALLOC(256, sizeof(l_int32))) == NULL)
        return (l_int32 *)ERROR_PTR("calloc fail for tab", procName, NULL);
    maxval = (1 << depth) - 1;
    if (depth < 8)
        nlevels = 1 << depth;
    for (i = 0; i < 256; i++) {
        for (j = 0; j < nlevels; j++) {
            thresh = 255 * (2 * j + 1) / (2 * nlevels - 2);
            if (i <= thresh) {
                quantval = maxval * j / (nlevels - 1);
                tab[i] = quantval;
/*                fprintf(stderr, "tab[%d] = %d\n", i, tab[i]); */
                break;
            }
        }
    }
    return tab;
}


/*----------------------------------------------------------------------*
 *   Quantization table for arbitrary thresholding of grayscale images  *
 *----------------------------------------------------------------------*/
/*!
 *  makeGrayQuantTableArb()
 *
 *      Input:  na (numa of bin boundaries)
 *              outdepth (of colormap: 1, 2, 4 or 8)
 *              &tab (<return> table mapping input gray level to cmap index)
 *              &cmap (<return> colormap)
 *      Return: 0 if OK, 1 on error
 *
 *  Notes:
 *      (1) The number of bins is the count of @na + 1.
 *      (2) The bin boundaries in na must be sorted in increasing order.
 *      (3) The table is an inverse colormap: it maps input gray level
 *          to colormap index (the bin number).
 *      (4) The colormap generated here has quantized values at the
 *          center of each bin.  If you want to use the average gray
 *          value of pixels within the bin, discard the colormap and
 *          compute it using makeGrayQuantColormapArb().
 *      (5) Returns an error if there are not enough levels in the
 *          output colormap for the number of bins.  The number
 *          of bins must not exceed 2^outdepth.
 */
LEPTONICA_EXPORT l_int32
makeGrayQuantTableArb(NUMA      *na,
                      l_int32    outdepth,
                      l_int32  **ptab,
                      PIXCMAP  **pcmap)
{
l_int32   i, j, n, jstart, ave, val;
l_int32  *tab;
PIXCMAP  *cmap;

    PROCNAME("makeGrayQuantTableArb");

    if (!ptab)
        return ERROR_INT("&tab not defined", procName, 1);
    *ptab = NULL;
    if (!pcmap)
        return ERROR_INT("&cmap not defined", procName, 1);
    *pcmap = NULL;
    if (!na)
        return ERROR_INT("na not defined", procName, 1);
    n = numaGetCount(na);
    if (n + 1 > (1 << outdepth))
        return ERROR_INT("more bins than cmap levels", procName, 1);

    if ((tab = (l_int32 *)CALLOC(256, sizeof(l_int32))) == NULL)
        return ERROR_INT("calloc fail for tab", procName, 1);
    if ((cmap = pixcmapCreate(outdepth)) == NULL)
        return ERROR_INT("cmap not made", procName, 1);
    *ptab = tab;
    *pcmap = cmap;

        /* First n bins */
    jstart = 0;
    for (i = 0; i < n; i++) {
        numaGetIValue(na, i, &val);
        ave = (jstart + val) / 2;
        pixcmapAddColor(cmap, ave, ave, ave);
        for (j = jstart; j < val; j++)
            tab[j] = i;
        jstart = val;
    }

        /* Last bin */
    ave = (jstart + 255) / 2;
    pixcmapAddColor(cmap, ave, ave, ave);
    for (j = jstart; j < 256; j++) 
        tab[j] = n;

    return 0;
}


/*!
 *  makeGrayQuantColormapArb()
 *
 *      Input:  pixs (8 bpp)
 *              tab (table mapping input gray level to cmap index)
 *              outdepth (of colormap: 1, 2, 4 or 8)
 *              &cmap (<return> colormap)
 *      Return: 0 if OK, 1 on error
 *
 *  Notes:
 *      (1) The table is a 256-entry inverse colormap: it maps input gray
 *          level to colormap index (the bin number).  It is computed
 *          using makeGrayQuantTableArb().
 *      (2) The colormap generated here has quantized values at the
 *          average gray value of the pixels that are in each bin.
 *      (3) Returns an error if there are not enough levels in the
 *          output colormap for the number of bins.  The number
 *          of bins must not exceed 2^outdepth.
 */
LEPTONICA_EXPORT l_int32
makeGrayQuantColormapArb(PIX       *pixs,
                         l_int32   *tab,
                         l_int32    outdepth,
                         PIXCMAP  **pcmap)
{
l_int32    i, j, index, w, h, d, nbins, wpl, factor, val;
l_int32   *bincount, *binave, *binstart;
l_uint32  *line, *data;

    PROCNAME("makeGrayQuantColormapArb");

    if (!pcmap)
        return ERROR_INT("&cmap not defined", procName, 1);
    *pcmap = NULL;
    if (!pixs)
        return ERROR_INT("pixs not defined", procName, 1);
    pixGetDimensions(pixs, &w, &h, &d);
    if (d != 8)
        return ERROR_INT("pixs not 8 bpp", procName, 1);
    if (!tab)
        return ERROR_INT("tab not defined", procName, 1);
    nbins = tab[255] + 1;
    if (nbins > (1 << outdepth))
        return ERROR_INT("more bins than cmap levels", procName, 1);

        /* Find the count and weighted count for each bin */
    if ((bincount = (l_int32 *)CALLOC(nbins, sizeof(l_int32))) == NULL)
        return ERROR_INT("calloc fail for bincount", procName, 1);
    if ((binave = (l_int32 *)CALLOC(nbins, sizeof(l_int32))) == NULL)
        return ERROR_INT("calloc fail for binave", procName, 1);
    factor = (l_int32)(sqrt((l_float64)(w * h) / 30000.) + 0.5);
    factor = L_MAX(1, factor);
    data = pixGetData(pixs);
    wpl = pixGetWpl(pixs);
    for (i = 0; i < h; i += factor) {
        line = data + i * wpl;
        for (j = 0; j < w; j += factor) {
            val = GET_DATA_BYTE(line, j);
            bincount[tab[val]]++;
            binave[tab[val]] += val;
        }
    }

        /* Find the smallest gray values in each bin */
    if ((binstart = (l_int32 *)CALLOC(nbins, sizeof(l_int32))) == NULL)
        return ERROR_INT("calloc fail for binstart", procName, 1);
    for (i = 1, index = 1; i < 256; i++) {
        if (tab[i] < index) continue;
        if (tab[i] == index)
            binstart[index++] = i;
    }

        /* Get the averages.  If there are no samples in a bin, use
	 * the center value of the bin. */
    *pcmap = pixcmapCreate(outdepth);
    for (i = 0; i < nbins; i++) {
        if (bincount[i])
            val = binave[i] / bincount[i];
        else {  /* no samples in the bin */
            if (i < nbins - 1)
                val = (binstart[i] + binstart[i + 1]) / 2;
            else  /* last bin */
                val = (binstart[i] + 255) / 2;
        }
        pixcmapAddColor(*pcmap, val, val, val);
    }

    FREE(bincount);
    FREE(binave);
    FREE(binstart);
    return 0;
}
	    

/*--------------------------------------------------------------------*
 *                 Thresholding from 32 bpp rgb to 1 bpp              *
 *--------------------------------------------------------------------*/
/*!
 *  pixGenerateMaskByBand32()
 *
 *      Input:  pixs (32 bpp)
 *              refval (reference rgb value)
 *              delm (max amount below the ref value for any component)
 *              delp (max amount above the ref value for any component)
 *      Return: pixd (1 bpp), or null on error
 *
 *  Notes:
 *      (1) Generates a 1 bpp mask pixd, the same size as pixs, where
 *          the fg pixels in the mask are those where each component
 *          is within -delm to +delp of the reference value.
 */
LEPTONICA_EXPORT PIX *
pixGenerateMaskByBand32(PIX    *pixs,
                        l_uint32  refval,
                        l_int32   delm,
                        l_int32   delp)
{
l_int32    i, j, w, h, d, wpls, wpld;
l_int32    rref, gref, bref, rval, gval, bval;
l_uint32   pixel;
l_uint32  *datas, *datad, *lines, *lined;
PIX       *pixd;

    PROCNAME("pixGenerateMaskByBand32");

    if (!pixs)
        return (PIX *)ERROR_PTR("pixs not defined", procName, NULL);
    pixGetDimensions(pixs, &w, &h, &d);
    if (d != 32)
        return (PIX *)ERROR_PTR("not 32 bpp", procName, NULL);
    if (delm < 0 || delp < 0)
        return (PIX *)ERROR_PTR("delm and delp must be >= 0", procName, NULL);

    extractRGBValues(refval, &rref, &gref, &bref);
    pixd = pixCreate(w, h, 1);
    pixCopyResolution(pixd, pixs);
    datas = pixGetData(pixs);
    wpls = pixGetWpl(pixs);
    datad = pixGetData(pixd);
    wpld = pixGetWpl(pixd);
    for (i = 0; i < h; i++) {
        lines = datas + i * wpls;
        lined = datad + i * wpld;
        for (j = 0; j < w; j++) {
            pixel = lines[j];
            rval = (pixel >> L_RED_SHIFT) & 0xff;
            if (rval < rref - delm || rval > rref + delp)
                continue;
            gval = (pixel >> L_GREEN_SHIFT) & 0xff;
            if (gval < gref - delm || gval > gref + delp)
                continue;
            bval = (pixel >> L_BLUE_SHIFT) & 0xff;
            if (bval < bref - delm || bval > bref + delp)
                continue;
            SET_DATA_BIT(lined, j);
        }
    }

    return pixd;
}


/*!
 *  pixGenerateMaskByDiscr32()
 *
 *      Input:  pixs (32 bpp)
 *              refval1 (reference rgb value)
 *              refval2 (reference rgb value)
 *              distflag (L_MANHATTAN_DISTANCE, L_EUCLIDEAN_DISTANCE)
 *      Return: pixd (1 bpp), or null on error
 *
 *  Notes:
 *      (1) Generates a 1 bpp mask pixd, the same size as pixs, where
 *          the fg pixels in the mask are those where the pixel in pixs
 *          is "closer" to refval1 than to refval2.
 *      (2) "Closer" can be defined in several ways, such as:
 *            - manhattan distance (L1)
 *            - euclidean distance (L2)
 *            - majority vote of the individual components
 *          Here, we have a choice of L1 or L2.
 */
LEPTONICA_EXPORT PIX *
pixGenerateMaskByDiscr32(PIX      *pixs,
                         l_uint32  refval1,
                         l_uint32  refval2,
			 l_int32   distflag)
{
l_int32    i, j, w, h, d, wpls, wpld;
l_int32    rref1, gref1, bref1, rref2, gref2, bref2, rval, gval, bval;
l_uint32   pixel, dist1, dist2;
l_uint32  *datas, *datad, *lines, *lined;
PIX       *pixd;

    PROCNAME("pixGenerateMaskByDiscr32");

    if (!pixs)
        return (PIX *)ERROR_PTR("pixs not defined", procName, NULL);
    pixGetDimensions(pixs, &w, &h, &d);
    if (d != 32)
        return (PIX *)ERROR_PTR("not 32 bpp", procName, NULL);
    if (distflag != L_MANHATTAN_DISTANCE && distflag != L_EUCLIDEAN_DISTANCE)
        return (PIX *)ERROR_PTR("invalid distflag", procName, NULL);

    extractRGBValues(refval1, &rref1, &gref1, &bref1);
    extractRGBValues(refval2, &rref2, &gref2, &bref2);
    pixd = pixCreate(w, h, 1);
    pixCopyResolution(pixd, pixs);
    datas = pixGetData(pixs);
    wpls = pixGetWpl(pixs);
    datad = pixGetData(pixd);
    wpld = pixGetWpl(pixd);
    for (i = 0; i < h; i++) {
        lines = datas + i * wpls;
        lined = datad + i * wpld;
        for (j = 0; j < w; j++) {
            pixel = lines[j];
            extractRGBValues(pixel, &rval, &gval, &bval);
            if (distflag == L_MANHATTAN_DISTANCE) {
                dist1 = L_ABS(rref1 - rval);
                dist2 = L_ABS(rref2 - rval);
                dist1 += L_ABS(gref1 - gval);
                dist2 += L_ABS(gref2 - gval);
                dist1 += L_ABS(bref1 - bval);
                dist2 += L_ABS(bref2 - bval);
            }
            else {
                dist1 = (rref1 - rval) * (rref1 - rval);
                dist2 = (rref2 - rval) * (rref2 - rval);
                dist1 += (gref1 - gval) * (gref1 - gval);
                dist2 += (gref2 - gval) * (gref2 - gval);
                dist1 += (bref1 - bval) * (bref1 - bval);
                dist2 += (bref2 - bval) * (bref2 - bval);
            }
            if (dist1 < dist2)
                SET_DATA_BIT(lined, j);
        }
    }

    return pixd;
}


/*----------------------------------------------------------------------*
 *        Color quantize grayscale image using existing colormap        *
 *----------------------------------------------------------------------*/
/*!
 *  pixGrayQuantFromCmap()
 *
 *      Input:  pixs (8 bpp grayscale without cmap)
 *              cmap (to quantize to; of dest pix)
 *              mindepth (minimum depth of pixd: can be 2, 4 or 8 bpp)
 *      Return: pixd (2, 4 or 8 bpp, colormapped), or null on error
 *
 *  Notes:
 *      (1) In use, pixs is an 8 bpp grayscale image without a colormap.
 *          If there is an existing colormap, a warning is issued and
 *          a copy of the input pixs is returned.
 */
LEPTONICA_EXPORT PIX *
pixGrayQuantFromCmap(PIX      *pixs,
                     PIXCMAP  *cmap,
                     l_int32   mindepth)
{
l_int32    i, j, index, w, h, d, depth, wpls, wpld;
l_int32    hascolor, vals, vald;
l_int32   *tab;
l_uint32  *datas, *datad, *lines, *lined;
PIXCMAP   *cmapd;
PIX       *pixd;

    PROCNAME("pixGrayQuantFromCmap");

    if (!pixs)
        return (PIX *)ERROR_PTR("pixs not defined", procName, NULL);
    if (pixGetColormap(pixs) != NULL) {
        L_WARNING("pixs already has a colormap; returning a copy", procName);
        return pixCopy(NULL, pixs);
    }
    pixGetDimensions(pixs, &w, &h, &d);
    if (d != 8)
        return (PIX *)ERROR_PTR("pixs not 8 bpp", procName, NULL);
    if (!cmap)
        return (PIX *)ERROR_PTR("cmap not defined", procName, NULL);
    if (mindepth != 2 && mindepth != 4 && mindepth != 8)
        return (PIX *)ERROR_PTR("invalid mindepth", procName, NULL);

        /* Make sure the colormap is gray */
    pixcmapHasColor(cmap, &hascolor);
    if (hascolor) {
        L_WARNING("Converting colormap colors to gray", procName);
        cmapd = pixcmapColorToGray(cmap, 0.3, 0.5, 0.2);
    }
    else
        cmapd = pixcmapCopy(cmap);

        /* Make LUT into colormap */
    if ((tab = (l_int32 *)CALLOC(256, sizeof(l_int32))) == NULL)
        return (PIX *)ERROR_PTR("tab not made", procName, NULL);
    for (i = 0; i < 256; i++) {
        pixcmapGetNearestGrayIndex(cmapd, i, &index);
        tab[i] = index;
    }

    pixcmapGetMinDepth(cmap, &depth);
    depth = L_MAX(depth, mindepth);
    pixd = pixCreate(w, h, depth); 
    pixSetColormap(pixd, cmapd);
    pixCopyResolution(pixd, pixs);
    pixCopyInputFormat(pixd, pixs);
    datas = pixGetData(pixs);
    datad = pixGetData(pixd);
    wpls = pixGetWpl(pixs);
    wpld = pixGetWpl(pixd);
    for (i = 0; i < h; i++) {
        lines = datas + i * wpls;
        lined = datad + i * wpld;
        for (j = 0; j < w; j++) {
            vals = GET_DATA_BYTE(lines, j);
            vald = tab[vals];
            if (depth == 2)
                SET_DATA_DIBIT(lined, j, vald);
            else if (depth == 4)
                SET_DATA_QBIT(lined, j, vald);
            else  /* depth == 8 */
                SET_DATA_BYTE(lined, j, vald);
        }
    }

    FREE(tab);
    return pixd;
}


