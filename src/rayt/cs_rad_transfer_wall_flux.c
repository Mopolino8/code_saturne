/*============================================================================
 * Radiation solver operations.
 *============================================================================*/

/* This file is part of Code_Saturne, a general-purpose CFD tool.

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
  Street, Fifth Floor, Boston, MA 02110-1301, USA. */

/*----------------------------------------------------------------------------*/

#include "cs_defs.h"
#include "cs_math.h"

/*----------------------------------------------------------------------------
 * Standard C library headers
 *----------------------------------------------------------------------------*/

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <float.h>

#if defined(HAVE_MPI)
#include <mpi.h>
#endif

/*----------------------------------------------------------------------------
 *  Local headers
 *----------------------------------------------------------------------------*/

#include "bft_error.h"
#include "bft_mem.h"
#include "bft_printf.h"

#include "cs_log.h"
#include "cs_boundary_zone.h"
#include "cs_mesh.h"
#include "cs_mesh_quantities.h"
#include "cs_parall.h"
#include "cs_parameters.h"
#include "cs_sles.h"
#include "cs_sles_it.h"
#include "cs_timer.h"

#include "cs_rad_transfer.h"

/*----------------------------------------------------------------------------
 *  Header for the current file
 *----------------------------------------------------------------------------*/

#include "cs_rad_transfer_wall_flux.h"

/*----------------------------------------------------------------------------*/

BEGIN_C_DECLS

/*=============================================================================
 * Additional Doxygen documentation
 *============================================================================*/

/*! \file  cs_rad_transfer_wall_flux.c */

/*! \cond DOXYGEN_SHOULD_SKIP_THIS */

/*! (DOXYGEN_SHOULD_SKIP_THIS) \endcond */

/*=============================================================================
 * Public function definitions
 *============================================================================*/

/*----------------------------------------------------------------------------*/
/*!
 * \brief Wall temperature computation with flux balance.
 *
 * \param[in]  nvar     number of variable BC's
 * \param[in]  ivart    variable id of thermal variable
 * \param[in]  isothp   list of isothermal boundaries
 * \param[in]  rcodcl   boundary condition values
 *                        rcodcl[0] = Dirichlet value
 *                        rcodcl[1] = exchange coefficient value. (infinite
 *                                    if no exchange)
 *                        rcodcl[2] = flux density value (negative if gain)
 *                                    in w/m2 or rugosity height (m)
 *                                    if icodcl=6,
 *                                    - for velocities, (vistl+visct)*gradu
 *                                    - for pressure, dt*gradp,
 *                                    - for scalars,
 *                                      cp*(viscls+visct/turb_schmidt)*gradt
 * \param[out] tparop   wall temperature in Kelvin
 * \param[in]  qincip   dradiative flux density at boundaries
 * \param[in]  textp    exterior boundary temperature in degrees C
 * \param[in]  tintp    interior boundary temperature in degrees C
 * \param[in]  xlamp    thermal conductivity coefficient of wall faces (w/m/k)
 * \param[in]  epap     wall thickness (m)
 * \param[in]  epsp     wall emissivity
 * \param[in]  hfconp   boundary fluid exchange coefficient
 * \param[in]  flconp   boundary convective flux density
 * \param[in]  tempkp   temperature in Kelvin
 */
/*----------------------------------------------------------------------------*/

void
cs_rad_transfer_wall_flux(int         nvar,
                          int         ivart,
                          int         isothp[],
                          cs_real_t  *tmin,
                          cs_real_t  *tmax,
                          cs_real_t  *tx,
                          cs_real_t  *rcodcl,
                          cs_real_t   tparop[],
                          cs_real_t   qincip[],
                          cs_real_t   textp[],
                          cs_real_t   tintp[],
                          cs_real_t   xlamp[],
                          cs_real_t   epap[],
                          cs_real_t   epsp[],
                          cs_real_t   hfconp[],
                          cs_real_t   flconp[],
                          cs_real_t   tempkp[])
{
  const int     nb1int = 5;
  const int     nb2int = 5;
  const int     nbrrdp = 5;

  const cs_real_t stephn = 5.6703e-8;
  const cs_real_t tkelvi = 273.15e0;

  /* local variables */
  cs_gnum_t  inttm1[nb1int], inttm2[nb2int];
  cs_real_t  xtpmax, ytpmax, ztpmax, xtpmin, ytpmin, ztpmin;

  cs_real_t tpmax  = -cs_math_big_r;
  cs_real_t tpmin  =  cs_math_big_r;
  cs_real_t qcmax  = -cs_math_big_r;
  cs_real_t qcmin  =  cs_math_big_r;
  cs_real_t qrmax  = -cs_math_big_r;
  cs_real_t qrmin  =  cs_math_big_r;
  cs_real_t rapmax = 0.0;
  cs_gnum_t n1min  = 0;
  cs_gnum_t n1max  = 0;
  cs_gnum_t nrelax = 0;
  cs_gnum_t nmoins = 0;
  cs_gnum_t nplus  = 0;
  int iitpim = 0;
  int iipgrn = 0;
  int iipref = 0;
  int iifgrn = 0;
  int iifref = 0;

  cs_lnum_t ifacmx = 0;
  cs_lnum_t ifacmn = 0;

  const cs_lnum_t n_b_faces = cs_glob_mesh->n_b_faces;

  cs_boundary_zone_update_face_class_id();
  const int n_zones = cs_boundary_zone_max_class_or_zone_id() + 1;
  const int *b_face_class_id = cs_boundary_zone_face_class_or_zone_id();

  int       *i_buf;
  cs_real_t *r_buf;
  const size_t  buf_stride = n_zones;

  BFT_MALLOC(i_buf, n_zones, int);
  BFT_MALLOC(r_buf, buf_stride*7, cs_real_t);

  int        *indtp = i_buf;
  cs_real_t  *tzomax = r_buf;
  cs_real_t  *tzomin = r_buf + buf_stride;
  cs_real_t  *tzomoy = r_buf + buf_stride*2;
  cs_real_t  *flunet = r_buf + buf_stride*3;
  cs_real_t  *radios = r_buf + buf_stride*4;
  cs_real_t  *surft  = r_buf + buf_stride*5;
  cs_real_t  *rdptmp = r_buf + buf_stride*6;

  for (int log_z_id = 0; log_z_id < n_zones; log_z_id++) {
    indtp[log_z_id]  = 0;
    tzomax[log_z_id] = -cs_math_big_r;
    tzomin[log_z_id] =  cs_math_big_r;
  }

  /* Wall temperature computation
     ---------------------------- */

  cs_lnum_t  ircodcl = ivart * n_b_faces + 2 * n_b_faces * nvar;

  for (cs_lnum_t ifac = 0; ifac < n_b_faces; ifac++) {

    cs_real_t qconv = 0.0;
    cs_real_t qrayt = 0.0;

    int log_z_id = b_face_class_id[ifac];

    /* Isotherms */
    if (isothp[ifac] == cs_glob_rad_transfer_params->itpimp) {

      /* Mark for logging */
      iitpim = 1;
      indtp[log_z_id] = cs_glob_rad_transfer_params->itpimp;

      /* Computation */
      tparop[ifac] = tintp[ifac];

      qconv = flconp[ifac];
      cs_real_t qinci = qincip[ifac];
      cs_real_t sigt4 = stephn * cs_math_pow4(tparop[ifac]);
      cs_real_t epp   = epsp[ifac];
      qrayt = epp * (qinci - sigt4);
    }

    /* Grey or black boundaries (non reflecting) */
    else if (isothp[ifac] == cs_glob_rad_transfer_params->ipgrno) {

      /* Mark for logging */
      iipgrn = 1;
      indtp[log_z_id] = cs_glob_rad_transfer_params->ipgrno;

      /* Computation */
      cs_real_t esl    = epap[ifac] / xlamp[ifac];
      qconv  = flconp[ifac];
      cs_real_t qinci  = qincip[ifac];
      cs_real_t epp    = epsp[ifac];
      cs_real_t sigt3  = stephn * cs_math_pow3(tparop[ifac]);
      qrayt  = epp * (qinci - sigt3 * tparop[ifac]);
      cs_real_t detep  =   (esl * (qconv + qrayt) - (tparop[ifac] - textp[ifac]))
                         / (1.0 + 4.0 * esl * epp * sigt3 + esl * hfconp[ifac]);
      cs_real_t rapp   = detep / tparop[ifac];
      cs_real_t abrapp = CS_ABS(rapp);

      /* Relaxation */
      if (abrapp >= *tx) {
        nrelax++;
        tparop[ifac] *= 1.0 + *tx * rapp / abrapp;
      }
      else
        tparop[ifac] += detep;

      rapmax = CS_MAX(rapmax, abrapp);
      if (rapp <= 0.0)
        nmoins++;
      else
        nplus++;

      /* Clipping */
      if (tparop[ifac] < *tmin) {
        n1min++;
        tparop[ifac] = *tmin;
      }
      if (tparop[ifac] > *tmax) {
        n1max++;
        tparop[ifac] = *tmax;
      }

    }

    /* Reflecting walls */
    else if (isothp[ifac] == cs_glob_rad_transfer_params->iprefl) {

      /* Mark for logging */
      iipref = 1;
      indtp[log_z_id] = cs_glob_rad_transfer_params->iprefl;

      /* Computation */
      cs_real_t esl    = epap[ifac] / xlamp[ifac];
      qconv  = flconp[ifac];
      cs_real_t detep  =  (esl * qconv - (tparop[ifac] - textp[ifac]))
                        / (1.0 + esl * hfconp[ifac]);
      cs_real_t rapp   = detep / tparop[ifac];
      cs_real_t abrapp = CS_ABS(rapp);
      qrayt = 0.0;

      /* Relaxation */
      if (abrapp >= *tx) {
        nrelax++;
        tparop[ifac] *= 1.0 + *tx * rapp / abrapp;
      }
      else
        tparop[ifac] += detep;

      rapmax = CS_MAX(rapmax, abrapp);
      if (rapp <= 0.0)
        nmoins++;
      else
        nplus++;

      /* Clipping */
      if (tparop[ifac] < *tmin) {
        n1min++;
        tparop[ifac] = *tmin;
      }
      if (tparop[ifac] > *tmax) {
        n1max++;
        tparop[ifac] = *tmax;
      }

    }

    /* Wall with non-reflecting conduction flux:
     *   if the flux is zero, the wall is adiabatic
     *   (radiative transfer is balanced by convection) */
    else if (isothp[ifac] == cs_glob_rad_transfer_params->ifgrno) {

      /* Mark for logging */
      iifgrn = 1;
      indtp[log_z_id] = cs_glob_rad_transfer_params->ifgrno;

      /* Computation */
      qconv  = flconp[ifac];
      cs_real_t qinci  = qincip[ifac];
      cs_real_t epp    = epsp[ifac];
      cs_real_t sigt3  = stephn * cs_math_pow3(tparop[ifac]);
      qrayt  = epp * (qinci - sigt3 * tparop[ifac]);
      cs_real_t detep  =  (qconv + qrayt - rcodcl[ifac + ircodcl])
                        / (4.0 * epp * sigt3 + hfconp[ifac]);
      cs_real_t rapp   = detep / tparop[ifac];
      cs_real_t abrapp = CS_ABS(rapp);

      /* Relaxation */
      if (abrapp >= *tx) {
        nrelax++;
        tparop[ifac] *= 1.0 + *tx * rapp / abrapp;
      }
      else
        tparop[ifac] += detep;

      rapmax = CS_MAX(rapmax, abrapp);
      if (rapp <= 0.0)
        nmoins++;
      else
        nplus++;

      /* Clipping */
      if (tparop[ifac] < *tmin) {
        n1min++;
        tparop[ifac] = *tmin;
      }
      if (tparop[ifac] > *tmax) {
        n1max++;
        tparop[ifac] = *tmax;
      }

    }

    /* 5) parois a flux de conduction imposee et reflechissante
     *    equivalent a impose un flux total au fluide    */
    else if (isothp[ifac] == cs_glob_rad_transfer_params->ifrefl) {

      /* Mark for logging */
      iifref = 1;
      indtp[log_z_id] = cs_glob_rad_transfer_params->ifrefl;

      /* Computation */
      cs_lnum_t iel = cs_glob_mesh->b_face_cells[ifac];
      tparop[ifac] = hfconp[ifac] * tempkp[iel] - rcodcl[ifac + ircodcl];
      tparop[ifac] = tparop[ifac] / CS_MAX(hfconp[ifac], cs_math_epzero);

      qconv = flconp[ifac];
      qrayt = 0.0;

      /* Clipping */
      if (tparop[ifac] < *tmin) {
        n1min++;
        tparop[ifac] = *tmin;
      }
      if (tparop[ifac] > *tmax) {
        n1max++;
        tparop[ifac] = *tmax;
      }

    }

    /* Max-Min */
    if (   isothp[ifac] == cs_glob_rad_transfer_params->itpimp
        || isothp[ifac] == cs_glob_rad_transfer_params->ipgrno
        || isothp[ifac] == cs_glob_rad_transfer_params->iprefl
        || isothp[ifac] == cs_glob_rad_transfer_params->ifgrno
        || isothp[ifac] == cs_glob_rad_transfer_params->ifrefl) {
      if (tpmax <= tparop[ifac]) {
        ifacmx = ifac;
        tpmax  = tparop[ifac];
        qcmax  = qconv;
        qrmax  = qrayt;
      }
      if (tpmin >= tparop[ifac]) {
        ifacmn = ifac;
        tpmin  = tparop[ifac];
        qcmin  = qconv;
        qrmin  = qrayt;
      }
      tzomax[log_z_id] = CS_MAX(tzomax[log_z_id], tparop[ifac]);
      tzomin[log_z_id] = CS_MIN(tzomin[log_z_id], tparop[ifac]);
    }

  }

  /* Logging
   * ======= */

  /* Check if there are any wall zones */

  if (cs_glob_rank_id >= 0)
    cs_parall_max(n_zones, CS_INT_TYPE, indtp);

  int indtpm = 0;
  for (int log_z_id = 0; log_z_id < n_zones; log_z_id++) {
    if (indtp[log_z_id] != 0) {
      indtpm  = 1;
      break;
    }
  }

  /* If there are any walls */
  if (indtpm > 0) {

    /* Level 1 verbosity */
    if (cs_glob_rad_transfer_params->iimpar >= 1) {

      /* Calcul de TZOMOY FLUNET RADIOS SURFT */
      for (int log_z_id = 0; log_z_id < n_zones; log_z_id++) {
        tzomoy[log_z_id] = 0.0;
        flunet[log_z_id] = 0.0;
        radios[log_z_id] = 0.0;
        surft[log_z_id]  = 0.0;
      }

      for (cs_lnum_t ifac = 0; ifac < n_b_faces; ifac++) {
        cs_real_t srfbn = cs_glob_mesh_quantities->b_face_surf[ifac];
        int log_z_id = b_face_class_id[ifac];

        if (indtp[log_z_id] != 0) {
          cs_real_t tp4 = cs_math_pow4(tparop[ifac]);
          tzomoy[log_z_id] += tparop[ifac] * srfbn;
          flunet[log_z_id] += epsp[ifac] * (qincip[ifac] - stephn * tp4) * srfbn;
          radios[log_z_id] += - (epsp[ifac] * stephn * tp4
                           + (1.0 - epsp[ifac]) * qincip[ifac]) * srfbn;
          surft[log_z_id]  += srfbn;
        }

      }

      if (cs_glob_rank_id >= 0) {
        cs_parall_sum(n_zones, CS_REAL_TYPE, tzomoy);
        cs_parall_sum(n_zones, CS_REAL_TYPE, flunet);
        cs_parall_sum(n_zones, CS_REAL_TYPE, radios);
        cs_parall_sum(n_zones, CS_REAL_TYPE, surft);
      }

      for (int log_z_id = 0; log_z_id < n_zones; log_z_id++) {
        if (indtp[log_z_id] != 0) {
          tzomoy[log_z_id] /= surft[log_z_id];
          radios[log_z_id] /= surft[log_z_id];
        }
      }

      /*      Determination de la TPMAX TPMIN et des grandeurs associees   */

      if (ifacmx > 0) {
        cs_lnum_t iel = cs_glob_mesh->b_face_cells[ifacmx];
        cs_real_3_t *xyzcen = (cs_real_3_t *)cs_glob_mesh_quantities->cell_cen;
        xtpmax = xyzcen[iel][0];
        ytpmax = xyzcen[iel][1];
        ztpmax = xyzcen[iel][2];
      }
      else {
        xtpmax = 0.0;
        ytpmax = 0.0;
        ztpmax = 0.0;
      }
      if (ifacmn > 0) {
        cs_lnum_t iel = cs_glob_mesh->b_face_cells[ifacmn];
        cs_real_3_t *xyzcen = (cs_real_3_t *)cs_glob_mesh_quantities->cell_cen;
        xtpmin = xyzcen[iel][0];
        ytpmin = xyzcen[iel][1];
        ztpmin = xyzcen[iel][2];
      }
      else {
        xtpmin = 0.0;
        ytpmin = 0.0;
        ztpmin = 0.0;
      }

      if (cs_glob_rank_id >= 0) {
        rdptmp[0] = xtpmax;
        rdptmp[1] = ytpmax;
        rdptmp[2] = ztpmax;
        rdptmp[3] = qcmax;
        rdptmp[4] = qrmax;
        cs_parall_max_loc_vals(nbrrdp, &tpmax, rdptmp);
        xtpmax = rdptmp[0];
        ytpmax = rdptmp[1];
        ztpmax = rdptmp[2];
        qcmax  = rdptmp[3];
        qrmax  = rdptmp[4];

        rdptmp[0] = xtpmin;
        rdptmp[1] = ytpmin;
        rdptmp[2] = ztpmin;
        rdptmp[3] = qcmin;
        rdptmp[4] = qrmin;
        cs_parall_min_loc_vals(nbrrdp, &tpmin, rdptmp);
        xtpmin = rdptmp[0];
        ytpmin = rdptmp[1];
        ztpmin = rdptmp[2];
        qcmin  = rdptmp[3];
        qrmin  = rdptmp[4];
      }

      /* Determination of counters */

      if (cs_glob_rank_id >= 0) {
        cs_parall_max(1, CS_REAL_TYPE, &rapmax);

        inttm2[0] = nmoins;
        inttm2[1] = nplus;
        inttm2[2] = n1min;
        inttm2[3] = n1max;
        inttm2[4] = nrelax;
        cs_parall_sum(nb2int, CS_GNUM_TYPE, inttm2);
        nmoins = inttm2[0];
        nplus  = inttm2[1];
        n1min  = inttm2[2];
        n1max  = inttm2[3];
        nrelax = inttm2[4];

        cs_parall_max(n_zones, CS_REAL_TYPE, tzomax);
        cs_parall_min(n_zones, CS_REAL_TYPE, tzomin);

        inttm1[0] = iitpim;
        inttm1[1] = iipgrn;
        inttm1[2] = iipref;
        inttm1[3] = iifgrn;
        inttm1[4] = iifref;
        cs_parall_max(nb1int, CS_GNUM_TYPE, inttm1);
        iitpim = inttm1[0];
        iipgrn = inttm1[1];
        iipref = inttm1[2];
        iifgrn = inttm1[3];
        iifref = inttm1[4];
      }

      cs_log_separator(CS_LOG_DEFAULT);

      cs_log_printf(CS_LOG_DEFAULT,
                    _("\n"
                      "  ** Information on wall temperature\n"
                      "     -------------------------------\n"));

      if (nrelax > 0)
        cs_log_printf(CS_LOG_DEFAULT,
                      _("\n"
                        " Warning: wall temperature relaxed to %7.2f "
                        "at (%llu points)\n"),
                      *tx * 100.0, (unsigned long long)nrelax);

      if (n1min > 0 || n1max > 0)
        cs_log_printf(CS_LOG_DEFAULT,
                      _("\n"
                        " Warning: wall temperature clipped:\n"
                        "   to minimum at %llu faces\n"
                        "   to maximum at %llu faces\n"),
                      (unsigned long long)n1min,
                      (unsigned long long)n1max);

      if (rapmax > 0 || nmoins > 0 || nplus > 0)
        cs_log_printf(CS_LOG_DEFAULT,
                      _("\n"
                        " Maximum variation: %9.4f\n"
                        "   decreasing wall temperature: %llu faces\n"
                        "   increasing wall temperature: %llu faces\n"),
                      rapmax * 100.0,
                      (unsigned long long)nmoins,
                      (unsigned long long)nplus);

      if (iitpim == 1) {
        cs_log_printf(CS_LOG_DEFAULT,
                      _("\n"
                        " Fixed profiles   Temp max (C)   Temp min (C)   "
                        "Temp mean (C)  Net flux (W)\n"
                        " ------------------------------------------------"
                        "---------------------------\n"));
        for (int log_z_id = 0; log_z_id < n_zones; log_z_id++)
          if (indtp[log_z_id] == cs_glob_rad_transfer_params->itpimp)
            cs_log_printf(CS_LOG_DEFAULT,
                          "%10d        %11.4e    %11.4e    %11.4e    %11.4e\n",
                          log_z_id,
                          tzomax[log_z_id]-tkelvi,
                          tzomin[log_z_id]-tkelvi,
                          tzomoy[log_z_id]-tkelvi,
                          flunet[log_z_id]);
      }

      if (iipgrn == 1) {
        cs_log_printf(CS_LOG_DEFAULT,
                      _("\n"
                        " Gray or black    Temp max (C)   Temp min (C)   "
                        "Temp mean (C)  Net flux (W)\n"
                        "------------------------------------------------"
                        "---------------------------\n"));
        for (int log_z_id = 0; log_z_id < n_zones; log_z_id++)
          if (indtp[log_z_id] == cs_glob_rad_transfer_params->ipgrno)
            cs_log_printf(CS_LOG_DEFAULT,
                          "%10d        %11.4e    %11.4e    %11.4e    %11.4e\n",
                          log_z_id,
                          tzomax[log_z_id]-tkelvi,
                          tzomin[log_z_id]-tkelvi,
                          tzomoy[log_z_id]-tkelvi,
                          flunet[log_z_id]);
      }

      if (iipref == 1) {
        cs_log_printf(CS_LOG_DEFAULT,
                      _("\n"
                        " Walls at EPS=0   Temp max (C)   Temp min (C)   "
                        "Temp mean (C)  Net flux (W)\n"
                        "------------------------------------------------"
                        "---------------------------\n"));
        for (int log_z_id = 0; log_z_id < n_zones; log_z_id++) {
          if (indtp[log_z_id] == cs_glob_rad_transfer_params->iprefl)
            cs_log_printf(CS_LOG_DEFAULT,
                          "%10d        %11.4e    %11.4e    %11.4e    %11.4e\n",
                          log_z_id,
                          tzomax[log_z_id]-tkelvi,
                          tzomin[log_z_id]-tkelvi,
                          tzomoy[log_z_id]-tkelvi,
                          flunet[log_z_id]);
        }
      }

      if (iifgrn == 1) {
        cs_log_printf(CS_LOG_DEFAULT,
                      _("\n"
                        " Fix flux EPS!=0  Temp max (C)   Temp min (C)   "
                        "Temp mean (C)  Net flux (W)\n"
                        "------------------------------------------------"
                        "---------------------------\n"));
        for (int log_z_id = 0; log_z_id < n_zones; log_z_id++) {
          if (indtp[log_z_id] == cs_glob_rad_transfer_params->ifgrno)
            cs_log_printf(CS_LOG_DEFAULT,
                          "%10d        %11.4e    %11.4e    %11.4e    %11.4e\n",
                          log_z_id,
                          tzomax[log_z_id]-tkelvi,
                          tzomin[log_z_id]-tkelvi,
                          tzomoy[log_z_id]-tkelvi,
                          flunet[log_z_id]);
        }

      }

      if (iifref == 1) {
        cs_log_printf(CS_LOG_DEFAULT,
                      _("\n"
                        " Fix flux EPS=0   Temp max (C)   Temp min (C)   "
                        "Temp mean (C)  Net flux (W)\n"
                        "------------------------------------------------"
                        "---------------------------\n"));
        for (int log_z_id = 0; log_z_id < n_zones; log_z_id++) {
          if (indtp[log_z_id] == cs_glob_rad_transfer_params->ifrefl)
            cs_log_printf(CS_LOG_DEFAULT,
                          "%10d        %11.4e    %11.4e    %11.4e    %11.4e\n",
                          log_z_id,
                          tzomax[log_z_id]-tkelvi,
                          tzomin[log_z_id]-tkelvi,
                          tzomoy[log_z_id]-tkelvi,
                          flunet[log_z_id]);
        }
      }

    }

    /* If we need additional verbosity */

    if (cs_glob_rad_transfer_params->iimpar >= 2) {
      const char fmt[]
        = N_("\n"
             " %s wall temperature (degrees Celsius) = %15.7f\n"
             "   at coordinates [%11.4e, %11.4e, %11.4e]\n"
             "\n"
             "   convective flux: %15.7f\n"
             "   radiative flux = %15.7f\n\n");
      cs_log_printf(CS_LOG_DEFAULT,
                    fmt, _("Maximum"),
                    tpmax-tkelvi, xtpmax, ytpmax, ztpmax, qcmax, qrmax);
      cs_log_printf(CS_LOG_DEFAULT,
                    fmt, _("Minimum"),
                    tpmin-tkelvi, xtpmin, ytpmin, ztpmin, qcmin, qrmin);
    }
  }

  BFT_FREE(i_buf);
  BFT_FREE(r_buf);
}

/*----------------------------------------------------------------------------*/

END_C_DECLS
