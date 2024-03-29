/* Copyright (C) 2007-2008 The Android Open Source Project
**
** This software is licensed under the terms of the GNU General Public
** License version 2, as published by the Free Software Foundation, and
** may be copied, distributed, and modified under those terms.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
*/
#ifndef _ANDROID_SKIN_SCALER_H
#define _ANDROID_SKIN_SCALER_H

#include "skin_image.h"

typedef struct SkinScaler   SkinScaler;

/* create a new image scaler. by default, it uses a scale of 1.0 */
extern SkinScaler*  skin_scaler_create( void );

/* change the scale of a given scaler. returns 0 on success, or -1 in case of
 * problem (unsupported scale) */
extern int          skin_scaler_set( SkinScaler*  scaler, double  scale );

extern void         skin_scaler_free( SkinScaler*  scaler );

extern void         skin_scaler_scale( SkinScaler*   scaler,
                                       SDL_Surface*  dst,
                                       SDL_Surface*  src,
                                       int           sx,
                                       int           sy,
                                       int           sw,
                                       int           sh );

#endif /* _ANDROID_SKIN_SCALER_H */
