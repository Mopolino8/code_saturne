/*============================================================================
 * Functions and structures to deal with source term computations
 *============================================================================*/

/*
  This file is part of Code_Saturne, a general-purpose CFD tool.

  Copyright (C) 1998-2017 EDF S.A.

  This program is free software; you can redistribute it and/or modify it under
  the terms of the GNU General Public License as published by the Free Software
  Foundation; either version 2 of the License, or (at your option) any later
  version.

  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
  details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 51 Franklin
  Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/

#include "cs_defs.h"

/*----------------------------------------------------------------------------
 * Standard C library headers
 *----------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <float.h>

/*----------------------------------------------------------------------------
 * Local headers
 *----------------------------------------------------------------------------*/

#include <bft_mem.h>

#include "cs_evaluate.h"
#include "cs_log.h"
#include "cs_math.h"
#include "cs_mesh_location.h"

/*----------------------------------------------------------------------------
 * Header for the current file
 *----------------------------------------------------------------------------*/

#include "cs_source_term.h"

/*----------------------------------------------------------------------------*/

BEGIN_C_DECLS

/*=============================================================================
 * Local Macro definitions and structure definitions
 *============================================================================*/

#define CS_SOURCE_TERM_DBG 0

/*============================================================================
 * Private variables
 *============================================================================*/

static const char _err_empty_st[] =
  " Stop setting an empty cs_source_term_t structure.\n"
  " Please check your settings.\n";

/* Pointer to shared structures (owned by a cs_domain_t structure) */
static const cs_cdo_quantities_t  *cs_cdo_quant;
static const cs_cdo_connect_t  *cs_cdo_connect;
static const cs_time_step_t  *cs_time_step;

/*============================================================================
 * Private function prototypes
 *============================================================================*/

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Allocate and initialize a name (copy or generic name)
 *
 * \param[in] name       input name
 * \param[in] basename   generic name by default if input name is NULL
 * \param[in] id         id related to this name
 *
 * \return a pointer to a new allocated string
 */
/*----------------------------------------------------------------------------*/

static inline char *
_get_name(const char   *name,
          const char   *base_name,
          int           id)
{
  char *n = NULL;

  if (name == NULL) { /* Define a name by default */
    assert(id < 100);
    int len = strlen(base_name) + 4; // 1 + 3 = "_00\n"
    BFT_MALLOC(n, len, char);
    sprintf(n, "%s_%2d", base_name, id);
  }
  else {  /* Copy name */
    int  len = strlen(name) + 1;
    BFT_MALLOC(n, len, char);
    strncpy(n, name, len);
  }

  return n;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Update the mask associated to each cell from the mask related to
 *         the given source term structure
 *
 * \param[in]      st          pointer to a cs_source_term_t structure
 * \param[in]      st_mask     id related to this source term
 * \param[in, out] cell_mask   mask related to each cell to be updated
 */
/*----------------------------------------------------------------------------*/

static void
_set_mask(const cs_source_term_t     *st,
          int                         st_id,
          cs_mask_t                  *cell_mask)
{
  if (st == NULL)
    bft_error(__FILE__, __LINE__, 0, _(_err_empty_st));

  const cs_mask_t  mask = (1 << st_id); // value of the mask for the source term

  if (st->flag & CS_FLAG_FULL_LOC) // All cells are selected
# pragma omp parallel for if (cs_cdo_quant->n_cells > CS_THR_MIN)
    for (cs_lnum_t i = 0; i < cs_cdo_quant->n_cells; i++) cell_mask[i] |= mask;

  else {

    /* Retrieve information from mesh location structures */
    const cs_lnum_t  *n_elts = cs_mesh_location_get_n_elts(st->ml_id);
    const cs_lnum_t  *elt_ids = cs_mesh_location_get_elt_list(st->ml_id);

    assert(elt_ids != NULL); /* Sanity check */
    for (cs_lnum_t i = 0; i < n_elts[0]; i++) cell_mask[elt_ids[i]] |= mask;

  }

}

/*============================================================================
 * Public function prototypes
 *============================================================================*/

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Set shared pointers to main domain members
 *
 * \param[in]      quant      additional mesh quantities struct.
 * \param[in]      connect    pointer to a cs_cdo_connect_t struct.
 * \param[in]      time_step  pointer to a time step structure
 */
/*----------------------------------------------------------------------------*/

void
cs_source_term_set_shared_pointers(const cs_cdo_quantities_t    *quant,
                                   const cs_cdo_connect_t       *connect,
                                   const cs_time_step_t         *time_step)
{
  /* Assign static const pointers */
  cs_cdo_quant = quant;
  cs_cdo_connect = connect;
  cs_time_step = time_step;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Destroy an array of cs_source_term_t structures
 *
 * \param[in]      n_source_terms   number of source terms
 * \param[in, out] source_terms     pointer to a cs_source_term_t structure
 *
 * \return NULL pointer
 */
/*----------------------------------------------------------------------------*/

cs_source_term_t *
cs_source_term_destroy(int                 n_source_terms,
                       cs_source_term_t   *source_terms)
{
  if (source_terms == NULL)
    return source_terms;

  for (int st_id = 0; st_id < n_source_terms; st_id++) {

    cs_source_term_t *st = source_terms + st_id;

    BFT_FREE(st->name);

    if (st->array_desc.state & CS_FLAG_STATE_OWNER) {
      if (st->array != NULL)
        BFT_FREE(st->array);
    }

  }
  BFT_FREE(source_terms);

  return NULL;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Generic way to define a cs_source_term_t structure by value
 *
 * \param[in, out] st        pointer to the cs_source_term_t structure to set
 * \param[in]      st_id     id related to source term to define
 * \param[in]      name      name of the source term
 * \param[in]      var_type  type of variable (scalar, vector...)
 * \param[in]      ml_id     id related to the mesh location
 * \param[in]      flag      metadata related to this source term
 * \param[in]      val       accessor to the value to set
 */
/*----------------------------------------------------------------------------*/

void
cs_source_term_def_by_value(cs_source_term_t    *st,
                            int                  st_id,
                            const char          *name,
                            cs_param_var_type_t  var_type,
                            int                  ml_id,
                            cs_flag_t            flag,
                            const char          *val)
{
  /* Sanity checks */
  if (st == NULL)
    bft_error(__FILE__, __LINE__, 0, _(_err_empty_st));

  assert(ml_id != -1);
  assert(cs_mesh_location_get_type(ml_id) == CS_MESH_LOCATION_CELLS);

  st->name = _get_name(name, "sourceterm", st_id);
  st->ml_id = ml_id;

  st->flag = flag;
  if (cs_mesh_location_get_elt_list(ml_id) == NULL)
    st->flag |= CS_FLAG_FULL_LOC;

  st->def_type = CS_PARAM_DEF_BY_VALUE;
  st->def.get.val = 0.0;
  st->quad_type = CS_QUADRATURE_BARY;
  st->array_desc.location = 0;
  st->array_desc.state = 0;
  st->array = NULL;
  // st->struc is only shared when used

  switch (var_type) {

  case CS_PARAM_VAR_SCAL:
    st->flag |= CS_FLAG_SCALAR;
    cs_param_set_get(CS_PARAM_VAR_SCAL, (const void *)val, &(st->def.get));
    break;

  case CS_PARAM_VAR_VECT:
    st->flag |= CS_FLAG_VECTOR;
    cs_param_set_get(CS_PARAM_VAR_VECT, (const void *)val, &(st->def.get));
    break;

  case CS_PARAM_VAR_TENS:
    st->flag |= CS_FLAG_TENSOR;
    cs_param_set_get(CS_PARAM_VAR_TENS, (const void *)val, &(st->def.get));
    break;

  default:
    bft_error(__FILE__, __LINE__, 0, _(" Invalid type of source term."));
    break;

  } /* switch on variable type */

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Define a cs_source_term_t structure thanks to an analytic function
 *
 * \param[in, out] st        pointer to the cs_source_term_t structure to set
 * \param[in]      st_id     id related to source term to define
 * \param[in]      name      name of the source term
 * \param[in]      var_type  type of variable (scalar, vector...)
 * \param[in]      ml_id     id related to the mesh location
 * \param[in]      flag      metadata related to this source term
 * \param[in]      func      pointer to a function
 */
/*----------------------------------------------------------------------------*/

void
cs_source_term_def_by_analytic(cs_source_term_t     *st,
                               int                   st_id,
                               const char           *name,
                               cs_param_var_type_t   var_type,
                               int                   ml_id,
                               cs_flag_t             flag,
                               cs_analytic_func_t   *func)
{
  /* Sanity checks */
  if (st == NULL)
    bft_error(__FILE__, __LINE__, 0, _(_err_empty_st));

  assert(ml_id != -1);
  assert(cs_mesh_location_get_type(ml_id) == CS_MESH_LOCATION_CELLS);

  st->name = _get_name(name, "sourceterm", st_id);
  st->ml_id = ml_id;

  st->flag = flag;
  if (cs_mesh_location_get_elt_list(ml_id) == NULL)
    st->flag |= CS_FLAG_FULL_LOC;

  st->def_type = CS_PARAM_DEF_BY_ANALYTIC_FUNCTION;
  st->def.analytic = func;
  st->quad_type = CS_QUADRATURE_BARY;
  st->array_desc.location = 0;
  st->array_desc.state = 0;
  st->array = NULL;
  // st->struc is only shared when used

  switch (var_type) {

  case CS_PARAM_VAR_SCAL:
    st->flag |= CS_FLAG_SCALAR;
    break;

  case CS_PARAM_VAR_VECT:
    st->flag |= CS_FLAG_VECTOR;
    break;

  case CS_PARAM_VAR_TENS:
    st->flag |= CS_FLAG_TENSOR;
    break;

  default:
    bft_error(__FILE__, __LINE__, 0, _(" Invalid type of source term."));
    break;

  } /* switch on variable type */

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Define a cs_source_term_t structure thanks to an array of values
 *
 * \param[in, out] st        pointer to the cs_source_term_t structure to set
 * \param[in]      st_id     id related to source term to define
 * \param[in]      name      name of the source term
 * \param[in]      var_type  type of variable (scalar, vector...)
 * \param[in]      ml_id     id related to the mesh location
 * \param[in]      flag      metadata related to this source term
 * \param[in]      desc      description of the main feature of this array
 * \param[in]      array     pointer to an array
 */
/*----------------------------------------------------------------------------*/

void
cs_source_term_def_by_array(cs_source_term_t     *st,
                            int                   st_id,
                            const char           *name,
                            cs_param_var_type_t   var_type,
                            int                   ml_id,
                            cs_flag_t             flag,
                            cs_desc_t             desc,
                            cs_real_t            *array)
{
  /* Sanity checks */
  if (st == NULL)
    bft_error(__FILE__, __LINE__, 0, _(_err_empty_st));

  assert(ml_id != -1);
  assert(cs_mesh_location_get_type(ml_id) == CS_MESH_LOCATION_CELLS);

  st->name = _get_name(name, "sourceterm", st_id);
  st->ml_id = ml_id;

  st->flag = flag;
  if (cs_mesh_location_get_elt_list(ml_id) == NULL)
    st->flag |= CS_FLAG_FULL_LOC;

  st->def_type = CS_PARAM_DEF_BY_ARRAY;
  st->def.get.val = 0.0; // Avoid a warning but not useful in this context
  st->quad_type = CS_QUADRATURE_BARY;
  st->array_desc.location = desc.location;
  st->array_desc.state = desc.state;
  st->array = array;
  // st->struc is only shared when used

  switch (var_type) {

  case CS_PARAM_VAR_SCAL:
    st->flag |= CS_FLAG_SCALAR;
    break;

  case CS_PARAM_VAR_VECT:
    st->flag |= CS_FLAG_VECTOR;
    break;

  case CS_PARAM_VAR_TENS:
    st->flag |= CS_FLAG_TENSOR;
    break;

  default:
    bft_error(__FILE__, __LINE__, 0, _(" Invalid type of source term."));
    break;

  } /* switch on variable type */
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Set advanced parameters which are defined by default in a
 *         source term structure.
 *
 * \param[in, out]  st          pointer to a cs_source_term_t structure
 * \param[in]       quad_type   type of quadrature to use
 */
/*----------------------------------------------------------------------------*/

void
cs_source_term_set_quadrature(cs_source_term_t  *st,
                              cs_quadra_type_t   quad_type)
{
  if (st == NULL)
    bft_error(__FILE__, __LINE__, 0, _(_err_empty_st));

  st->quad_type = quad_type;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Set the default flag related to a source term according to the
 *         numerical scheme chosen for discretizing an equation
 *
 * \param[in]       scheme    numerical scheme used for the discretization
 *
 * \return a default flag
 */
/*----------------------------------------------------------------------------*/

cs_flag_t
cs_source_term_set_default_flag(cs_space_scheme_t   scheme)
{
  cs_flag_t  st_flag = 0;

  switch (scheme) {
  case CS_SPACE_SCHEME_CDOVB:
    st_flag = CS_FLAG_DUAL | CS_FLAG_CELL; // Default
    break;

  case CS_SPACE_SCHEME_CDOFB:
    st_flag = CS_FLAG_PRIMAL | CS_FLAG_CELL; // Default
    break;

  case CS_SPACE_SCHEME_CDOVCB:
  case CS_SPACE_SCHEME_HHO:
    st_flag = CS_FLAG_PRIMAL;
    break;

  default:
    bft_error(__FILE__, __LINE__, 0,
              _(" Invalid numerical scheme to set a source term."));

  }

  return st_flag;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Set advanced parameters which are defined by default in a
 *         source term structure.
 *
 * \param[in, out]  st        pointer to a cs_source_term_t structure
 * \param[in]       flag      CS_FLAG_DUAL or CS_FLAG_PRIMAL
 */
/*----------------------------------------------------------------------------*/

void
cs_source_term_set_reduction(cs_source_term_t     *st,
                             cs_flag_t             flag)
{
  if (st == NULL)
    bft_error(__FILE__, __LINE__, 0, _(_err_empty_st));

  if (st->flag & flag)
    return; // Nothing to do

  cs_flag_t  save_flag = st->flag;

  st->flag = 0;
  /* Set unchanged parts of the existing flag */
  if (save_flag & CS_FLAG_SCALAR) st->flag |= CS_FLAG_SCALAR;
  if (save_flag & CS_FLAG_VECTOR) st->flag |= CS_FLAG_VECTOR;
  if (save_flag & CS_FLAG_TENSOR) st->flag |= CS_FLAG_TENSOR;
  if (save_flag & CS_FLAG_BORDER) st->flag |= CS_FLAG_BORDER;
  if (save_flag & CS_FLAG_BY_CELL) st->flag |= CS_FLAG_BY_CELL;
  if (save_flag & CS_FLAG_FULL_LOC) st->flag |= CS_FLAG_FULL_LOC;

  if (flag & CS_FLAG_DUAL) {
    assert(save_flag & CS_FLAG_PRIMAL);
    if (save_flag & CS_FLAG_VERTEX)
      st->flag |= CS_FLAG_DUAL | CS_FLAG_CELL;
    else
      bft_error(__FILE__, __LINE__, 0,
                " Stop modifying the source term flag.\n"
                " This case is not handled.");
  }
  else if (flag & CS_FLAG_PRIMAL) {
    assert(save_flag & CS_FLAG_DUAL);
    if (save_flag & CS_FLAG_CELL)
      st->flag |= CS_FLAG_PRIMAL | CS_FLAG_VERTEX;
    else
      bft_error(__FILE__, __LINE__, 0,
                " Stop modifying the source term flag.\n"
                " This case is not handled.");
  }
  else
    bft_error(__FILE__, __LINE__, 0,
              " Stop modifying the source term flag.\n"
              " This case is not handled.");
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Get metadata related to the given source term structure
 *
 * \param[in, out]  st          pointer to a cs_source_term_t structure
 *
 * \return the value of the flag related to this source term
 */
/*----------------------------------------------------------------------------*/

cs_flag_t
cs_source_term_get_flag(const cs_source_term_t  *st)
{
  if (st == NULL)
    bft_error(__FILE__, __LINE__, 0, _(_err_empty_st));

  return st->flag;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Get the name related to a cs_source_term_t structure
 *
 * \param[in] st      pointer to a cs_source_term_t structure
 *
 * \return the name of the source term
 */
/*----------------------------------------------------------------------------*/

const char *
cs_source_term_get_name(const cs_source_term_t   *st)
{
  if (st == NULL)
    return NULL;

  return st->name;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Summarize the content of a cs_source_term_t structure
 *
 * \param[in] eqname  name of the related equation
 * \param[in] st      pointer to a cs_source_term_t structure
 */
/*----------------------------------------------------------------------------*/

void
cs_source_term_summary(const char               *eqname,
                       const cs_source_term_t   *st)
{
  const char  *eqn, _eqn[] = "Equation";

  if (eqname == NULL)
    eqn = _eqn;
  else
    eqn = eqname;

  if (st == NULL) {
    cs_log_printf(CS_LOG_SETUP, "  <%s/NULL>\n", eqn);
    return;
  }

  cs_log_printf(CS_LOG_SETUP, "  <%s/%s> type: ", eqn, st->name);
  cs_log_printf(CS_LOG_SETUP,
                " mesh_location: %s\n", cs_mesh_location_get_name(st->ml_id));

  cs_log_printf(CS_LOG_SETUP, "  <%s/%s> Definition: %s\n",
                eqn, st->name, cs_param_get_def_type_name(st->def_type));
  if (st->def_type == CS_PARAM_DEF_BY_ANALYTIC_FUNCTION)
    cs_log_printf(CS_LOG_SETUP, "  <%s/%s> Quadrature: %s\n",
                  eqn, st->name, cs_quadrature_get_type_name(st->quad_type));

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief   Initialize data to build the source terms
 *
 * \param[in]      space_scheme    scheme used to discretize in space
 * \param[in]      n_source_terms  number of source terms
 * \param[in]      source_terms    pointer to the definitions of source terms
 * \param[in, out] compute_source  array of function pointers
 * \param[in, out] sys_flag        metadata about the algebraic system
 * \param[in, out] source_mask     pointer to an array storing in a compact way
 *                                 which source term is defined in a given cell
 */
/*----------------------------------------------------------------------------*/

void
cs_source_term_init(cs_space_scheme_t            space_scheme,
                    const int                    n_source_terms,
                    const cs_source_term_t      *source_terms,
                    cs_source_term_cellwise_t   *compute_source[],
                    cs_flag_t                   *sys_flag,
                    cs_mask_t                   *source_mask[])
{
  if (n_source_terms > CS_N_MAX_SOURCE_TERMS)
    bft_error(__FILE__, __LINE__, 0,
              " Limitation to %d source terms has been reached!",
              CS_N_MAX_SOURCE_TERMS);

  *source_mask = NULL;
  for (short int i = 0; i < CS_N_MAX_SOURCE_TERMS; i++)
    compute_source[i] = NULL;

  if (n_source_terms == 0)
    return;

  bool  need_mask = false;

  for (int st_id = 0; st_id < n_source_terms; st_id++) {

    const cs_source_term_t  *st = source_terms + st_id;

    if (st->flag & CS_FLAG_PRIMAL)
      *sys_flag |= CS_FLAG_SYS_HLOC_CONF | CS_FLAG_SYS_SOURCES_HLOC;

    if ((st->flag & CS_FLAG_FULL_LOC) == 0) // Not defined on the whole mesh
      need_mask = true;

    switch (space_scheme) {

    case CS_SPACE_SCHEME_CDOVB:

      if (st->flag & CS_FLAG_DUAL) {

        switch (st->def_type) {

        case CS_PARAM_DEF_BY_VALUE:
          compute_source[st_id] = cs_source_term_dcsd_by_value;
          break;

        case CS_PARAM_DEF_BY_ANALYTIC_FUNCTION:

          switch (st->quad_type) {
          case CS_QUADRATURE_BARY:
            compute_source[st_id] = cs_source_term_dcsd_bary_by_analytic;
            break;
          case CS_QUADRATURE_BARY_SUBDIV:
            compute_source[st_id] = cs_source_term_dcsd_q1o1_by_analytic;
            break;
          case CS_QUADRATURE_HIGHER:
            compute_source[st_id] = cs_source_term_dcsd_q10o2_by_analytic;
            break;
          case CS_QUADRATURE_HIGHEST:
            compute_source[st_id] = cs_source_term_dcsd_q5o3_by_analytic;
            break;
          default:
            bft_error(__FILE__, __LINE__, 0,
                      " Invalid type of quadrature for computing a source term"
                      " with CDOVB schemes");
          } // quad_type
          break;

        default:
          bft_error(__FILE__, __LINE__, 0,
                    " Invalid type of definition for a source term in CDOVB");
          break;
        } // switch def_type

      }
      else {
        assert(st->flag & CS_FLAG_PRIMAL);

        switch (st->def_type) {

        case CS_PARAM_DEF_BY_VALUE:
          compute_source[st_id] = cs_source_term_pvsp_by_value;
          break;

        case CS_PARAM_DEF_BY_ANALYTIC_FUNCTION:
          compute_source[st_id] = cs_source_term_pvsp_by_analytic;
          break;

        default:
          bft_error(__FILE__, __LINE__, 0,
                    " Invalid type of definition for a source term in CDOVB");
          break;

        } // switch def_type

      } // flag PRIMAL or DUAL
      break; // CDOVB

    case CS_SPACE_SCHEME_CDOVCB:
      if (st->flag & CS_FLAG_DUAL) {

        bft_error(__FILE__, __LINE__, 0,
                  " Invalid type of definition for a source term in CDOVB");

        /* TODO
           case CS_PARAM_DEF_BY_VALUE:
           cs_source_term_vcsd_by_value; --> case CS_QUADRATURE_BARY:

           case CS_PARAM_DEF_BY_ANALYTIC_FUNCTION:
           cs_source_term_vcsd_q1o1_by_analytic; --> case CS_QUADRATURE_BARY:
           cs_source_term_vcsd_q10o2_by_analytic; --> case CS_QUADRATURE_HIGHER:
           cs_source_term_vcsd_q5o3_by_analytic; --> case CS_QUADRATURE_HIGHEST:
        */

      }
      else {
        assert(st->flag & CS_FLAG_PRIMAL);

        switch (st->def_type) {

        case CS_PARAM_DEF_BY_VALUE:
          compute_source[st_id] = cs_source_term_vcsp_by_value;
          break;

        case CS_PARAM_DEF_BY_ANALYTIC_FUNCTION:
          compute_source[st_id] = cs_source_term_vcsp_by_analytic;
          break;

        default:
          bft_error(__FILE__, __LINE__, 0,
                    " Invalid type of definition for a source term in CDOVB");
          break;

        } // switch def_type

      }
      break; // CDOVCB

    default:
      bft_error(__FILE__, __LINE__, 0,
                "Invalid space scheme for setting the source term.");
      break;

    } // Switch on space scheme

  } // Loop on source terms

  if (need_mask) {

    const cs_lnum_t  n_cells = cs_cdo_quant->n_cells;

    /* Initialize mask buffer */
    cs_mask_t  *mask = NULL;
    BFT_MALLOC(mask, n_cells, cs_mask_t);
# pragma omp parallel for if (n_cells > CS_THR_MIN)
    for (int i = 0; i < n_cells; i++) mask[i] = 0;

    for (int st_id = 0; st_id < n_source_terms; st_id++)
      _set_mask(source_terms + st_id, st_id, mask);

    *source_mask = mask;

  } /* Build a tag related to the source terms defined in each cell */

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief   Compute the local contributions of source terms in a cell
 *
 * \param[in]      n_source_terms  number of source terms
 * \param[in]      source_terms    pointer to the definitions of source terms
 * \param[in]      cm              pointer to a cs_cell_mesh_t structure
 * \param[in]      sys_flag        metadata about the algebraic system
 * \param[in]      source_mask     array storing in a compact way which source
 *                                 term is defined in a given cell
 * \param[in]      compute_source  array of function pointers
 * \param[in, out] cb              pointer to a cs_cell_builder_t structure
 * \param[in, out] csys            cellwise algebraic system
 */
/*----------------------------------------------------------------------------*/

void
cs_source_term_compute_cellwise(const int                    n_source_terms,
                                const cs_source_term_t      *source_terms,
                                const cs_cell_mesh_t        *cm,
                                const cs_flag_t              sys_flag,
                                const cs_mask_t             *source_mask,
                                cs_source_term_cellwise_t   *compute_source[],
                                cs_cell_builder_t           *cb,
                                cs_cell_sys_t               *csys)
{
  /* Reset local contributions */
  for (short int i = 0; i < csys->n_dofs; i++) csys->source[i] = 0;

  if ((sys_flag & CS_FLAG_SYS_SOURCETERM) == 0)
    return;

  if (source_mask == NULL) { // All source terms are defined on the whole mesh

    for (short int st_id = 0; st_id < n_source_terms; st_id++) {

      cs_source_term_cellwise_t  *compute = compute_source[st_id];

      /* Contrib is updated inside */
      compute(source_terms + st_id, cm, cb, csys->source);

    } // Loop on source terms

  }
  else { /* Some source terms are only defined on a selection of cells */

    for (short int st_id = 0; st_id < n_source_terms; st_id++) {

      const cs_mask_t  st_mask = (1 << st_id);
      if (source_mask[cm->c_id] & st_mask) {

        cs_source_term_cellwise_t  *compute = compute_source[st_id];

        /* Contrib is updated inside */
        compute(source_terms + st_id, cm, cb, csys->source);

      } // Compute the source term on this cell

    } // Loop on source terms

  } // Source terms are defined on the whole domain or not ?

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Compute the contribution related to a source term
 *
 * \param[in]      dof_desc   description of the associated DoF
 * \param[in]      source     pointer to a cs_source_term_t structure
 * \param[in, out] p_values   pointer to the computed values (allocated if NULL)
 */
/*----------------------------------------------------------------------------*/

void
cs_source_term_compute(cs_desc_t                     dof_desc,
                       const cs_source_term_t       *source,
                       double                       *p_values[])
{
  const int  stride = 1; // Only this case is managed up to now
  const cs_cdo_quantities_t  *quant = cs_cdo_quant;

  double  *values = *p_values;

  if (source == NULL)
    bft_error(__FILE__, __LINE__, 0, _(_err_empty_st));

  cs_lnum_t n_ent = 0;
  if (cs_cdo_same_support(dof_desc.location, cs_cdo_dual_cell) ||
      cs_cdo_same_support(dof_desc.location, cs_cdo_primal_vtx))
    n_ent = quant->n_vertices;
  else if (cs_cdo_same_support(dof_desc.location, cs_cdo_primal_cell))
    n_ent = quant->n_cells;
  else
    bft_error(__FILE__, __LINE__, 0,
              _(" Invalid case. Not able to compute the source term.\n"));

  /* Initialize values */
  if (values == NULL)
    BFT_MALLOC(values, n_ent*stride, double);
  for (cs_lnum_t i = 0; i < n_ent*stride; i++)
    values[i] = 0.0;

  if (dof_desc.state & CS_FLAG_STATE_POTENTIAL) {

    switch (source->def_type) {

    case CS_PARAM_DEF_BY_VALUE:
      cs_evaluate_potential_by_value(dof_desc.location,
                                       source->ml_id,
                                       source->def.get,
                                       values);
      break;

    case CS_PARAM_DEF_BY_ANALYTIC_FUNCTION:
      cs_evaluate_potential_by_analytic(dof_desc.location,
                                          source->ml_id,
                                          source->def.analytic,
                                          values);
      break;

    default:
      bft_error(__FILE__, __LINE__, 0, _(" Invalid type of definition.\n"));

    } /* Switch according to def_type */

  }
  else if (dof_desc.state & CS_FLAG_STATE_DENSITY) {

    switch (source->def_type) {

    case CS_PARAM_DEF_BY_VALUE:
      cs_evaluate_density_by_value(dof_desc.location,
                                     source->ml_id,
                                     source->def.get,
                                     values);
      break;

    case CS_PARAM_DEF_BY_ANALYTIC_FUNCTION:
      cs_evaluate_density_by_analytic(dof_desc.location,
                                        source->ml_id,
                                        source->def.analytic,
                                        source->quad_type,
                                        values);
      break;

    default:
      bft_error(__FILE__, __LINE__, 0, _(" Invalid type of definition.\n"));

    } /* Switch according to def_type */

  } /* Density variable */

  /* Return values */
  *p_values = values;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Compute the contribution for a cell related to a source term and
 *         add it the given array of values.
 *         Case of a scalar potential defined at primal vertices by a constant
 *         value.
 *         A discrete Hodge operator has to be computed before this call and
 *         stored inside a cs_cell_builder_t structure
 *
 * \param[in]      source     pointer to a cs_source_term_t structure
 * \param[in]      cm         pointer to a cs_cell_mesh_t structure
 * \param[in, out] cb         pointer to a cs_cell_builder_t structure
 * \param[in, out] values     pointer to the computed values
 */
/*----------------------------------------------------------------------------*/

void
cs_source_term_pvsp_by_value(const cs_source_term_t    *source,
                             const cs_cell_mesh_t      *cm,
                             cs_cell_builder_t         *cb,
                             double                    *values)
{
  if (source == NULL)
    return;

  /* Sanity checks */
  assert(values != NULL && cm != NULL);
  assert(cb != NULL && cb->hdg != NULL);

  const double  pot_value = source->def.get.val;

  /* Retrieve the values of the potential at each cell vertices */
  double  *eval = cb->values;
  for (short int v = 0; v < cm->n_vc; v++)
    eval[v] = pot_value;

  /* Multiply these values by a cellwise Hodge operator previously computed */
  double  *hdg_eval = cb->values + cm->n_vc;
  cs_locmat_matvec(cb->hdg, eval, hdg_eval);

  for (short int v = 0; v < cm->n_vc; v++)
    values[v] += hdg_eval[v];
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Compute the contribution for a cell related to a source term and
 *         add it the given array of values.
 *         Case of a scalar potential defined at primal vertices by an
 *         analytical function.
 *         A discrete Hodge operator has to be computed before this call and
 *         stored inside a cs_cell_builder_t structure
 *
 * \param[in]      source     pointer to a cs_source_term_t structure
 * \param[in]      cm         pointer to a cs_cell_mesh_t structure
 * \param[in, out] cb         pointer to a cs_cell_builder_t structure
 * \param[in, out] values     pointer to the computed values
 */
/*----------------------------------------------------------------------------*/

void
cs_source_term_pvsp_by_analytic(const cs_source_term_t    *source,
                                const cs_cell_mesh_t      *cm,
                                cs_cell_builder_t         *cb,
                                double                    *values)
{
  if (source == NULL)
    return;

  /* Sanity checks */
  assert(values != NULL && cm != NULL);
  assert(cb != NULL && cb->hdg != NULL);

  const double  tcur = cs_time_step->t_cur;

  /* Retrieve the values of the potential at each cell vertices */
  double  *eval = cb->values;
  source->def.analytic(tcur, cm->n_vc, cm->xv, eval);

  /* Multiply these values by a cellwise Hodge operator previously computed */
  double  *hdg_eval = cb->values + cm->n_vc;
  cs_locmat_matvec(cb->hdg, eval, hdg_eval);

  for (short int v = 0; v < cm->n_vc; v++)
    values[v] += hdg_eval[v];
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Compute the contribution for a cell related to a source term and
 *         add it the given array of values.
 *         Case of a scalar density defined at dual cells by a value.
 *
 * \param[in]      source     pointer to a cs_source_term_t structure
 * \param[in]      cm         pointer to a cs_cell_mesh_t structure
 * \param[in, out] cb         pointer to a cs_cell_builder_t structure
 * \param[in, out] values     pointer to the computed values
 */
/*----------------------------------------------------------------------------*/

void
cs_source_term_dcsd_by_value(const cs_source_term_t    *source,
                             const cs_cell_mesh_t      *cm,
                             cs_cell_builder_t         *cb,
                             double                    *values)
{
  CS_UNUSED(cb);

  if (source == NULL)
    return;

  /* Sanity checks */
  assert(values != NULL && cm != NULL);

  const double  density_value = source->def.get.val;
  for (int v = 0; v < cm->n_vc; v++)
    values[v] += density_value * cm->wvc[v];
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Compute the contribution for a cell related to a source term and
 *         add it the given array of values.
 *         Case of a scalar density defined at dual cells by an analytical
 *         function.
 *         Use a the barycentric approximation as quadrature to evaluate the
 *         integral. Exact for linear function.
 *
 * \param[in]      source     pointer to a cs_source_term_t structure
 * \param[in]      cm         pointer to a cs_cell_mesh_t structure
 * \param[in, out] cb         pointer to a cs_cell_builder_t structure
 * \param[in, out] values     pointer to the computed values
 */
/*----------------------------------------------------------------------------*/

void
cs_source_term_dcsd_bary_by_analytic(const cs_source_term_t    *source,
                                     const cs_cell_mesh_t      *cm,
                                     cs_cell_builder_t         *cb,
                                     double                    *values)
{
  if (source == NULL)
    return;

  /* Sanity checks */
  assert(values != NULL && cm != NULL);

  /* Compute the barycenter of each portion of dual cells */
  cs_real_3_t  *xgv = cb->vectors;
  for (short int v = 0; v < cm->n_vc; v++)
    xgv[v][0] = xgv[v][1] = xgv[v][2] = 0.;

  for (short int f = 0; f < cm->n_fc; f++) {

    cs_real_3_t  xfc;

    const double  *xf = cm->face[f].center;

    for (int k = 0; k < 3; k++) xfc[k] = 0.25*(xf[k] + cm->xc[k]);

    for (int i = cm->f2e_idx[f]; i < cm->f2e_idx[f+1]; i++) {

      const short int  e = cm->f2e_ids[i];
      const short int  v1 = cm->e2v_ids[2*e];
      const short int  v2 = cm->e2v_ids[2*e+1];
      const double  *xv1 = cm->xv + 3*v1, *xv2 = cm->xv + 3*v2;
      const double  tet_vol = 0.5*cs_math_voltet(xv1, xv2, xf, cm->xc);

      // xg = 0.25(xv1 + xe + xf + xc) where xe = 0.5*(xv1 + xv2)
      for (int k = 0; k < 3; k++)
        xgv[v1][k] += tet_vol*(xfc[k] + 0.375*xv1[k] + 0.125*xv2[k]);

      // xg = 0.25(xv2 + xe + xf + xc) where xe = 0.5*(xv1 + xv2)
      for (int k = 0; k < 3; k++)
        xgv[v2][k] += tet_vol*(xfc[k] + 0.375*xv2[k] + 0.125*xv1[k]);

    } // Loop on face edges

  } // Loop on cell faces

  /* Compute the source term contribution for each vertex */
  const double  tcur = cs_time_step->t_cur;
  double  *result = cb->values;

  for (short int v = 0; v < cm->n_vc; v++) {
    const double  vol_vc = cm->vol_c * cm->wvc[v];
    const double  invvol = 1/vol_vc;
    for (int k = 0; k < 3; k++) xgv[v][k] *= invvol;
  }

  source->def.analytic(tcur, cm->n_vc, (const cs_real_t *)xgv, result);

  for (short int v = 0; v < cm->n_vc; v++)
    values[v] = cm->vol_c * cm->wvc[v] * result[v];

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Compute the contribution for a cell related to a source term and
 *         add it the given array of values.
 *         Case of a scalar density defined at dual cells by an analytical
 *         function.
 *         Use a the barycentric approximation as quadrature to evaluate the
 *         integral. Exact for linear function.
 *
 * \param[in]      source     pointer to a cs_source_term_t structure
 * \param[in]      cm         pointer to a cs_cell_mesh_t structure
 * \param[in, out] cb         pointer to a cs_cell_builder_t structure
 * \param[in, out] values     pointer to the computed values
 */
/*----------------------------------------------------------------------------*/

void
cs_source_term_dcsd_q1o1_by_analytic(const cs_source_term_t    *source,
                                     const cs_cell_mesh_t      *cm,
                                     cs_cell_builder_t         *cb,
                                     double                    *values)
{
  CS_UNUSED(cb);

  if (source == NULL)
    return;

  /* Sanity checks */
  assert(values != NULL && cm != NULL);

  const double  tcur = cs_time_step->t_cur;

  cs_analytic_func_t  *ana = source->def.analytic;

  for (short int f = 0; f < cm->n_fc; f++) {

    cs_real_3_t  xg[2], xfc;
    cs_real_t  result[2];

    const double  *xf = cm->face[f].center;

    for (int k = 0; k < 3; k++) xfc[k] = 0.25*(xf[k] + cm->xc[k]);

    for (int i = cm->f2e_idx[f]; i < cm->f2e_idx[f+1]; i++) {

      const short int  e = cm->f2e_ids[i];
      const short int  v1 = cm->e2v_ids[2*e];
      const short int  v2 = cm->e2v_ids[2*e+1];
      const double  *xv1 = cm->xv + 3*v1, *xv2 = cm->xv + 3*v2;
      const double  tet_vol = 0.5*cs_math_voltet(xv1, xv2, xf, cm->xc);

      // xg = 0.25(xv1 + xe + xf + xc) where xe = 0.5*(xv1 + xv2)
      for (int k = 0; k < 3; k++)
        xg[0][k] = xfc[k] + 0.375*xv1[k] + 0.125*xv2[k];

      // xg = 0.25(xv1 + xe + xf + xc) where xe = 0.5*(xv1 + xv2)
      for (int k = 0; k < 3; k++)
        xg[1][k] = xfc[k] + 0.375*xv2[k] + 0.125*xv1[k];

      ana(tcur, 2, (const cs_real_t *)xg, result);
      values[v1] += tet_vol * result[0];
      values[v2] += tet_vol * result[1];

    } // Loop on face edges

  } // Loop on cell faces

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Compute the contribution for a cell related to a source term and
 *         add it the given array of values.
 *         Case of a scalar density defined at dual cells by an analytical
 *         function.
 *         Use a ten-point quadrature rule to evaluate the integral.
 *         Exact for quadratic function.
 *
 * \param[in]      source     pointer to a cs_source_term_t structure
 * \param[in]      cm         pointer to a cs_cell_mesh_t structure
 * \param[in, out] cb         pointer to a cs_cell_builder_t structure
 * \param[in, out] values     pointer to the computed values
 */
/*----------------------------------------------------------------------------*/

void
cs_source_term_dcsd_q10o2_by_analytic(const cs_source_term_t    *source,
                                      const cs_cell_mesh_t      *cm,
                                      cs_cell_builder_t         *cb,
                                      double                    *values)
{
  if (source == NULL)
    return;

  /* Sanity checks */
  assert(values != NULL && cm != NULL);
  assert(cb != NULL);

  const double  tcur = cs_time_step->t_cur;
  cs_analytic_func_t  *ana = source->def.analytic;

  /* Temporary buffers */
  double  *contrib = cb->values;              // size n_vc

  /* Cell evaluation */
  double  val_c;
  ana(tcur, 1, cm->xc, &val_c);

  /* Contributions related to vertices */
  double  *result_v = cb->values + cm->n_vc; // size n_vc
  ana(tcur, cm->n_vc, cm->xv, result_v);

  cs_real_3_t  *xvc = cb->vectors;
  for (short int v = 0; v < cm->n_vc; v++) {
    const double  *xv = cm->xv + 3*v;
    for (int k = 0; k < 3; k++) xvc[v][k] = 0.5*(cm->xc[k] + xv[k]);
  }

  double  *result_vc = cb->values + 2*cm->n_vc; // size n_vc
  ana(tcur, cm->n_vc, (const cs_real_t *)xvc, result_vc);

  for (short int v = 0; v < cm->n_vc; v++) {

    // -1/20 on extrimity points and 1/5 on midpoints
    const double  val_v = -0.05*(val_c + result_v[v]) + 0.2*result_vc[v];

    /* Set the initial values */
    contrib[v] = cm->wvc[v]*cm->vol_c*val_v;

  } // Loop on vertices

  /* Overwrite result_v and result_vc */
  double  *pec_vol = cb->values + cm->n_vc;   // size n_vc
  double  *pfv_vol = cb->values + 2*cm->n_vc; // size n_ec
  for (short int e = 0; e < cm->n_ec; e++) pec_vol[e] = 0;

  /* Main loop on faces */
  for (short int f = 0; f < cm->n_fc; f++) {

    const double  *xf = cm->face[f].center;

    /* Reset volume of the face related to a vertex */
    for (short int v = 0; v < cm->n_vc; v++) pfv_vol[v] = 0;

    for (int i = cm->f2e_idx[f]; i < cm->f2e_idx[f+1]; i++) {

      const short int  e = cm->f2e_ids[i];
      const short int  v1 = cm->e2v_ids[2*e];
      const short int  v2 = cm->e2v_ids[2*e+1];
      const double  pef_vol = cs_math_voltet(cm->xv + 3*v1,
                                             cm->xv + 3*v2,
                                             xf,
                                             cm->xc);

      pec_vol[e] += pef_vol;
      pfv_vol[v1] += 0.5*pef_vol;
      pfv_vol[v2] += 0.5*pef_vol;

      cs_real_3_t  xef;
      cs_real_t  result_ef;
      for (int k = 0; k < 3; k++) xef[k] = 0.5*(cm->edge[e].center[k] + xf[k]);
      ana(tcur, 1, xef, &result_ef);

      const double  ef_contrib = 0.1*pef_vol*result_ef;
      contrib[v1] += ef_contrib;
      contrib[v2] += ef_contrib;

    } // Loop on face edges

    /* Contributions related to this face */
    double  val_f = 0.0, val_fc = 0.0;
    ana(tcur, 1, xf, &val_f);
    val_f *= -0.05; // -1/20

    cs_real_3_t  xfc;
    for (int k = 0; k < 3; k++) xfc[k] = 0.5*(xf[k] + cm->xc[k]);
    ana(tcur, 1, xfc, &val_fc);
    val_f += 0.2*val_fc; // 1/5

    for (short int v = 0; v < cm->n_vc; v++) {
      if (pfv_vol[v] > 0) {
        double  val_fv = 0;
        cs_real_3_t  xfv;
        for (int k = 0; k < 3; k++) xfv[k] = 0.5*(xf[k] + cm->xv[3*v+k]);
        ana(tcur, 1, xfv, &val_fv);
        contrib[v] += pfv_vol[v]*(val_f + 0.2*val_fv); // 1/5
      }
    }

  } // Loop on cell faces

  /* Contributions related to edges */
  cs_real_3_t  *xev = cb->vectors;              // overwrite xvc
  for (short int e = 0; e < cm->n_ec; e++) {

    const cs_real_t  *xe = cm->edge[e].center;

    const short int  v1 = cm->e2v_ids[2*e];
    const double  *xv1 = cm->xv + 3*v1;
    const short int  v2 = cm->e2v_ids[2*e+1];
    const double  *xv2 = cm->xv + 3*v2;

    for (int k = 0; k < 3; k++) {
      xev[2*e][k] = 0.5*(xv1[k] + xe[k]);
      xev[2*e+1][k] = 0.5*(xv2[k] + xe[k]);
    }

  } // Loop on edges

  cs_real_t  *val_ev = cb->values + 2*cm->n_vc; // overwrite pfv_vol
  ana(tcur, 2*cm->n_ec, (const cs_real_t *)xev, val_ev);

  cs_real_3_t  *xe = cb->vectors;               // overwrite xev
  cs_real_3_t  *xec = cb->vectors + cm->n_ec;
  for (short int e = 0; e < cm->n_ec; e++) {

    /* Update contrib */
    const double  vol_e = 0.1*pec_vol[e];
    const short int  v1 = cm->e2v_ids[2*e];
    const short int  v2 = cm->e2v_ids[2*e+1];

    contrib[v1] += vol_e * val_ev[2*e];
    contrib[v2] += vol_e * val_ev[2*e+1];

    /* Prepare the second call related to edges */
    for (int k = 0; k < 3; k++) {
      const double  coord = cm->edge[e].center[k];
      xe[e][k] = coord;
      xec[e][k] = 0.5*(cm->xc[k] + coord);
    }

  } // Loop on edges

  cs_real_t  *val_ec = cb->values + 2*cm->n_vc; // overwrite vel_ev
  ana(tcur, 2*cm->n_ec, (const cs_real_t *)xe, val_ec);

  /* Last update of contrib related to edges */
  for (short int e = 0; e < cm->n_ec; e++) {

    // -1/20 * val_e + 1/5*val_ec
    const double val_e = -0.05*val_ec[2*e] + 0.2*val_ec[2*e+1];
    const double e_contrib = 0.5 * pec_vol[e] * val_e;
    const short int  v1 = cm->e2v_ids[2*e], v2 = cm->e2v_ids[2*e+1];

    contrib[v1] += e_contrib;
    contrib[v2] += e_contrib;

  } // Loop on edges

  /* Add the computed contributions to the return values */
  for (short int v = 0; v < cm->n_vc; v++)
    values[v] += contrib[v];

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Compute the contribution for a cell related to a source term and
 *         add it the given array of values.
 *         Case of a scalar density defined at dual cells by an analytical
 *         function.
 *         Use a five-point quadrature rule to evaluate the integral.
 *         Exact for cubic function.
 *         This function may be expensive since many evaluations are needed.
 *         Please use it with care.
 *
 * \param[in]      source     pointer to a cs_source_term_t structure
 * \param[in]      cm         pointer to a cs_cell_mesh_t structure
 * \param[in, out] cb         pointer to a cs_cell_builder_t structure
 * \param[in, out] values     pointer to the computed values
 */
/*----------------------------------------------------------------------------*/

void
cs_source_term_dcsd_q5o3_by_analytic(const cs_source_term_t    *source,
                                     const cs_cell_mesh_t      *cm,
                                     cs_cell_builder_t         *cb,
                                     double                    *values)
{
  double  sum, weights[5], results[5];
  cs_real_3_t  gauss_pts[5];

  if (source == NULL)
    return;

  /* Sanity checks */
  assert(values != NULL && cm != NULL);
  assert(cb != NULL);

  const double  tcur = cs_time_step->t_cur;
  cs_analytic_func_t  *ana = source->def.analytic;

  /* Temporary buffers */
  double  *contrib = cb->values;
  for (short int v = 0; v < cm->n_vc; v++) contrib[v] = 0;

  /* Main loop on faces */
  for (short int f = 0; f < cm->n_fc; f++) {

    const double  *xf = cm->face[f].center;

    for (int i = cm->f2e_idx[f]; i < cm->f2e_idx[f+1]; i++) {

      const short int  e = cm->f2e_ids[i];
      const short int  v1 = cm->e2v_ids[2*e];
      const short int  v2 = cm->e2v_ids[2*e+1];
      const double  tet_vol = 0.5*cs_math_voltet(cm->xv + 3*v1,
                                                 cm->xv + 3*v2,
                                                 xf,
                                                 cm->xc);

      /* Compute Gauss points and its weights */
      cs_quadrature_tet_5pts(cm->xv + 3*v1, cm->edge[e].center, xf, cm->xc,
                             tet_vol,
                             gauss_pts, weights);

      ana(tcur, 5, (const cs_real_t *)gauss_pts, results);
      sum = 0.;
      for (int p = 0; p < 5; p++) sum += results[p] * weights[p];
      contrib[v1] += sum;

      /* Compute Gauss points and its weights */
      cs_quadrature_tet_5pts(cm->xv + 3*v2, cm->edge[e].center, xf, cm->xc,
                             tet_vol,
                             gauss_pts, weights);

      ana(tcur, 5, (const cs_real_t *)gauss_pts, results);
      sum = 0.;
      for (int p = 0; p < 5; p++) sum += results[p] * weights[p];
      contrib[v2] += sum;

    } // Loop on face edges

  } // Loop on cell faces

  /* Add the computed contributions to the return values */
  for (short int v = 0; v < cm->n_vc; v++)
    values[v] += contrib[v];
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Compute the contribution for a cell related to a source term and
 *         add it the given array of values.
 *         Case of a scalar potential defined at primal vertices and cells
 *         by a constant value.
 *         A discrete Hodge operator has to be computed before this call and
 *         stored inside a cs_cell_builder_t structure
 *
 * \param[in]      source     pointer to a cs_source_term_t structure
 * \param[in]      cm         pointer to a cs_cell_mesh_t structure
 * \param[in, out] cb         pointer to a cs_cell_builder_t structure
 * \param[in, out] values     pointer to the computed values
 */
/*----------------------------------------------------------------------------*/

void
cs_source_term_vcsp_by_value(const cs_source_term_t    *source,
                             const cs_cell_mesh_t      *cm,
                             cs_cell_builder_t         *cb,
                             double                    *values)
{
  if (source == NULL)
    return;

  /* Sanity checks */
  assert(values != NULL && cm != NULL);
  assert(cb != NULL && cb->hdg != NULL);

  const double  pot_value = source->def.get.val;

  /* Retrieve the values of the potential at each cell vertices */
  double  *eval = cb->values;
  for (short int v = 0; v < cm->n_vc; v++)
    eval[v] = pot_value;
  eval[cm->n_vc] = pot_value;

  /* Multiply these values by a cellwise Hodge operator previously computed */
  double  *hdg_eval = cb->values + cm->n_vc + 1;
  cs_locmat_matvec(cb->hdg, eval, hdg_eval);

  for (short int v = 0; v < cm->n_vc + 1; v++)
    values[v] += hdg_eval[v];
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Compute the contribution for a cell related to a source term and
 *         add it the given array of values.
 *         Case of a scalar potential defined at primal vertices and cells by
 *         an analytical function.
 *         A discrete Hodge operator has to be computed before this call and
 *         stored inside a cs_cell_builder_t structure
 *
 * \param[in]      source     pointer to a cs_source_term_t structure
 * \param[in]      cm         pointer to a cs_cell_mesh_t structure
 * \param[in, out] cb         pointer to a cs_cell_builder_t structure
 * \param[in, out] values     pointer to the computed values
 */
/*----------------------------------------------------------------------------*/

void
cs_source_term_vcsp_by_analytic(const cs_source_term_t    *source,
                                const cs_cell_mesh_t      *cm,
                                cs_cell_builder_t         *cb,
                                double                    *values)
{
  if (source == NULL)
    return;

  /* Sanity checks */
  assert(values != NULL && cm != NULL);
  assert(cb != NULL && cb->hdg != NULL);

  const double  tcur = cs_time_step->t_cur;

  cs_analytic_func_t  *ana = source->def.analytic;

  /* Retrieve the values of the potential at each cell vertices */
  double  *eval = cb->values;
  ana(tcur, cm->n_vc, cm->xv, eval);
  ana(tcur, 1, cm->xc, eval + cm->n_vc);

  /* Multiply these values by a cellwise Hodge operator previously computed */
  double  *hdg_eval = cb->values + cm->n_vc + 1;
  cs_locmat_matvec(cb->hdg, eval, hdg_eval);

  for (short int v = 0; v < cm->n_vc + 1; v++)
    values[v] += hdg_eval[v];
}

/*----------------------------------------------------------------------------*/

END_C_DECLS
