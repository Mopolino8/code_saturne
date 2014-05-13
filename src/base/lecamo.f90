!-------------------------------------------------------------------------------

! This file is part of Code_Saturne, a general-purpose CFD tool.
!
! Copyright (C) 1998-2014 EDF S.A.
!
! This program is free software; you can redistribute it and/or modify it under
! the terms of the GNU General Public License as published by the Free Software
! Foundation; either version 2 of the License, or (at your option) any later
! version.
!
! This program is distributed in the hope that it will be useful, but WITHOUT
! ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
! FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
! details.
!
! You should have received a copy of the GNU General Public License along with
! this program; if not, write to the Free Software Foundation, Inc., 51 Franklin
! Street, Fifth Floor, Boston, MA 02110-1301, USA.

!-------------------------------------------------------------------------------
!===============================================================================
! Function :
! --------

!> \file lecamo.f90
!>
!> \brief Reading of restart file.
!>
!-------------------------------------------------------------------------------

!-------------------------------------------------------------------------------
! Arguments
!______________________________________________________________________________.
!  mode           name          role                                           !
!______________________________________________________________________________!
!> \param[in]     ncelet        number of extended (real + ghost) cells
!> \param[in]     ncel          number of cells
!> \param[in]     nfac          number of inner faces
!> \param[in]     nfabor        number of boundary faces
!> \param[in]     nvar          total number of variables
!> \param[in]     nscal         total number of scalars
!> \param[out]    dt            time step (per cell)
!> \param[out]    rtp           calculated variables at cell centers
!> \param[out]    propce        physical properties at cell centers
!> \param[out]    frcxt         external forces making hydrostatic pressure
!> \param[out]    prhyd         predicted hydrostatic pressure
!_______________________________________________________________________________


subroutine lecamo &
 ( ncelet , ncel   , nfac   , nfabor , nvar   , nscal  ,          &
   dt     , rtp    , propce ,                                     &
   frcxt  , prhyd  )

!===============================================================================

!===============================================================================
! Module files
!===============================================================================

use paramx
use cstphy
use cstnum
use entsor
use optcal
use pointe
use numvar
use parall

!===============================================================================

implicit none

! Arguments

integer          ncelet , ncel   , nfac   , nfabor
integer          nvar   , nscal

double precision dt(ncelet), rtp(ncelet,nflown:nvar)
double precision propce(ncelet,*)
double precision frcxt(3,ncelet), prhyd(ncelet)

! Local variables


!===============================================================================



!===============================================================================
! 1. INITIALISATIONS
!===============================================================================

write(nfecra,1000)


!===============================================================================
! 2. LECTURE DU FICHIER SUITE PRINCIPAL
!===============================================================================

call lecamp &
!==========
( ncelet , ncel   ,                                    &
  nvar   , nscal  ,                                    &
  rtp    )


!===============================================================================
! 3. LECTURE DU FICHIER SUITE AUXILIAIRE
!===============================================================================

if (ileaux.eq.1) then

  call lecamx &
  !==========
( ncelet , ncel   , nfac   , nfabor , nvar   , nscal  , &
  dt     , propce ,                                     &
  frcxt  , prhyd  )

endif


!===============================================================================
! 4. SORTIE
!===============================================================================

write(nfecra,2000)

!===============================================================================
! 5. FORMATS
!===============================================================================


#if defined(_CS_LANG_FR)

 1000 format(/,                                                   &
' ----------------------------------------------------------- ',/,&
                                                                /,&
     3X,'** LECTURE DES FICHIERS SUITE PRINCIPAL ET AUXILIAIRE',/,&
     3X,'   ------------------------------------------------- ',/)
 2000 format(/,                                                   &
' ----------------------------------------------------------- ',/)

#else

 1000 format(/,                                                   &
' ----------------------------------------------------------- ',/,&
                                                                /,&
     3X,'** READING MAIN AND AUXILIARY RESTART FILES'          ,/,&
     3X,'   ----------------------------------------'          ,/)
 2000 format(/,                                                   &
' ----------------------------------------------------------- ',/)

#endif


return
end subroutine
