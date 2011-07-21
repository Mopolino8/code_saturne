#ifndef __FVM_DEFS_H__
#define __FVM_DEFS_H__

/*============================================================================
 * Definitions, global variables, and base functions
 *============================================================================*/

/*
  This file is part of the "Finite Volume Mesh" library, intended to provide
  finite volume mesh and associated fields I/O and manipulation services.

  Copyright (C) 2004-2011  EDF

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
extern "C" {
#if 0
} /* Fake brace to force back Emacs auto-indentation back to column 0 */
#endif
#endif /* __cplusplus */

/*=============================================================================
 * Macro definitions
 *============================================================================*/

/* "Classical" macros */
/*--------------------*/

#define FVM_ABS(a)     ((a) <  0  ? -(a) : (a))  /* Absolute value of a */
#define FVM_MIN(a,b)   ((a) > (b) ?  (b) : (a))  /* Minimum of a et b */
#define FVM_MAX(a,b)   ((a) < (b) ?  (b) : (a))  /* Maximum of a et b */

/*============================================================================
 * Type definitions
 *============================================================================*/

/*----------------------------------------------------------------------------
 * Element types
 *----------------------------------------------------------------------------*/

typedef enum {

  FVM_EDGE,               /* Edge */
  FVM_FACE_TRIA,          /* Triangle */
  FVM_FACE_QUAD,          /* Quadrangle */
  FVM_FACE_POLY,          /* Simple Polygon */
  FVM_CELL_TETRA,         /* Tetrahedron */
  FVM_CELL_PYRAM,         /* Pyramid */
  FVM_CELL_PRISM,         /* Prism (pentahedron) */
  FVM_CELL_HEXA,          /* Hexahedron (brick) */
  FVM_CELL_POLY,          /* Simple Polyhedron (convex or quasi-convex) */
  FVM_N_ELEMENT_TYPES     /* Number of element types */

} fvm_element_t;

/*----------------------------------------------------------------------------
 * Variable interlace type:
 * {x1, y1, z1, x2, y2, z2, ...,xn, yn, zn} if interlaced
 * {x1, x2, ..., xn, y1, y2, ..., yn, z1, z2, ..., zn} if non interlaced
 *----------------------------------------------------------------------------*/

typedef enum {

  FVM_INTERLACE,          /* Variable is interlaced */
  FVM_NO_INTERLACE        /* Variable is not interlaced */

} fvm_interlace_t;

/*----------------------------------------------------------------------------
 * Variable value type.
 *----------------------------------------------------------------------------*/

typedef enum {

  FVM_DATATYPE_NULL,      /* empty datatype */
  FVM_CHAR,               /* character values */
  FVM_FLOAT,              /* 4-byte floating point values */
  FVM_DOUBLE,             /* 8-byte floating point values */
  FVM_INT32,              /* 4-byte signed integer values */
  FVM_INT64,              /* 8-byte signed integer values */
  FVM_UINT32,             /* 4-byte unsigned integer values */
  FVM_UINT64              /* 8-byte unsigned integer values */

} fvm_datatype_t;

/*----------------------------------------------------------------------------
 * Basic types used by FVM.
 * They may be modified here to better map to a given library, with the
 * following constraints:
 *  - fvm_lnum_t must be signed
 *  - fvm_gnum_t may be signed or unsigned
 *----------------------------------------------------------------------------*/

typedef cs_gnum_t   fvm_gnum_t;     /* Global integer index or number */
typedef cs_lnum_t   fvm_lnum_t;     /* Local integer index or number */
typedef cs_coord_t  fvm_coord_t;    /* Real number (coordinate value) */

/*=============================================================================
 * Static global variables
 *============================================================================*/

/* Names of (multiple) element types */

extern const char  *fvm_elements_type_name[];

/* Names of (single) element types */

extern const char  *fvm_element_type_name[];

/* Sizes and names associated with datatypes */

extern const size_t  fvm_datatype_size[];
extern const char   *fvm_datatype_name[];

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __FVM_DEFS_H__ */
