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
 *   numabasic.c
 *
 *      Numa creation, destruction, copy, clone, etc.
 *          NUMA        *numaCreate()
 *          NUMA        *numaCreateFromIArray()
 *          NUMA        *numaCreateFromFArray()
 *          void        *numaDestroy()
 *          NUMA        *numaCopy()
 *          NUMA        *numaClone()
 *          l_int32      numaEmpty()
 *
 *      Add/remove number (float or integer)
 *          l_int32      numaAddNumber()
 *          l_int32      numaExtendArray()
 *          l_int32      numaInsertNumber()
 *          l_int32      numaRemoveNumber()
 *          l_int32      numaReplaceNumber()
 *
 *      Numa accessors
 *          l_int32      numaGetCount()
 *          l_int32      numaSetCount()
 *          l_int32      numaGetIValue()
 *          l_int32      numaGetFValue()
 *          l_int32      numaSetValue()
 *          l_int32      numaShiftValue()
 *          l_int32     *numaGetIArray()
 *          l_float32   *numaGetFArray()
 *          l_int32      numaGetRefcount()
 *          l_int32      numaChangeRefcount()
 *          l_int32      numaGetXParameters()
 *          l_int32      numaSetXParameters()
 *          l_int32      numaCopyXParameters()
 *
 *      Serialize numa for I/O
 *          l_int32      numaRead()
 *          l_int32      numaReadStream()
 *          l_int32      numaWrite()
 *          l_int32      numaWriteStream()
 *
 *      Numaa creation, destruction
 *          NUMAA       *numaaCreate()
 *          void        *numaaDestroy()
 *
 *      Add Numa to Numaa
 *          l_int32      numaaAddNuma()
 *          l_int32      numaaExtendArray()
 *
 *      Numaa accessors
 *          l_int32      numaaGetCount()
 *          l_int32      numaaGetNumaCount()
 *          l_int32      numaaGetNumberCount()
 *          NUMA       **numaaGetPtrArray()
 *          NUMA        *numaaGetNuma()
 *          NUMA        *numaaReplaceNuma()
 *          l_int32      numaaGetValue()
 *          l_int32      numaaAddNumber()
 *
 *      Serialize numaa for I/O
 *          l_int32      numaaRead()
 *          l_int32      numaaReadStream()
 *          l_int32      numaaWrite()
 *          l_int32      numaaWriteStream()
 *
 *      Numa2d creation, destruction
 *          NUMA2D      *numa2dCreate()
 *          void        *numa2dDestroy()
 *
 *      Numa2d Accessors
 *          l_int32      numa2dAddNumber()
 *          l_int32      numa2dGetCount()
 *          NUMA        *numa2dGetNuma()
 *          l_int32      numa2dGetFValue()
 *          l_int32      numa2dGetIValue()
 *
 *      NumaHash creation, destruction
 *          NUMAHASH    *numaHashCreate()
 *          void        *numaHashDestroy()
 *
 *      NumaHash Accessors
 *          NUMA        *numaHashGetNuma()
 *          void        *numaHashAdd()
 *
 *    (1) The numa is a struct, not an array.  Always use the accessors
 *        in this file, never the fields directly.
 *
 *    (2) The number array holds l_float32 values.  It can also
 *        be used to store l_int32 values.
 *
 *    (3) Storing and retrieving numbers:
 *
 *       * to append a new number to the array, use numaAddNumber().  If
 *         the number is an int, it will will automatically be converted
 *         to l_float32 and stored.
 *
 *       * to reset a value stored in the array, use numaSetValue().
 *
 *       * to increment or decrement a value stored in the array,
 *         use numaShiftValue().
 *
 *       * to obtain a value from the array, use either numaGetIValue()
 *         or numaGetFValue(), depending on whether you are retrieving
 *         an integer or a float.  This avoids doing an explicit cast,
 *         such as
 *           (a) return a l_float32 and cast it to an l_int32
 *           (b) cast the return directly to (l_float32 *) to
 *               satisfy the function prototype, as in
 *                 numaGetFValue(na, index, (l_float32 *)&ival);   [ugly!]
 *
 *    (4) int <--> float conversions:
 *
 *        Tradition dictates that type conversions go automatically from
 *        l_int32 --> l_float32, even though it is possible to lose
 *        precision for large integers, whereas you must cast (l_int32)
 *        to go from l_float32 --> l_int32 because you're truncating
 *        to the integer value.
 *
 *    (5) As with other arrays in leptonica, the numa has both an allocated
 *        size and a count of the stored numbers.  When you add a number, it
 *        goes on the end of the array, and causes a realloc if the array
 *        is already filled.  However, in situations where you want to
 *        add numbers randomly into an array, such as when you build a
 *        histogram, you must set the count of stored numbers in advance.
 *        This is done with numaSetCount().  If you set a count larger
 *        than the allocated array, it does a realloc to the size requested.
 *
 *    (6) In situations where the data in a numa correspond to a function
 *        y(x), the values can be either at equal spacings in x or at
 *        arbitrary spacings.  For the former, we can represent all x values
 *        by two parameters: startx (corresponding to y[0]) and delx
 *        for the change in x for adjacent values y[i] and y[i+1].
 *        startx and delx are initialized to 0.0 and 1.0, rsp.
 *        For arbitrary spacings, we use a second numa, and the two
 *        numas are typically denoted nay and nax.
 *
 *    (7) The numa is also the basic struct used for histograms.  Every numa
 *        has startx and delx fields, initialized to 0.0 and 1.0, that can
 *        be used to represent the "x" value for the location of the
 *        first bin and the bin width, respectively.  Accessors are the
 *        numa*XParameters() functions.  All functions that make numa
 *        histograms must set these fields properly, and many functions
 *        that use numa histograms rely on the correctness of these values.
 */

#include <string.h>
#include <math.h>
#include "allheaders.h"

static const l_int32 INITIAL_PTR_ARRAYSIZE1 = 50;      /* n'importe quoi */


/*--------------------------------------------------------------------------*
 *               Numa creation, destruction, copy, clone, etc.              *
 *--------------------------------------------------------------------------*/

/*!
 *  numaClone()
 *
 *      Input:  na
 *      Return: ptr to same numa, or null on error
 */
LEPTONICA_EXPORT NUMA *
numaClone(NUMA  *na)
{
    PROCNAME("numaClone");

    if (!na)
        return (NUMA *)ERROR_PTR("na not defined", procName, NULL);

    numaChangeRefcount(na, 1);
    return na;
}

/*--------------------------------------------------------------------------*
 *                 Number array: add number and extend array                *
 *--------------------------------------------------------------------------*/

/*!
 *  numaExtendArray()
 *
 *      Input:  na
 *      Return: 0 if OK, 1 on error
 */
LEPTONICA_EXPORT l_int32
numaExtendArray(NUMA  *na)
{
    PROCNAME("numaExtendArray");

    if (!na)
        return ERROR_INT("na not defined", procName, 1);

    if ((na->array = (l_float32 *)reallocNew((void **)&na->array,
                                sizeof(l_float32) * na->nalloc,
                                2 * sizeof(l_float32) * na->nalloc)) == NULL)
            return ERROR_INT("new ptr array not returned", procName, 1);

    na->nalloc *= 2;
    return 0;
}


/*----------------------------------------------------------------------*
 *                            Numa accessors                            *
 *----------------------------------------------------------------------*/
/*!
 *  numaGetCount()
 *
 *      Input:  na
 *      Return: count, or 0 if no numbers or on error
 */
LEPTONICA_EXPORT l_int32
numaGetCount(NUMA  *na)
{
    PROCNAME("numaGetCount");

    if (!na)
        return ERROR_INT("na not defined", procName, 0);
    return na->n;
}

/*!
 *  numaGetRefCount()
 *
 *      Input:  na
 *      Return: refcount, or UNDEF on error
 */
LEPTONICA_EXPORT l_int32
numaGetRefcount(NUMA  *na)
{
    PROCNAME("numaGetRefcount");

    if (!na)
        return ERROR_INT("na not defined", procName, UNDEF);
    return na->refcount;
}


/*!
 *  numaChangeRefCount()
 *
 *      Input:  na
 *              delta (change to be applied)
 *      Return: 0 if OK, 1 on error
 */
LEPTONICA_EXPORT l_int32
numaChangeRefcount(NUMA    *na,
                   l_int32  delta)
{
    PROCNAME("numaChangeRefcount");

    if (!na)
        return ERROR_INT("na not defined", procName, 1);
    na->refcount += delta;
    return 0;
}


/*--------------------------------------------------------------------------*
 *               Number array hash: Creation and destruction                *
 *--------------------------------------------------------------------------*/
/*!
 *  numaHashCreate()
 *
 *      Input: nbuckets (the number of buckets in the hash table,
 *                       which should be prime.)
 *             initsize (initial size of each allocated numa; 0 for default)
 *      Return: ptr to new nahash, or null on error
 *
 *  Note: actual numa are created only as required by numaHashAdd()
 */
LEPTONICA_EXPORT NUMAHASH *
numaHashCreate(l_int32  nbuckets,
               l_int32  initsize)
{
NUMAHASH  *nahash;

    PROCNAME("numaHashCreate");

    if (nbuckets <= 0)
        return (NUMAHASH *)ERROR_PTR("negative hash size", procName, NULL);
    if ((nahash = (NUMAHASH *)CALLOC(1, sizeof(NUMAHASH))) == NULL)
        return (NUMAHASH *)ERROR_PTR("nahash not made", procName, NULL);
    if ((nahash->numa = (NUMA **)CALLOC(nbuckets, sizeof(NUMA *))) == NULL) {
        FREE(nahash);
        return (NUMAHASH *)ERROR_PTR("numa ptr array not made", procName, NULL);
    }

    nahash->nbuckets = nbuckets;
    nahash->initsize = initsize;
    return nahash;
}


/*!
 *  numaHashDestroy()
 *
 *      Input:  &nahash (<to be nulled, if it exists>)
 *      Return: void
 */
LEPTONICA_EXPORT void
numaHashDestroy(NUMAHASH **pnahash)
{
NUMAHASH  *nahash;
l_int32    i;

    PROCNAME("numaHashDestroy");

    if (pnahash == NULL) {
        L_WARNING("ptr address is NULL!", procName);
        return;
    }

    if ((nahash = *pnahash) == NULL)
        return;

    for (i = 0; i < nahash->nbuckets; i++)
        numaDestroy(&nahash->numa[i]);
    FREE(nahash->numa);
    FREE(nahash);
    *pnahash = NULL;
}


/*--------------------------------------------------------------------------*
 *               Number array hash: Add elements and return numas
 *--------------------------------------------------------------------------*/
/*!
 *  numaHashGetNuma()
 *
 *      Input:  nahash
 *              key  (key to be hashed into a bucket number)
 *      Return: ptr to numa
 */
LEPTONICA_EXPORT NUMA *
numaHashGetNuma(NUMAHASH  *nahash,
                l_uint32   key)
{
l_int32  bucket;
NUMA    *na;

    PROCNAME("numaHashGetNuma");

    if (!nahash)
        return (NUMA *)ERROR_PTR("nahash not defined", procName, NULL);
    bucket = key % nahash->nbuckets;
    na = nahash->numa[bucket];
    if (na)
        return numaClone(na);
    else
        return NULL;
}

/*!
 *  numaHashAdd()
 *
 *      Input:  nahash
 *              key  (key to be hashed into a bucket number)
 *              value  (float value to be appended to the specific numa)
 *      Return: 0 if OK; 1 on error
 */
LEPTONICA_EXPORT l_int32
numaHashAdd(NUMAHASH  *nahash,
            l_uint32   key,
            l_float32  value)
{
l_int32  bucket;
NUMA    *na;

    PROCNAME("numaHashAdd");

    if (!nahash)
        return ERROR_INT("nahash not defined", procName, 1);
    bucket = key % nahash->nbuckets;
    na = nahash->numa[bucket];
    if (!na) {
        if ((na = numaCreate(nahash->initsize)) == NULL)
            return ERROR_INT("na not made", procName, 1);
        nahash->numa[bucket] = na;
    }
    numaAddNumber(na, value);
    return 0;
}

