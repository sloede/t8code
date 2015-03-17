/*
  This file is part of t8code.
  t8code is a C library to manage a collection (a forest) of multiple
  connected adaptive space-trees of general element types in parallel.

  Copyright (C) 2010 The University of Texas System
  Written by Carsten Burstedde, Lucas C. Wilcox, and Tobin Isaac

  t8code is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  t8code is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with t8code; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/

/** \file t8_default_common.h
 */

#ifndef T8_DEFAULT_COMMON_H
#define T8_DEFAULT_COMMON_H

#include <t8_element.h>

/** This type independent function assumes an sc_mempool_t as context.
 * It is suitable as the ts_destroy callback in \ref t8_type_scheme_t.
 * We assume that the mempool has been created with the correct element size.
 * \param [in,out] ts           This type scheme's context is destroyed.
 */
void                t8_default_scheme_mempool_destroy (t8_type_scheme_t * ts);

/** This type independent function assumes an sc_mempool_t as context.
 * It is suitable as the elem_new callback in \ref t8_type_scheme_t.
 * We assume that the mempool has been created with the correct element size.
 * \param [in,out] ts_context   An element is allocated in this sc_mempool_t.
 * \param [in]     length       Non-negative number of elements to allocate.
 * \param [in,out] elem         Array of correct size whose members are filled.
 */
void                t8_default_mempool_alloc (void *ts_context, int length,
                                              t8_element_t ** elem);

/** This type independent function assumes an sc_mempool_t as context.
 * It is suitable as the elem_destroy callback in \ref t8_type_scheme_t.
 * We assume that the mempool has been created with the correct element size.
 * \param [in,out] ts_context   An element is returned to this sc_mempool_t.
 * \param [in]     length       Non-negative number of elements to destroy.
 * \param [in,out] elem         Array whose members are returned to the mempool.
 */
void                t8_default_mempool_free (void *ts_context, int length,
                                             t8_element_t ** elem);

#endif /* !T8_DEFAULT_COMMON_H */