/*!
 * \file CPBIncNSSolver.cpp
 * \brief Main subroutines for solving Navier-Stokes incompressible flow.
 * \author F. Palacios, T. Economon
 * \version 7.0.2 "Blackbird"
 *
 * SU2 Project Website: https://su2code.github.io
 *
 * The SU2 Project is maintained by the SU2 Foundation
 * (http://su2foundation.org)
 *
 * Copyright 2012-2020, SU2 Contributors (cf. AUTHORS.md)
 *
 * SU2 is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * SU2 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with SU2. If not, see <http://www.gnu.org/licenses/>.
 */

#include "../../include/solvers/CPBIncNSSolver.hpp"
#include "../../include/variables/CPBIncNSVariable.hpp"
#include "../../../Common/include/toolboxes/printing_toolbox.hpp"

CPBIncNSSolver::CPBIncNSSolver(void) : CPBIncEulerSolver() {

  /*--- Basic array initialization ---*/

  CD_Visc = NULL; CL_Visc = NULL; CSF_Visc = NULL; CEff_Visc = NULL;
  CMx_Visc = NULL;   CMy_Visc = NULL;   CMz_Visc = NULL;
  CFx_Visc = NULL;   CFy_Visc = NULL;   CFz_Visc = NULL;
  CoPx_Visc = NULL;   CoPy_Visc = NULL;   CoPz_Visc = NULL;

  ForceViscous = NULL; MomentViscous = NULL; CSkinFriction = NULL;

  /*--- Surface based array initialization ---*/

  Surface_CL_Visc = NULL; Surface_CD_Visc = NULL; Surface_CSF_Visc = NULL; Surface_CEff_Visc = NULL;
  Surface_CFx_Visc = NULL;   Surface_CFy_Visc = NULL;   Surface_CFz_Visc = NULL;
  Surface_CMx_Visc = NULL;   Surface_CMy_Visc = NULL;   Surface_CMz_Visc = NULL;

  /*--- Rotorcraft simulation array initialization ---*/

  CMerit_Visc = NULL; CT_Visc = NULL; CQ_Visc = NULL;

}

CPBIncNSSolver::CPBIncNSSolver(CGeometry *geometry, CConfig *config, unsigned short iMesh) : CPBIncEulerSolver() {

  unsigned long iPoint, iVertex, iEdge, jPoint;
  unsigned short iVar, iDim, iMarker, nLineLets;
  ifstream restart_file;
  unsigned short nZone = geometry->GetnZone();
  bool restart   = (config->GetRestart() || config->GetRestart_Flow());
  int Unst_RestartIter;
  unsigned short iZone = config->GetiZone();
  bool dual_time = ((config->GetTime_Marching() == DT_STEPPING_1ST) ||
                    (config->GetTime_Marching() == DT_STEPPING_2ND));
  bool time_stepping = config->GetTime_Marching() == TIME_STEPPING;
  bool adjoint = (config->GetContinuous_Adjoint()) || (config->GetDiscrete_Adjoint());
  string filename_ = config->GetSolution_FileName();
  bool fsi     = config->GetFSI_Simulation();

  unsigned short direct_diff = config->GetDirectDiff();

  /* A grid is defined as dynamic if there's rigid grid movement or grid deformation AND the problem is time domain */
  dynamic_grid = config->GetDynamic_Grid();

  /*--- Check for a restart file to evaluate if there is a change in the angle of attack
   before computing all the non-dimesional quantities. ---*/

  if (!(!restart || (iMesh != MESH_0) || nZone > 1)) {

    /*--- Multizone problems require the number of the zone to be appended. ---*/

    if (nZone > 1) filename_ = config->GetMultizone_FileName(filename_, iZone, ".dat");

    /*--- Modify file name for a dual-time unsteady restart ---*/

    if (dual_time) {
      if (adjoint) Unst_RestartIter = SU2_TYPE::Int(config->GetUnst_AdjointIter())-1;
      else if (config->GetTime_Marching() == DT_STEPPING_1ST)
        Unst_RestartIter = SU2_TYPE::Int(config->GetRestart_Iter())-1;
      else Unst_RestartIter = SU2_TYPE::Int(config->GetRestart_Iter())-2;
      filename_ = config->GetUnsteady_FileName(filename_, Unst_RestartIter, ".dat");
    }

    /*--- Modify file name for a time stepping unsteady restart ---*/

    if (time_stepping) {
      if (adjoint) Unst_RestartIter = SU2_TYPE::Int(config->GetUnst_AdjointIter())-1;
      else Unst_RestartIter = SU2_TYPE::Int(config->GetRestart_Iter())-1;
      filename_ = config->GetUnsteady_FileName(filename_, Unst_RestartIter, ".dat");
    }

    /*--- Read and store the restart metadata. ---*/

    Read_SU2_Restart_Metadata(geometry, config, false, filename_);

  }

  /*--- Array initialization ---*/

  CD_Visc = NULL; CL_Visc = NULL; CSF_Visc = NULL; CEff_Visc = NULL;
  CMx_Visc = NULL;   CMy_Visc = NULL;   CMz_Visc = NULL;
  CFx_Visc = NULL;   CFy_Visc = NULL;   CFz_Visc = NULL;
  CoPx_Visc = NULL;   CoPy_Visc = NULL;   CoPz_Visc = NULL;

  Surface_CL_Visc = NULL; Surface_CD_Visc = NULL; Surface_CSF_Visc = NULL; Surface_CEff_Visc = NULL;
  Surface_CFx_Visc = NULL;   Surface_CFy_Visc = NULL;   Surface_CFz_Visc = NULL;
  Surface_CMx_Visc = NULL;   Surface_CMy_Visc = NULL;   Surface_CMz_Visc = NULL;

  CMerit_Visc = NULL;      CT_Visc = NULL;      CQ_Visc = NULL;
  MaxHF_Visc = NULL; ForceViscous = NULL; MomentViscous = NULL;
  CSkinFriction = NULL;    Cauchy_Serie = NULL; HF_Visc = NULL;

  /*--- Set the gamma value ---*/

  Gamma = config->GetGamma();
  Gamma_Minus_One = Gamma - 1.0;

  /*--- Define geometry constants in the solver structure
   * Incompressible flow, primitive variables (P, vx, vy, vz, rho, lamMu, EddyMu) --- */

  nDim = geometry->GetnDim();

  nVar = nDim; nPrimVar = nDim+4; nPrimVarGrad = nDim+2;

  /*--- Initialize nVarGrad for deallocation ---*/

  nVarGrad = nPrimVarGrad;

  nMarker      = config->GetnMarker_All();
  nPoint       = geometry->GetnPoint();
  nPointDomain = geometry->GetnPointDomain();
  nEdge        = geometry->GetnEdge();

  /*--- Store the number of vertices on each marker for deallocation later ---*/

  nVertex = new unsigned long[nMarker];
  for (iMarker = 0; iMarker < nMarker; iMarker++)
    nVertex[iMarker] = geometry->nVertex[iMarker];

  /*--- Fluid model intialization. ---*/

  FluidModel = NULL;

  /*--- Perform the non-dimensionalization for the flow equations using the
   specified reference values. ---*/

  SetNondimensionalization(config, iMesh);

  /*--- Define some auxiliar vector related with the residual ---*/

  Residual      = new su2double[nVar]; for (iVar = 0; iVar < nVar; iVar++) Residual[iVar]      = 0.0;
  Residual_RMS  = new su2double[nVar]; for (iVar = 0; iVar < nVar; iVar++) Residual_RMS[iVar]  = 0.0;
  Residual_Max  = new su2double[nVar]; for (iVar = 0; iVar < nVar; iVar++) Residual_Max[iVar]  = 0.0;
  Res_Conv      = new su2double[nVar]; for (iVar = 0; iVar < nVar; iVar++) Res_Conv[iVar]      = 0.0;
  Res_Visc      = new su2double[nVar]; for (iVar = 0; iVar < nVar; iVar++) Res_Visc[iVar]      = 0.0;
  Res_Sour      = new su2double[nVar]; for (iVar = 0; iVar < nVar; iVar++) Res_Sour[iVar]      = 0.0;

  /*--- Define some structures for locating max residuals ---*/

  Point_Max     = new unsigned long[nVar];  for (iVar = 0; iVar < nVar; iVar++) Point_Max[iVar]     = 0;
  Point_Max_Coord = new su2double*[nVar];
  for (iVar = 0; iVar < nVar; iVar++) {
    Point_Max_Coord[iVar] = new su2double[nDim];
    for (iDim = 0; iDim < nDim; iDim++) Point_Max_Coord[iVar][iDim] = 0.0;
  }

  /*--- Define some auxiliary vectors related to the solution ---*/

  Solution   = new su2double[nVar]; for (iVar = 0; iVar < nVar; iVar++) Solution[iVar]   = 0.0;
  Solution_i = new su2double[nVar]; for (iVar = 0; iVar < nVar; iVar++) Solution_i[iVar] = 0.0;
  Solution_j = new su2double[nVar]; for (iVar = 0; iVar < nVar; iVar++) Solution_j[iVar] = 0.0;

  /*--- Define some auxiliary vectors related to the geometry ---*/

  Vector   = new su2double[nDim]; for (iDim = 0; iDim < nDim; iDim++) Vector[iDim]   = 0.0;
  Vector_i = new su2double[nDim]; for (iDim = 0; iDim < nDim; iDim++) Vector_i[iDim] = 0.0;
  Vector_j = new su2double[nDim]; for (iDim = 0; iDim < nDim; iDim++) Vector_j[iDim] = 0.0;

  /*--- Define some auxiliary vectors related to the primitive solution ---*/

  Primitive   = new su2double[nPrimVar]; for (iVar = 0; iVar < nPrimVar; iVar++) Primitive[iVar]   = 0.0;
  Primitive_i = new su2double[nPrimVar]; for (iVar = 0; iVar < nPrimVar; iVar++) Primitive_i[iVar] = 0.0;
  Primitive_j = new su2double[nPrimVar]; for (iVar = 0; iVar < nPrimVar; iVar++) Primitive_j[iVar] = 0.0;

  /*--- Define some auxiliar vector related with the undivided lapalacian computation ---*/

  if (config->GetKind_ConvNumScheme_Flow() == SPACE_CENTERED) {
    iPoint_UndLapl = new su2double [nPoint];
    jPoint_UndLapl = new su2double [nPoint];
  }


  /*--- Initialize the solution and right hand side vectors for storing
   the residuals and updating the solution (always needed even for
   explicit schemes). ---*/

  LinSysSol.Initialize(nPoint, nPointDomain, nVar, 0.0);
  LinSysRes.Initialize(nPoint, nPointDomain, nVar, 0.0);

  /*--- Jacobians and vector structures for implicit computations ---*/

  if (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT) {

    Jacobian_i = new su2double* [nVar];
    Jacobian_j = new su2double* [nVar];
    for (iVar = 0; iVar < nVar; iVar++) {
      Jacobian_j[iVar] = new su2double [nVar];
      Jacobian_i[iVar] = new su2double [nVar];
    }

    if (rank == MASTER_NODE) cout << "Initialize Jacobian structure (Navier-Stokes). MG level: " << iMesh <<"." << endl;
    Jacobian.Initialize(nPoint, nPointDomain, nVar, nVar, true, geometry, config);

  }

  else {
    if (rank == MASTER_NODE)
      cout << "Explicit scheme. No Jacobian structure (Navier-Stokes). MG level: " << iMesh <<"." << endl;
  }

  /*--- Define some auxiliary vectors for computing flow variable
   gradients by least squares, S matrix := inv(R)*traspose(inv(R)),
   c vector := transpose(WA)*(Wb) ---*/

  if (config->GetLeastSquaresRequired()) {
    Smatrix = new su2double* [nDim];
    for (iDim = 0; iDim < nDim; iDim++)
      Smatrix[iDim] = new su2double [nDim];

    Cvector = new su2double* [nPrimVarGrad];
    for (iVar = 0; iVar < nPrimVarGrad; iVar++)
      Cvector[iVar] = new su2double [nDim];
  }

  /*--- Store the value of the characteristic primitive variables at the boundaries ---*/

  CharacPrimVar = new su2double** [nMarker];
  for (iMarker = 0; iMarker < nMarker; iMarker++) {
    CharacPrimVar[iMarker] = new su2double* [geometry->nVertex[iMarker]];
    for (iVertex = 0; iVertex < geometry->nVertex[iMarker]; iVertex++) {
      CharacPrimVar[iMarker][iVertex] = new su2double [nPrimVar];
      for (iVar = 0; iVar < nPrimVar; iVar++) {
        CharacPrimVar[iMarker][iVertex][iVar] = 0.0;
      }
    }
  }


  /*--- Inviscid force definition and coefficient in all the markers ---*/

  CPressure = new su2double* [nMarker];
  CPressureTarget = new su2double* [nMarker];
  for (iMarker = 0; iMarker < nMarker; iMarker++) {
    CPressure[iMarker] = new su2double [geometry->nVertex[iMarker]];
    CPressureTarget[iMarker] = new su2double [geometry->nVertex[iMarker]];
    for (iVertex = 0; iVertex < geometry->nVertex[iMarker]; iVertex++) {
      CPressure[iMarker][iVertex] = 0.0;
      CPressureTarget[iMarker][iVertex] = 0.0;
    }
  }

  /*--- Heat flux in all the markers ---*/

  HeatFlux = new su2double* [nMarker];
  HeatFluxTarget = new su2double* [nMarker];
  for (iMarker = 0; iMarker < nMarker; iMarker++) {
    HeatFlux[iMarker] = new su2double [geometry->nVertex[iMarker]];
    HeatFluxTarget[iMarker] = new su2double [geometry->nVertex[iMarker]];
    for (iVertex = 0; iVertex < geometry->nVertex[iMarker]; iVertex++) {
      HeatFlux[iMarker][iVertex] = 0.0;
      HeatFluxTarget[iMarker][iVertex] = 0.0;
    }
  }

  /*--- Y plus in all the markers ---*/

  YPlus = new su2double* [nMarker];
  for (iMarker = 0; iMarker < nMarker; iMarker++) {
    YPlus[iMarker] = new su2double [geometry->nVertex[iMarker]];
    for (iVertex = 0; iVertex < geometry->nVertex[iMarker]; iVertex++) {
      YPlus[iMarker][iVertex] = 0.0;
    }
  }

  /*--- Skin friction in all the markers ---*/

  CSkinFriction = new su2double** [nMarker];
  for (iMarker = 0; iMarker < nMarker; iMarker++) {
    CSkinFriction[iMarker] = new su2double*[nDim];
    for (iDim = 0; iDim < nDim; iDim++) {
      CSkinFriction[iMarker][iDim] = new su2double[geometry->nVertex[iMarker]];
      for (iVertex = 0; iVertex < geometry->nVertex[iMarker]; iVertex++) {
        CSkinFriction[iMarker][iDim][iVertex] = 0.0;
      }
    }
  }

  /*--- Store the value of the Total Pressure at the inlet BC ---*/

  Inlet_Ttotal = new su2double* [nMarker];
  for (iMarker = 0; iMarker < nMarker; iMarker++) {
    Inlet_Ttotal[iMarker] = new su2double [geometry->nVertex[iMarker]];
    for (iVertex = 0; iVertex < geometry->nVertex[iMarker]; iVertex++) {
      Inlet_Ttotal[iMarker][iVertex] = 0;
    }
  }

  /*--- Store the value of the Total Temperature at the inlet BC ---*/

  Inlet_Ptotal = new su2double* [nMarker];
  for (iMarker = 0; iMarker < nMarker; iMarker++) {
    Inlet_Ptotal[iMarker] = new su2double [geometry->nVertex[iMarker]];
    for (iVertex = 0; iVertex < geometry->nVertex[iMarker]; iVertex++) {
      Inlet_Ptotal[iMarker][iVertex] = 0;
    }
  }

  /*--- Store the value of the Flow direction at the inlet BC ---*/

  Inlet_FlowDir = new su2double** [nMarker];
  for (iMarker = 0; iMarker < nMarker; iMarker++) {
    Inlet_FlowDir[iMarker] = new su2double* [geometry->nVertex[iMarker]];
    for (iVertex = 0; iVertex < geometry->nVertex[iMarker]; iVertex++) {
      Inlet_FlowDir[iMarker][iVertex] = new su2double [nDim];
      for (iDim = 0; iDim < nDim; iDim++) {
        Inlet_FlowDir[iMarker][iVertex][iDim] = 0;
      }
    }
  }

  /*--- Non dimensional coefficients ---*/

  ForceInviscid  = new su2double[3];
  MomentInviscid = new su2double[3];
  CD_Inv      = new su2double[nMarker];
  CL_Inv      = new su2double[nMarker];
  CSF_Inv = new su2double[nMarker];
  CMx_Inv        = new su2double[nMarker];
  CMy_Inv        = new su2double[nMarker];
  CMz_Inv        = new su2double[nMarker];
  CEff_Inv       = new su2double[nMarker];
  CFx_Inv        = new su2double[nMarker];
  CFy_Inv        = new su2double[nMarker];
  CFz_Inv        = new su2double[nMarker];
  CoPx_Inv        = new su2double[nMarker];
  CoPy_Inv        = new su2double[nMarker];
  CoPz_Inv        = new su2double[nMarker];

  ForceMomentum  = new su2double[3];
  MomentMomentum = new su2double[3];
  CD_Mnt      = new su2double[nMarker];
  CL_Mnt      = new su2double[nMarker];
  CSF_Mnt = new su2double[nMarker];
  CMx_Mnt        = new su2double[nMarker];
  CMy_Mnt        = new su2double[nMarker];
  CMz_Mnt        = new su2double[nMarker];
  CEff_Mnt       = new su2double[nMarker];
  CFx_Mnt        = new su2double[nMarker];
  CFy_Mnt        = new su2double[nMarker];
  CFz_Mnt        = new su2double[nMarker];
  CoPx_Mnt        = new su2double[nMarker];
  CoPy_Mnt        = new su2double[nMarker];
  CoPz_Mnt        = new su2double[nMarker];

  ForceViscous     = new su2double[3];
  MomentViscous    = new su2double[3];
  CD_Visc       = new su2double[nMarker];
  CL_Visc       = new su2double[nMarker];
  CSF_Visc  = new su2double[nMarker];
  CMx_Visc         = new su2double[nMarker];
  CMy_Visc         = new su2double[nMarker];
  CMz_Visc         = new su2double[nMarker];
  CEff_Visc        = new su2double[nMarker];
  CFx_Visc         = new su2double[nMarker];
  CFy_Visc         = new su2double[nMarker];
  CFz_Visc         = new su2double[nMarker];
  CoPx_Visc         = new su2double[nMarker];
  CoPy_Visc         = new su2double[nMarker];
  CoPz_Visc         = new su2double[nMarker];

  Surface_CL_Inv      = new su2double[config->GetnMarker_Monitoring()];
  Surface_CD_Inv      = new su2double[config->GetnMarker_Monitoring()];
  Surface_CSF_Inv = new su2double[config->GetnMarker_Monitoring()];
  Surface_CEff_Inv       = new su2double[config->GetnMarker_Monitoring()];
  Surface_CFx_Inv        = new su2double[config->GetnMarker_Monitoring()];
  Surface_CFy_Inv        = new su2double[config->GetnMarker_Monitoring()];
  Surface_CFz_Inv        = new su2double[config->GetnMarker_Monitoring()];
  Surface_CMx_Inv        = new su2double[config->GetnMarker_Monitoring()];
  Surface_CMy_Inv        = new su2double[config->GetnMarker_Monitoring()];
  Surface_CMz_Inv        = new su2double[config->GetnMarker_Monitoring()];

  Surface_CL_Mnt      = new su2double[config->GetnMarker_Monitoring()];
  Surface_CD_Mnt      = new su2double[config->GetnMarker_Monitoring()];
  Surface_CSF_Mnt = new su2double[config->GetnMarker_Monitoring()];
  Surface_CEff_Mnt       = new su2double[config->GetnMarker_Monitoring()];
  Surface_CFx_Mnt        = new su2double[config->GetnMarker_Monitoring()];
  Surface_CFy_Mnt        = new su2double[config->GetnMarker_Monitoring()];
  Surface_CFz_Mnt        = new su2double[config->GetnMarker_Monitoring()];
  Surface_CMx_Mnt        = new su2double[config->GetnMarker_Monitoring()];
  Surface_CMy_Mnt        = new su2double[config->GetnMarker_Monitoring()];
  Surface_CMz_Mnt        = new su2double[config->GetnMarker_Monitoring()];

  Surface_CL          = new su2double[config->GetnMarker_Monitoring()];
  Surface_CD          = new su2double[config->GetnMarker_Monitoring()];
  Surface_CSF     = new su2double[config->GetnMarker_Monitoring()];
  Surface_CEff           = new su2double[config->GetnMarker_Monitoring()];
  Surface_CFx            = new su2double[config->GetnMarker_Monitoring()];
  Surface_CFy            = new su2double[config->GetnMarker_Monitoring()];
  Surface_CFz            = new su2double[config->GetnMarker_Monitoring()];
  Surface_CMx            = new su2double[config->GetnMarker_Monitoring()];
  Surface_CMy            = new su2double[config->GetnMarker_Monitoring()];
  Surface_CMz            = new su2double[config->GetnMarker_Monitoring()];

  Surface_CL_Visc      = new su2double[config->GetnMarker_Monitoring()];
  Surface_CD_Visc      = new su2double[config->GetnMarker_Monitoring()];
  Surface_CSF_Visc = new su2double[config->GetnMarker_Monitoring()];
  Surface_CEff_Visc       = new su2double[config->GetnMarker_Monitoring()];
  Surface_CFx_Visc        = new su2double[config->GetnMarker_Monitoring()];
  Surface_CFy_Visc        = new su2double[config->GetnMarker_Monitoring()];
  Surface_CFz_Visc        = new su2double[config->GetnMarker_Monitoring()];
  Surface_CMx_Visc        = new su2double[config->GetnMarker_Monitoring()];
  Surface_CMy_Visc        = new su2double[config->GetnMarker_Monitoring()];
  Surface_CMz_Visc        = new su2double[config->GetnMarker_Monitoring()];

  /*--- Rotorcraft coefficients ---*/

  CT_Inv           = new su2double[nMarker];
  CQ_Inv           = new su2double[nMarker];
  CMerit_Inv       = new su2double[nMarker];

  CT_Mnt           = new su2double[nMarker];
  CQ_Mnt           = new su2double[nMarker];
  CMerit_Mnt       = new su2double[nMarker];

  CMerit_Visc      = new su2double[nMarker];
  CT_Visc          = new su2double[nMarker];
  CQ_Visc          = new su2double[nMarker];

  /*--- Init total coefficients ---*/

  Total_CD       = 0.0;  Total_CL           = 0.0;  Total_CSF            = 0.0;
  Total_CMx      = 0.0;  Total_CMy          = 0.0;  Total_CMz            = 0.0;
  Total_CEff     = 0.0;
  Total_CFx      = 0.0;  Total_CFy          = 0.0;  Total_CFz            = 0.0;
  Total_CT       = 0.0;  Total_CQ           = 0.0;  Total_CMerit         = 0.0;
  Total_MaxHeat  = 0.0;  Total_Heat         = 0.0;  Total_ComboObj       = 0.0;
  Total_CpDiff   = 0.0;  Total_HeatFluxDiff = 0.0;  Total_Custom_ObjFunc = 0.0;
  AoA_Prev       = 0.0;
  Total_CL_Prev  = 0.0;  Total_CD_Prev      = 0.0;
  Total_CMx_Prev = 0.0;  Total_CMy_Prev     = 0.0;  Total_CMz_Prev       = 0.0;

  /*--- Coefficients for fixed lift mode. ---*/

  AoA_Prev = 0.0;
  Total_CL_Prev = 0.0; Total_CD_Prev = 0.0;
  Total_CMx_Prev = 0.0; Total_CMy_Prev = 0.0; Total_CMz_Prev = 0.0;

  /*--- Read farfield conditions from config ---*/

  Density_Inf     = config->GetDensity_FreeStreamND();
  Pressure_Inf    = config->GetPressure_FreeStreamND();
  Velocity_Inf    = config->GetVelocity_FreeStreamND();
  Viscosity_Inf   = config->GetViscosity_FreeStreamND();
  Tke_Inf         = config->GetTke_FreeStreamND();

  /*--- Initialize the secondary values for direct derivative approxiations ---*/

  switch(direct_diff){
    case NO_DERIVATIVE:
      break;
    case D_DENSITY:
      SU2_TYPE::SetDerivative(Density_Inf, 1.0);
      break;
    case D_PRESSURE:
      SU2_TYPE::SetDerivative(Pressure_Inf, 1.0);
      break;
    case D_TEMPERATURE:
      SU2_TYPE::SetDerivative(Temperature_Inf, 1.0);
      break;
    case D_VISCOSITY:
      SU2_TYPE::SetDerivative(Viscosity_Inf, 1.0);
      break;
    case D_MACH: case D_AOA:
    case D_SIDESLIP: case D_REYNOLDS:
    case D_TURB2LAM: case D_DESIGN:
      /*--- Already done in postprocessing of config ---*/
      break;
    default:
      break;
  }

  su2double *prefcoord, *Coord_i, dist_iref, min_dist;
  unsigned long min_Point;
  prefcoord = config->GetPRef_Coord();
  PRef_Point = nPoint+5;
  min_Point = nPoint+5;
  min_dist = 1e6;
  for (iPoint = 0; iPoint < nPoint; iPoint++) {
      Coord_i = geometry->nodes->GetCoord(iPoint);

      dist_iref = 0.0;
      for (iDim = 0; iDim < nDim; iDim++)
           dist_iref += (prefcoord[iDim] - Coord_i[iDim])*(prefcoord[iDim] - Coord_i[iDim]);

      dist_iref = sqrt(dist_iref);
      if (dist_iref < min_dist) {
          min_dist = dist_iref;
          min_Point = iPoint;
      }
  }

  if (PRef_Point > nPoint)
      PRef_Point = min_Point;

  /*--- Initialize the cauchy critera array for fixed CL mode ---*/

  if (config->GetFixed_CL_Mode())
    Cauchy_Serie = new su2double [config->GetCauchy_Elems()+1];

  /*--- Initialize the solution to the far-field state everywhere. ---*/

  nodes = new CPBIncNSVariable(Pressure_Inf, Velocity_Inf, nPoint, nDim, nVar, config);
  SetBaseClassPointerToNodes();

  /*--- Initialize the BGS residuals in FSI problems. ---*/
  if (fsi){
    Residual_BGS      = new su2double[nVar];         for (iVar = 0; iVar < nVar; iVar++) Residual_RMS[iVar]  = 0.0;
    Residual_Max_BGS  = new su2double[nVar];         for (iVar = 0; iVar < nVar; iVar++) Residual_Max_BGS[iVar]  = 0.0;

    /*--- Define some structures for locating max residuals ---*/

    Point_Max_BGS       = new unsigned long[nVar];  for (iVar = 0; iVar < nVar; iVar++) Point_Max_BGS[iVar]  = 0;
    Point_Max_Coord_BGS = new su2double*[nVar];
    for (iVar = 0; iVar < nVar; iVar++) {
      Point_Max_Coord_BGS[iVar] = new su2double[nDim];
      for (iDim = 0; iDim < nDim; iDim++) Point_Max_Coord_BGS[iVar][iDim] = 0.0;
    }
  }
  /*--- Define solver parameters needed for execution of destructor ---*/

  if (config->GetKind_ConvNumScheme_Flow() == SPACE_CENTERED) space_centered = true;
  else space_centered = false;

  if (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT) euler_implicit = true;
  else euler_implicit = false;

  if (config->GetKind_Gradient_Method() == WEIGHTED_LEAST_SQUARES) least_squares = true;
  else least_squares = false;

  /*--- Allocate velocities at every face. This is for momentum interpolation. ---*/

  FaceVelocity        = new su2double*[geometry->GetnEdge()];
  FaceVelocityCorrec  = new su2double*[geometry->GetnEdge()];
  su2double Normal[MAXNDIM];
  for (iEdge = 0; iEdge < geometry->GetnEdge(); iEdge++) {
    geometry->edges->GetNormal(iEdge, Normal);
    FaceVelocity[iEdge]       = new su2double [nDim];
    FaceVelocityCorrec[iEdge] = new su2double [nDim];
    iPoint = geometry->edges->GetNode(iEdge,0); 
    jPoint = geometry->edges->GetNode(iEdge,1);
    for (iDim = 0; iDim < nDim; iDim++) {
      FaceVelocity[iEdge][iDim]= 0.5*(nodes->GetSolution(iPoint,iDim)+nodes->GetSolution(jPoint,iDim));
      FaceVelocityCorrec[iEdge][iDim] = 0.0;
     }
   }
  
  /*--- Communicate and store volume and the number of neighbors for
   any dual CVs that lie on on periodic markers. ---*/

  for (unsigned short iPeriodic = 1; iPeriodic <= config->GetnMarker_Periodic()/2; iPeriodic++) {
    InitiatePeriodicComms(geometry, config, iPeriodic, PERIODIC_VOLUME);
    CompletePeriodicComms(geometry, config, iPeriodic, PERIODIC_VOLUME);
    InitiatePeriodicComms(geometry, config, iPeriodic, PERIODIC_NEIGHBORS);
    CompletePeriodicComms(geometry, config, iPeriodic, PERIODIC_NEIGHBORS);
  }
  SetImplicitPeriodic(euler_implicit);
  if (iMesh == MESH_0) SetRotatePeriodic(true);

  /*--- Perform the MPI communication of the solution ---*/

  InitiateComms(geometry, config, SOLUTION);
  CompleteComms(geometry, config, SOLUTION);
  InitiateComms(geometry, config, PRESSURE_VAR);
  CompleteComms(geometry, config, PRESSURE_VAR);

  /* Store the initial CFL number for all grid points. */

  const su2double CFL = config->GetCFL(MGLevel);
  for (iPoint = 0; iPoint < nPoint; iPoint++) {
    nodes->SetLocalCFL(iPoint, CFL);
  }
  Min_CFL_Local = CFL;
  Max_CFL_Local = CFL;
  Avg_CFL_Local = CFL;

  /*--- Add the solver name (max 8 characters) ---*/
  SolverName = "INC.FLOW";

}


CPBIncNSSolver::~CPBIncNSSolver(void) {

  unsigned short iMarker, iDim;

  unsigned long iVertex;

  if (CD_Visc != NULL)       delete [] CD_Visc;
  if (CL_Visc != NULL)       delete [] CL_Visc;
  if (CSF_Visc != NULL)  delete [] CSF_Visc;
  if (CMx_Visc != NULL)         delete [] CMx_Visc;
  if (CMy_Visc != NULL)         delete [] CMy_Visc;
  if (CMz_Visc != NULL)         delete [] CMz_Visc;
  if (CoPx_Visc != NULL)        delete [] CoPx_Visc;
  if (CoPy_Visc != NULL)        delete [] CoPy_Visc;
  if (CoPz_Visc != NULL)        delete [] CoPz_Visc;
  if (CFx_Visc != NULL)         delete [] CFx_Visc;
  if (CFy_Visc != NULL)         delete [] CFy_Visc;
  if (CFz_Visc != NULL)         delete [] CFz_Visc;
  if (CEff_Visc != NULL)        delete [] CEff_Visc;
  if (CMerit_Visc != NULL)      delete [] CMerit_Visc;
  if (CT_Visc != NULL)          delete [] CT_Visc;
  if (CQ_Visc != NULL)          delete [] CQ_Visc;
  if (HF_Visc != NULL)        delete [] HF_Visc;
  if (MaxHF_Visc != NULL) delete [] MaxHF_Visc;
  if (ForceViscous != NULL)     delete [] ForceViscous;
  if (MomentViscous != NULL)    delete [] MomentViscous;

  if (Surface_CL_Visc != NULL)      delete [] Surface_CL_Visc;
  if (Surface_CD_Visc != NULL)      delete [] Surface_CD_Visc;
  if (Surface_CSF_Visc != NULL) delete [] Surface_CSF_Visc;
  if (Surface_CEff_Visc != NULL)       delete [] Surface_CEff_Visc;
  if (Surface_CFx_Visc != NULL)        delete [] Surface_CFx_Visc;
  if (Surface_CFy_Visc != NULL)        delete [] Surface_CFy_Visc;
  if (Surface_CFz_Visc != NULL)        delete [] Surface_CFz_Visc;
  if (Surface_CMx_Visc != NULL)        delete [] Surface_CMx_Visc;
  if (Surface_CMy_Visc != NULL)        delete [] Surface_CMy_Visc;
  if (Surface_CMz_Visc != NULL)        delete [] Surface_CMz_Visc;

  if (Cauchy_Serie != NULL) delete [] Cauchy_Serie;

  if (CSkinFriction != NULL) {
    for (iMarker = 0; iMarker < nMarker; iMarker++) {
      for (iDim = 0; iDim < nDim; iDim++) {
        delete [] CSkinFriction[iMarker][iDim];
      }
      delete [] CSkinFriction[iMarker];
    }
    delete [] CSkinFriction;
  }

}

void CPBIncNSSolver::Preprocessing(CGeometry *geometry, CSolver **solver_container, CConfig *config, unsigned short iMesh, unsigned short iRKStep, unsigned short RunTime_EqSystem, bool Output) {

unsigned long iPoint, ErrorCounter = 0;
  su2double StrainMag = 0.0, Omega = 0.0, *Vorticity;

  unsigned long InnerIter     = config->GetInnerIter();
  bool cont_adjoint         = config->GetContinuous_Adjoint();
  bool disc_adjoint         = config->GetDiscrete_Adjoint();
  bool implicit             = (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT);
  bool center               = ((config->GetKind_ConvNumScheme_Flow() == SPACE_CENTERED) || (cont_adjoint && config->GetKind_ConvNumScheme_AdjFlow() == SPACE_CENTERED));
  bool center_jst           = center && config->GetKind_Centered_Flow() == JST;
  bool limiter_flow         = (config->GetKind_SlopeLimit_Flow() != NO_LIMITER) && (InnerIter <= config->GetLimiterIter());
  bool limiter_turb         = (config->GetKind_SlopeLimit_Turb() != NO_LIMITER) && (InnerIter <= config->GetLimiterIter());
  bool limiter_adjflow      = (cont_adjoint && (config->GetKind_SlopeLimit_AdjFlow() != NO_LIMITER) && (InnerIter <= config->GetLimiterIter()));
  bool van_albada           = config->GetKind_SlopeLimit_Flow() == VAN_ALBADA_EDGE;
  bool outlet               = ((config->GetnMarker_Outlet() != 0));

  /*--- Set the primitive variables ---*/

  ErrorCounter = SetPrimitive_Variables(solver_container, config, Output);

  /*--- Compute gradient for MUSCL reconstruction. ---*/

  if (config->GetReconstructionGradientRequired() && (iMesh == MESH_0)) {
    if (config->GetKind_Gradient_Method_Recon() == GREEN_GAUSS)
      SetPrimitive_Gradient_GG(geometry, config, true);
    if (config->GetKind_Gradient_Method_Recon() == LEAST_SQUARES)
      SetPrimitive_Gradient_LS(geometry, config, true);
    if (config->GetKind_Gradient_Method_Recon() == WEIGHTED_LEAST_SQUARES)
      SetPrimitive_Gradient_LS(geometry, config, true);
  }

  /*--- Compute gradient of the primitive variables ---*/

  if (config->GetKind_Gradient_Method() == GREEN_GAUSS) {
    SetPrimitive_Gradient_GG(geometry, config, false);
  }
  if (config->GetKind_Gradient_Method() == WEIGHTED_LEAST_SQUARES) {
    SetPrimitive_Gradient_LS(geometry, config, false);
  }

  /*--- Compute the limiter in case we need it in the turbulence model
   or to limit the viscous terms (check this logic with JST and 2nd order turbulence model) ---*/

  if ((iMesh == MESH_0) && (limiter_flow || limiter_turb || limiter_adjflow)
      && !Output && !van_albada) { SetPrimitive_Limiter(geometry, config); }

  /*--- Evaluate the vorticity and strain rate magnitude ---*/

  solver_container[FLOW_SOL]->GetNodes()->SetVorticity_StrainMag();

  StrainMag_Max = 0.0; Omega_Max = 0.0;
  for (iPoint = 0; iPoint < nPoint; iPoint++) {

    StrainMag = solver_container[FLOW_SOL]->GetNodes()->GetStrainMag(iPoint);
    Vorticity = solver_container[FLOW_SOL]->GetNodes()->GetVorticity(iPoint);
    Omega = sqrt(Vorticity[0]*Vorticity[0]+ Vorticity[1]*Vorticity[1]+ Vorticity[2]*Vorticity[2]);

    StrainMag_Max = max(StrainMag_Max, StrainMag);
    Omega_Max = max(Omega_Max, Omega);

    nodes->ResetStrongBC(iPoint);

  }

  /*--- Initialize the Jacobian matrices ---*/

  if (implicit && !disc_adjoint) Jacobian.SetValZero();

  /*--- Error message ---*/
  if (config->GetComm_Level() == COMM_FULL) {

#ifdef HAVE_MPI
    unsigned long MyErrorCounter = ErrorCounter; ErrorCounter = 0;
    su2double MyOmega_Max = Omega_Max; Omega_Max = 0.0;
    su2double MyStrainMag_Max = StrainMag_Max; StrainMag_Max = 0.0;

    SU2_MPI::Allreduce(&MyErrorCounter, &ErrorCounter, 1, MPI_UNSIGNED_LONG, MPI_SUM, MPI_COMM_WORLD);
    SU2_MPI::Allreduce(&MyStrainMag_Max, &StrainMag_Max, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
    SU2_MPI::Allreduce(&MyOmega_Max, &Omega_Max, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
#endif

    if (iMesh == MESH_0)
      config->SetNonphysical_Points(ErrorCounter);

  }

  SetResMassFluxZero();

}


unsigned long CPBIncNSSolver::SetPrimitive_Variables(CSolver **solver_container, CConfig *config, bool Output) {

  unsigned long iPoint, nonPhysicalPoints = 0;
  su2double eddy_visc = 0.0, turb_ke = 0.0, DES_LengthScale = 0.0;
  unsigned short turb_model = config->GetKind_Turb_Model();
  bool physical = true;

  bool tkeNeeded = ((turb_model == SST) || (turb_model == SST_SUST));

  for (iPoint = 0; iPoint < nPoint; iPoint++) {

    /*--- Retrieve the value of the kinetic energy (if needed) ---*/

    if (turb_model != NONE && solver_container[TURB_SOL] != nullptr) {
      eddy_visc = solver_container[TURB_SOL]->GetNodes()->GetmuT(iPoint);
      if (tkeNeeded) turb_ke = solver_container[TURB_SOL]->GetNodes()->GetSolution(iPoint,0);

      if (config->GetKind_HybridRANSLES() != NO_HYBRIDRANSLES){
        DES_LengthScale = solver_container[TURB_SOL]->GetNodes()->GetDES_LengthScale(iPoint);
      }
    }

    /*--- Incompressible flow, primitive variables --- */

    physical = static_cast<CPBIncNSVariable*>(nodes)->SetPrimVar(iPoint,Density_Inf, Viscosity_Inf, eddy_visc, turb_ke, config);

    /* Check for non-realizable states for reporting. */

    if (!physical) nonPhysicalPoints++;

    /*--- Set the DES length scale (not used here) ---*/

    //nodes->SetDES_LengthScale(iPoint,DES_LengthScale);

    /*--- Initialize the convective, source and viscous residual vector ---*/

    if (!Output) LinSysRes.SetBlock_Zero(iPoint);

  }

  return nonPhysicalPoints;


}

void CPBIncNSSolver::Postprocessing(CGeometry *geometry, CSolver **solver_container, CConfig *config,
                                  unsigned short iMesh) {

bool RightSol = true;
unsigned long iPoint, ErrorCounter = 0;

  /*--- Set the current estimate of velocity as a primitive variable, needed for momentum interpolation.
   * -- This does not change the pressure, it remains the same as the old value .i.e. previous (pseudo)time step. ---*/
  if (iMesh == MESH_0) ErrorCounter = SetPrimitive_Variables(solver_container, config, true);

  /*--- Compute gradients to be used in Rhie Chow interpolation ---*/

    /*--- Gradient computation ---*/

    if (config->GetKind_Gradient_Method() == GREEN_GAUSS) {
      SetPrimitive_Gradient_GG(geometry, config);
    }
    if (config->GetKind_Gradient_Method() == WEIGHTED_LEAST_SQUARES) {
      SetPrimitive_Gradient_LS(geometry, config);
    }

    /*--- Evaluate the vorticity and strain rate magnitude ---*/

    solver_container[FLOW_SOL]->GetNodes()->SetVorticity_StrainMag();
}


void CPBIncNSSolver::Viscous_Residual(CGeometry *geometry, CSolver **solver_container, CNumerics **numerics_container,
                        CConfig *config, unsigned short iMesh, unsigned short iRKStep) {

  CNumerics* numerics = numerics_container[VISC_TERM];

  unsigned long iPoint, jPoint, iEdge;

  bool implicit = (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT);
  unsigned short turb_model = config->GetKind_Turb_Model();
  bool print = false;

  for (iEdge = 0; iEdge < geometry->GetnEdge(); iEdge++) {

    /*--- Points, coordinates and normal vector in edge ---*/

    iPoint = geometry->edges->GetNode(iEdge,0);
    jPoint = geometry->edges->GetNode(iEdge,1);
    numerics->SetCoord(geometry->nodes->GetCoord(iPoint),
                       geometry->nodes->GetCoord(jPoint));
    numerics->SetNormal(geometry->edges->GetNormal(iEdge));

    /*--- Primitive and secondary variables ---*/

    numerics->SetPrimitive(nodes->GetPrimitive(iPoint),
                           nodes->GetPrimitive(jPoint));

    /*--- Gradient and limiters ---*/

    numerics->SetPrimVarGradient(nodes->GetGradient_Primitive(iPoint),
                                 nodes->GetGradient_Primitive(jPoint));

    /*--- Turbulent kinetic energy ---*/

    if ((config->GetKind_Turb_Model() == SST) || (config->GetKind_Turb_Model() == SST_SUST))
      numerics->SetTurbKineticEnergy(solver_container[TURB_SOL]->GetNodes()->GetSolution(iPoint,0),
                                     solver_container[TURB_SOL]->GetNodes()->GetSolution(jPoint,0));

    /*--- Compute and update residual ---*/

    auto residual = numerics->ComputeResidual(config);

    LinSysRes.SubtractBlock(iPoint, residual);
    LinSysRes.AddBlock(jPoint, residual);

    /*--- Implicit part ---*/

    if (implicit) {
      Jacobian.UpdateBlocksSub(iEdge, iPoint, jPoint, residual.jacobian_i, residual.jacobian_j);
    }

  }
}



void CPBIncNSSolver::SetTime_Step(CGeometry *geometry, CSolver **solver_container, CConfig *config,
                    unsigned short iMesh, unsigned long Iteration) {

  su2double *Normal, Area, Vol, Length, Mean_ProjVel = 0.0, Mean_Vel, Mean_Density,
  Mean_BetaInc2 = 4.1, Lambda, Local_Delta_Time, Mean_DensityInc, GradVel, Lambda1,
  Mean_LaminarVisc, Mean_EddyVisc, Local_Delta_Time_Visc, K_v = 0.25, ProjVel_i, ProjVel_j,
  Global_Delta_Time = 1E6, Global_Delta_UnstTimeND, ProjVel, RefProjFlux, MinRefProjFlux;

  unsigned long iEdge, iVertex, iPoint, jPoint;
  unsigned short iDim, iMarker, iVar;

  bool implicit      = (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT);
  bool grid_movement = config->GetGrid_Movement();
  bool time_steping  = config->GetTime_Marching() == TIME_STEPPING;
  bool dual_time     = ((config->GetTime_Marching() == DT_STEPPING_1ST) ||
                    (config->GetTime_Marching() == DT_STEPPING_2ND));

  Min_Delta_Time = 1.E6; Max_Delta_Time = 0.0; MinRefProjFlux = 0.0;

  Normal = new su2double [nDim];

  /*--- Set maximum inviscid eigenvalue to zero, and compute sound speed ---*/

  for (iPoint = 0; iPoint < nPointDomain; iPoint++){
    nodes->SetMax_Lambda_Inv(iPoint,0.0);
    nodes->SetMax_Lambda_Visc(iPoint, 0.0);
  }

  /*--- Loop interior edges ---*/

  for (iEdge = 0; iEdge < geometry->GetnEdge(); iEdge++) {

    /*--- Point identification, Normal vector and area ---*/

    iPoint = geometry->edges->GetNode(iEdge,0);
    jPoint = geometry->edges->GetNode(iEdge,1);

    geometry->edges->GetNormal(iEdge,Normal);

    Area = 0.0;
    for (iDim = 0; iDim < nDim; iDim++) Area += Normal[iDim]*Normal[iDim];
    Area = sqrt(Area);

    /*--- Compute flux across the cell ---*/

    Mean_Density = 0.5*(nodes->GetDensity(iPoint) + nodes->GetDensity(jPoint));
    Mean_ProjVel = 0.0;
    for (iVar = 0; iVar < nVar; iVar++) {
        Mean_Vel = 0.5*(nodes->GetVelocity(iPoint, iVar) + nodes->GetVelocity(jPoint,iVar));
        Mean_ProjVel += Mean_Density*Mean_Vel*Normal[iVar];
    }
    RefProjFlux = fabs(config->GetInc_Velocity_Ref()*Area);
    MinRefProjFlux = max(RefProjFlux, MinRefProjFlux);

    /*--- Adjustment for grid movement ---*/

    if (dynamic_grid) {
      su2double *GridVel_i = geometry->nodes->GetGridVel(iPoint);
      su2double *GridVel_j = geometry->nodes->GetGridVel(jPoint);
      ProjVel_i = 0.0; ProjVel_j =0.0;
      for (iDim = 0; iDim < nDim; iDim++) {
        ProjVel_i += GridVel_i[iDim]*Normal[iDim];
        ProjVel_j += GridVel_j[iDim]*Normal[iDim];
      }
      Mean_ProjVel -= 0.5 * (ProjVel_i + ProjVel_j);
    }

    Lambda = fabs(Mean_ProjVel) + fabs(RefProjFlux);

    /*--- Inviscid contribution ---*/

    if (geometry->nodes->GetDomain(iPoint)) nodes->AddMax_Lambda_Inv(iPoint,Lambda+EPS);
    if (geometry->nodes->GetDomain(jPoint)) nodes->AddMax_Lambda_Inv(jPoint,Lambda+EPS);


    /*--- Viscous contribution ---*/

    Mean_LaminarVisc          = 0.5*(nodes->GetLaminarViscosity(iPoint)    + nodes->GetLaminarViscosity(jPoint));
    Mean_EddyVisc             = 0.5*(nodes->GetEddyViscosity(iPoint)       + nodes->GetEddyViscosity(jPoint));
    Mean_Density              = 0.5*(nodes->GetDensity(iPoint)             + nodes->GetDensity(jPoint));

    Lambda = (4.0/3.0)*(Mean_LaminarVisc + Mean_EddyVisc)*Area*Area/Mean_Density;

    if (geometry->nodes->GetDomain(iPoint)) nodes->AddMax_Lambda_Visc(iPoint,Lambda);
    if (geometry->nodes->GetDomain(jPoint)) nodes->AddMax_Lambda_Visc(jPoint,Lambda);

  }

  /*--- Loop boundary edges ---*/

  for (iMarker = 0; iMarker < geometry->GetnMarker(); iMarker++) {
    for (iVertex = 0; iVertex < geometry->GetnVertex(iMarker); iVertex++) {

      /*--- Point identification, Normal vector and area ---*/

      iPoint = geometry->vertex[iMarker][iVertex]->GetNode();
      geometry->vertex[iMarker][iVertex]->GetNormal(Normal);

      Area = 0.0;
      for (iDim = 0; iDim < nDim; iDim++) Area += Normal[iDim]*Normal[iDim];
      Area = sqrt(Area);

      /*--- Compute flux across the cell ---*/
      Mean_Density = nodes->GetDensity(iPoint);
      Mean_ProjVel = 0.0;
      for (iVar = 0; iVar < nVar; iVar++) {
          Mean_Vel = nodes->GetVelocity(iPoint,iVar);
          Mean_ProjVel += Mean_Density*Mean_Vel*Normal[iVar];
      }
      RefProjFlux = fabs(config->GetInc_Velocity_Ref()*Area);

      MinRefProjFlux = max(RefProjFlux, MinRefProjFlux);

      /*--- Adjustment for grid movement ---*/

      if (dynamic_grid) {
        su2double *GridVel = geometry->nodes->GetGridVel(iPoint);
        ProjVel = 0.0;
        for (iDim = 0; iDim < nDim; iDim++)
          ProjVel += GridVel[iDim]*Normal[iDim];
        Mean_ProjVel -= ProjVel;
      }

      Lambda = fabs(Mean_ProjVel) + fabs(RefProjFlux);

      if (geometry->nodes->GetDomain(iPoint)) {
        nodes->AddMax_Lambda_Inv(iPoint,Lambda+EPS);
      }

      /*--- Viscous contribution ---*/

      Mean_LaminarVisc          = nodes->GetLaminarViscosity(iPoint);
      Mean_EddyVisc             = nodes->GetEddyViscosity(iPoint);
      Mean_Density              = nodes->GetDensity(iPoint);

      Lambda = (4.0/3.0)*(Mean_LaminarVisc + Mean_EddyVisc)*Area*Area/Mean_Density;

      if (geometry->nodes->GetDomain(iPoint)) nodes->AddMax_Lambda_Visc(iPoint,Lambda);
    }
  }

  /*--- Each element uses their own speed, steady state simulation ---*/



  for (iPoint = 0; iPoint < nPointDomain; iPoint++) {

    Vol = geometry->nodes->GetVolume(iPoint);

    if (Vol != 0.0) {
      Local_Delta_Time = config->GetCFL(iMesh)*Vol / nodes->GetMax_Lambda_Inv(iPoint);
      Local_Delta_Time_Visc = config->GetCFL(iMesh)*K_v*Vol*Vol/ nodes->GetMax_Lambda_Visc(iPoint);
      Local_Delta_Time = min(Local_Delta_Time, Local_Delta_Time_Visc);
      Global_Delta_Time = min(Global_Delta_Time, Local_Delta_Time);
      Min_Delta_Time = min(Min_Delta_Time, Local_Delta_Time);
      Max_Delta_Time = max(Max_Delta_Time, Local_Delta_Time);
      if (Local_Delta_Time > config->GetMax_DeltaTime())
        Local_Delta_Time = config->GetMax_DeltaTime();
      nodes->SetDelta_Time(iPoint, Local_Delta_Time);
    }
    else {
      nodes->SetDelta_Time(iPoint, 0.0);
    }
  }

  /*--- Compute the max and the min dt (in parallel) ---*/
  if (config->GetComm_Level() == COMM_FULL) {
#ifdef HAVE_MPI
    su2double rbuf_time, sbuf_time;
    sbuf_time = Min_Delta_Time;
    SU2_MPI::Reduce(&sbuf_time, &rbuf_time, 1, MPI_DOUBLE, MPI_MIN, MASTER_NODE, MPI_COMM_WORLD);
    SU2_MPI::Bcast(&rbuf_time, 1, MPI_DOUBLE, MASTER_NODE, MPI_COMM_WORLD);
    Min_Delta_Time = rbuf_time;

    sbuf_time = Max_Delta_Time;
    SU2_MPI::Reduce(&sbuf_time, &rbuf_time, 1, MPI_DOUBLE, MPI_MAX, MASTER_NODE, MPI_COMM_WORLD);
    SU2_MPI::Bcast(&rbuf_time, 1, MPI_DOUBLE, MASTER_NODE, MPI_COMM_WORLD);
    Max_Delta_Time = rbuf_time;
#endif
  }

  /*--- For exact time solution use the minimum delta time of the whole mesh ---*/
  if (config->GetTime_Marching() == TIME_STEPPING) {
#ifdef HAVE_MPI
    su2double rbuf_time, sbuf_time;
    sbuf_time = Global_Delta_Time;
    SU2_MPI::Reduce(&sbuf_time, &rbuf_time, 1, MPI_DOUBLE, MPI_MIN, MASTER_NODE, MPI_COMM_WORLD);
    SU2_MPI::Bcast(&rbuf_time, 1, MPI_DOUBLE, MASTER_NODE, MPI_COMM_WORLD);
    Global_Delta_Time = rbuf_time;
#endif
    for (iPoint = 0; iPoint < nPointDomain; iPoint++)
      nodes->SetDelta_Time(iPoint, Global_Delta_Time);
  }

  /*--- Recompute the unsteady time step for the dual time strategy
   if the unsteady CFL is diferent from 0 ---*/
  if ((dual_time) && (Iteration == 0) && (config->GetUnst_CFL() != 0.0) && (iMesh == MESH_0)) {
    Global_Delta_UnstTimeND = config->GetUnst_CFL()*Global_Delta_Time/config->GetCFL(iMesh);

#ifdef HAVE_MPI
    su2double rbuf_time, sbuf_time;
    sbuf_time = Global_Delta_UnstTimeND;
    SU2_MPI::Reduce(&sbuf_time, &rbuf_time, 1, MPI_DOUBLE, MPI_MIN, MASTER_NODE, MPI_COMM_WORLD);
    SU2_MPI::Bcast(&rbuf_time, 1, MPI_DOUBLE, MASTER_NODE, MPI_COMM_WORLD);
    Global_Delta_UnstTimeND = rbuf_time;
#endif
    config->SetDelta_UnstTimeND(Global_Delta_UnstTimeND);
  }

  /*--- The pseudo local time (explicit integration) cannot be greater than the physical time ---*/
  if (dual_time)
    for (iPoint = 0; iPoint < nPointDomain; iPoint++) {
      if (!implicit) {
        Local_Delta_Time = min((2.0/3.0)*config->GetDelta_UnstTimeND(), nodes->GetDelta_Time(iPoint));
        nodes->SetDelta_Time(iPoint, Local_Delta_Time);
      }
    }

   delete [] Normal;
}

void CPBIncNSSolver::BC_HeatFlux_Wall(CGeometry *geometry, CSolver **solver_container, CNumerics *conv_numerics, CNumerics *visc_numerics, CConfig *config, unsigned short val_marker){


  unsigned short iDim, iVar, jVar;// Wall_Function;
  unsigned long iVertex, iPoint, total_index, Point_Normal;

  su2double *GridVel, *Normal, Area, Wall_HeatFlux;
  su2double *Coord_i, *Coord_j, dist_ij, delP, Pressure_j, Pressure_i;

  bool implicit      = (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT);
  Normal = new su2double [nDim];
  /*--- Identify the boundary by string name ---*/

  string Marker_Tag = config->GetMarker_All_TagBound(val_marker);

  /*--- Loop over all of the vertices on this boundary marker ---*/

  for (iVertex = 0; iVertex < geometry->nVertex[val_marker]; iVertex++) {
    iPoint = geometry->vertex[val_marker][iVertex]->GetNode();

    /*--- Check if the node belongs to the domain (i.e, not a halo node) ---*/

    if (geometry->nodes->GetDomain(iPoint)) {

      /*--- Compute dual-grid area and boundary normal ---*/

      geometry->vertex[val_marker][iVertex]->GetNormal(Normal);

      Area = 0.0;
      for (iDim = 0; iDim < nDim; iDim++)
        Area += Normal[iDim]*Normal[iDim];
      Area = sqrt (Area);

      /*--- Initialize the convective & viscous residuals to zero ---*/

      for (iVar = 0; iVar < nVar; iVar++) {
        Res_Conv[iVar] = 0.0;
        Res_Visc[iVar] = 0.0;
        if (implicit) {
          for (jVar = 0; jVar < nVar; jVar++)
            Jacobian_i[iVar][jVar] = 0.0;
        }
      }

      /*--- Store the corrected velocity at the wall which will
       be zero (v = 0), unless there are moving walls (v = u_wall)---*/

      if (dynamic_grid) {
        GridVel = geometry->nodes->GetGridVel(iPoint);
        for (iDim = 0; iDim < nDim; iDim++) Vector[iDim] = GridVel[iDim];
      } else {
        for (iDim = 0; iDim < nDim; iDim++) Vector[iDim] = 0.0;
      }

      /*--- Impose the value of the velocity as a strong boundary
       condition (Dirichlet). Fix the velocity and remove any
       contribution to the residual at this node. ---*/

      nodes->SetVelocity_Old(iPoint, Vector);

      for (iDim = 0; iDim < nDim; iDim++)
        LinSysRes.SetBlock_Zero(iPoint, iDim);

      nodes->SetStrongBC(iPoint);

      /*--- Enforce the no-slip boundary condition in a strong way by
       modifying the velocity-rows of the Jacobian (1 on the diagonal). ---*/

      if (implicit) {
        for (iVar = 0; iVar < nDim; iVar++) {
          total_index = iPoint*nVar+iVar;
          Jacobian.DeleteValsRowi(total_index);
        }
      }
    }
  }

  delete [] Normal;
}



void CPBIncNSSolver::BC_Isothermal_Wall(CGeometry *geometry, CSolver **solver_container, CNumerics *conv_numerics, CNumerics *visc_numerics, CConfig *config, unsigned short val_marker){


    /*--- No energy equation => Both adiabatic and isothermal walls become the usual no-slip boundary. ---*/
    BC_HeatFlux_Wall(geometry, solver_container, conv_numerics, visc_numerics, config, val_marker);

}

void CPBIncNSSolver::Friction_Forces(CGeometry *geometry, CConfig *config){


unsigned long iVertex, iPoint, iPointNormal;
  unsigned short Boundary, Monitoring, iMarker, iMarker_Monitoring, iDim, jDim;
  su2double Viscosity = 0.0, div_vel, *Normal, MomentDist[3] = {0.0, 0.0, 0.0}, WallDist[3] = {0.0, 0.0, 0.0},
  *Coord, *Coord_Normal, Area, WallShearStress, TauNormal, factor, RefVel2 = 0.0,
  RefDensity = 0.0, Density = 0.0, WallDistMod, FrictionVel, UnitNormal[3] = {0.0, 0.0, 0.0}, TauElem[3] = {0.0, 0.0, 0.0}, TauTangent[3] = {0.0, 0.0, 0.0},
  Tau[3][3] = {{0.0, 0.0, 0.0},{0.0, 0.0, 0.0},{0.0, 0.0, 0.0}}, Force[3] = {0.0, 0.0, 0.0},
  Grad_Vel[3][3] = {{0.0, 0.0, 0.0},{0.0, 0.0, 0.0},{0.0, 0.0, 0.0}},
  delta[3][3] = {{1.0, 0.0, 0.0},{0.0,1.0,0.0},{0.0,0.0,1.0}},
  Grad_Temp[3] = {0.0, 0.0, 0.0}, GradTemperature, thermal_conductivity, MaxNorm = 8.0;
  su2double MomentX_Force[3] = {0.0,0.0,0.0}, MomentY_Force[3] = {0.0,0.0,0.0}, MomentZ_Force[3] = {0.0,0.0,0.0};
  su2double AxiFactor;
  Normal = new su2double [nDim];


#ifdef HAVE_MPI
  su2double MyAllBound_CD_Visc, MyAllBound_CL_Visc, MyAllBound_CSF_Visc, MyAllBound_CMx_Visc, MyAllBound_CMy_Visc, MyAllBound_CMz_Visc, MyAllBound_CoPx_Visc, MyAllBound_CoPy_Visc, MyAllBound_CoPz_Visc, MyAllBound_CFx_Visc, MyAllBound_CFy_Visc, MyAllBound_CFz_Visc, MyAllBound_CT_Visc, MyAllBound_CQ_Visc, MyAllBound_HF_Visc, MyAllBound_MaxHF_Visc, *MySurface_CL_Visc = NULL, *MySurface_CD_Visc = NULL, *MySurface_CSF_Visc = NULL, *MySurface_CEff_Visc = NULL, *MySurface_CFx_Visc = NULL, *MySurface_CFy_Visc = NULL, *MySurface_CFz_Visc = NULL, *MySurface_CMx_Visc = NULL, *MySurface_CMy_Visc = NULL, *MySurface_CMz_Visc = NULL, *MySurface_HF_Visc = NULL, *MySurface_MaxHF_Visc = NULL;
#endif

  string Marker_Tag, Monitoring_Tag;

  su2double Alpha     = config->GetAoA()*PI_NUMBER/180.0;
  su2double Beta      = config->GetAoS()*PI_NUMBER/180.0;
  su2double RefArea   = config->GetRefArea();
  su2double RefLength = config->GetRefLength();
  su2double *Origin = NULL;

  if (config->GetnMarker_Monitoring() != 0) { Origin = config->GetRefOriginMoment(0); }

  bool axisymmetric = config->GetAxisymmetric();
  bool energy       = config->GetEnergy_Equation();

  /*--- Evaluate reference values for non-dimensionalization.
   For dimensional or non-dim based on initial values, use
   the far-field state (inf). For a custom non-dim based
   on user-provided reference values, use the ref values
   to compute the forces. ---*/

  if ((config->GetRef_Inc_NonDim() == DIMENSIONAL) ||
      (config->GetRef_Inc_NonDim() == INITIAL_VALUES)) {
    RefDensity  = Density_Inf;
    RefVel2 = 0.0;
    for (iDim = 0; iDim < nDim; iDim++)
      RefVel2  += Velocity_Inf[iDim]*Velocity_Inf[iDim];
  }
  else if (config->GetRef_Inc_NonDim() == REFERENCE_VALUES) {
    RefDensity = config->GetInc_Density_Ref();
    RefVel2    = config->GetInc_Velocity_Ref()*config->GetInc_Velocity_Ref();
  }

  /*--- Compute factor for force coefficients. ---*/

  factor = 1.0 / (0.5*RefDensity*RefArea*RefVel2);

  /*--- Variables initialization ---*/

  AllBound_CD_Visc = 0.0;    AllBound_CL_Visc = 0.0;       AllBound_CSF_Visc = 0.0;
  AllBound_CMx_Visc = 0.0;      AllBound_CMy_Visc = 0.0;         AllBound_CMz_Visc = 0.0;
  AllBound_CFx_Visc = 0.0;      AllBound_CFy_Visc = 0.0;         AllBound_CFz_Visc = 0.0;
  AllBound_CoPx_Visc = 0.0;      AllBound_CoPy_Visc = 0.0;         AllBound_CoPz_Visc = 0.0;
  AllBound_CT_Visc = 0.0;       AllBound_CQ_Visc = 0.0;          AllBound_CMerit_Visc = 0.0;
  AllBound_HF_Visc = 0.0; AllBound_MaxHF_Visc = 0.0; AllBound_CEff_Visc = 0.0;

  for (iMarker_Monitoring = 0; iMarker_Monitoring < config->GetnMarker_Monitoring(); iMarker_Monitoring++) {
    Surface_CL_Visc[iMarker_Monitoring]      = 0.0; Surface_CD_Visc[iMarker_Monitoring]      = 0.0;
    Surface_CSF_Visc[iMarker_Monitoring] = 0.0; Surface_CEff_Visc[iMarker_Monitoring]       = 0.0;
    Surface_CFx_Visc[iMarker_Monitoring]        = 0.0; Surface_CFy_Visc[iMarker_Monitoring]        = 0.0;
    Surface_CFz_Visc[iMarker_Monitoring]        = 0.0; Surface_CMx_Visc[iMarker_Monitoring]        = 0.0;
    Surface_CMy_Visc[iMarker_Monitoring]        = 0.0; Surface_CMz_Visc[iMarker_Monitoring]        = 0.0;
  }

  /*--- Loop over the Navier-Stokes markers ---*/

  for (iMarker = 0; iMarker < nMarker; iMarker++) {

    Boundary = config->GetMarker_All_KindBC(iMarker);
    Monitoring = config->GetMarker_All_Monitoring(iMarker);

    /*--- Obtain the origin for the moment computation for a particular marker ---*/

    if (Monitoring == YES) {
      for (iMarker_Monitoring = 0; iMarker_Monitoring < config->GetnMarker_Monitoring(); iMarker_Monitoring++) {
        Monitoring_Tag = config->GetMarker_Monitoring_TagBound(iMarker_Monitoring);
        Marker_Tag = config->GetMarker_All_TagBound(iMarker);
        if (Marker_Tag == Monitoring_Tag)
          Origin = config->GetRefOriginMoment(iMarker_Monitoring);
      }
    }

    if ((Boundary == HEAT_FLUX) || (Boundary == ISOTHERMAL)) {

      /*--- Forces initialization at each Marker ---*/

      CD_Visc[iMarker] = 0.0; CL_Visc[iMarker] = 0.0;       CSF_Visc[iMarker] = 0.0;
      CMx_Visc[iMarker] = 0.0;   CMy_Visc[iMarker] = 0.0;         CMz_Visc[iMarker] = 0.0;
      CFx_Visc[iMarker] = 0.0;   CFy_Visc[iMarker] = 0.0;         CFz_Visc[iMarker] = 0.0;
      CoPx_Visc[iMarker] = 0.0;  CoPy_Visc[iMarker] = 0.0;       CoPz_Visc[iMarker] = 0.0;
      CT_Visc[iMarker] = 0.0;    CQ_Visc[iMarker] = 0.0;          CMerit_Visc[iMarker] = 0.0;
      CEff_Visc[iMarker] = 0.0;

      for (iDim = 0; iDim < nDim; iDim++) ForceViscous[iDim] = 0.0;
      MomentViscous[0] = 0.0; MomentViscous[1] = 0.0; MomentViscous[2] = 0.0;
      MomentX_Force[0] = 0.0; MomentX_Force[1] = 0.0; MomentX_Force[2] = 0.0;
      MomentY_Force[0] = 0.0; MomentY_Force[1] = 0.0; MomentY_Force[2] = 0.0;
      MomentZ_Force[0] = 0.0; MomentZ_Force[1] = 0.0; MomentZ_Force[2] = 0.0;

      /*--- Loop over the vertices to compute the forces ---*/

      for (iVertex = 0; iVertex < geometry->nVertex[iMarker]; iVertex++) {

        iPoint = geometry->vertex[iMarker][iVertex]->GetNode();
        iPointNormal = geometry->vertex[iMarker][iVertex]->GetNormal_Neighbor();

        Coord = geometry->nodes->GetCoord(iPoint);
        Coord_Normal = geometry->nodes->GetCoord(iPointNormal);

        geometry->vertex[iMarker][iVertex]->GetNormal(Normal);

        for (iDim = 0; iDim < nDim; iDim++) {
          for (jDim = 0 ; jDim < nDim; jDim++) {
            Grad_Vel[iDim][jDim] = nodes->GetGradient_Primitive(iPoint, iDim+1, jDim);
          }
        }

        Viscosity = nodes->GetLaminarViscosity(iPoint);
        Density = nodes->GetDensity(iPoint);

        Area = 0.0; for (iDim = 0; iDim < nDim; iDim++) Area += Normal[iDim]*Normal[iDim]; Area = sqrt(Area);
        for (iDim = 0; iDim < nDim; iDim++) {
          UnitNormal[iDim] = Normal[iDim]/Area;
        }

        /*--- Evaluate Tau ---*/

        div_vel = 0.0; for (iDim = 0; iDim < nDim; iDim++) div_vel += Grad_Vel[iDim][iDim];

        for (iDim = 0; iDim < nDim; iDim++) {
          for (jDim = 0 ; jDim < nDim; jDim++) {
            Tau[iDim][jDim] = Viscosity*(Grad_Vel[jDim][iDim] + Grad_Vel[iDim][jDim]) - TWO3*Viscosity*div_vel*delta[iDim][jDim];
          }
        }

        /*--- Project Tau in each surface element ---*/

        for (iDim = 0; iDim < nDim; iDim++) {
          TauElem[iDim] = 0.0;
          for (jDim = 0; jDim < nDim; jDim++) {
            TauElem[iDim] += Tau[iDim][jDim]*UnitNormal[jDim];
          }
        }

        /*--- Compute wall shear stress (using the stress tensor). Compute wall skin friction coefficient, and heat flux on the wall ---*/

        TauNormal = 0.0;
        for (iDim = 0; iDim < nDim; iDim++)
          TauNormal += TauElem[iDim] * UnitNormal[iDim];

        WallShearStress = 0.0;
        for (iDim = 0; iDim < nDim; iDim++) {
          TauTangent[iDim] = TauElem[iDim] - TauNormal * UnitNormal[iDim];
          CSkinFriction[iMarker][iDim][iVertex] = TauTangent[iDim] / (0.5*RefDensity*RefVel2);
          WallShearStress += TauTangent[iDim] * TauTangent[iDim];
        }
        WallShearStress = sqrt(WallShearStress);

        for (iDim = 0; iDim < nDim; iDim++) WallDist[iDim] = (Coord[iDim] - Coord_Normal[iDim]);
        WallDistMod = 0.0; for (iDim = 0; iDim < nDim; iDim++) WallDistMod += WallDist[iDim]*WallDist[iDim]; WallDistMod = sqrt(WallDistMod);

        /*--- Compute y+ and non-dimensional velocity ---*/

        FrictionVel = sqrt(fabs(WallShearStress)/Density);
        YPlus[iMarker][iVertex] = WallDistMod*FrictionVel/(Viscosity/Density);

        /*--- Note that y+, and heat are computed at the
         halo cells (for visualization purposes), but not the forces ---*/

        if ((geometry->nodes->GetDomain(iPoint)) && (Monitoring == YES)) {

          /*--- Axisymmetric simulations ---*/

          if (axisymmetric) AxiFactor = 2.0*PI_NUMBER*geometry->nodes->GetCoord(iPoint,1);
          else AxiFactor = 1.0;

          /*--- Force computation ---*/

          for (iDim = 0; iDim < nDim; iDim++) {
            Force[iDim] = TauElem[iDim] * Area * factor * AxiFactor;
            ForceViscous[iDim] += Force[iDim];
            MomentDist[iDim] = Coord[iDim] - Origin[iDim];
          }

          /*--- Moment with respect to the reference axis ---*/

          if (iDim == 3) {
            MomentViscous[0] += (Force[2]*MomentDist[1] - Force[1]*MomentDist[2])/RefLength;
            MomentX_Force[1] += (-Force[1]*Coord[2]);
            MomentX_Force[2] += (Force[2]*Coord[1]);

            MomentViscous[1] += (Force[0]*MomentDist[2] - Force[2]*MomentDist[0])/RefLength;
            MomentY_Force[2] += (-Force[2]*Coord[0]);
            MomentY_Force[0] += (Force[0]*Coord[2]);
          }
          MomentViscous[2] += (Force[1]*MomentDist[0] - Force[0]*MomentDist[1])/RefLength;
          MomentZ_Force[0] += (-Force[0]*Coord[1]);
          MomentZ_Force[1] += (Force[1]*Coord[0]);

        }

      }

      /*--- Project forces and store the non-dimensional coefficients ---*/

      if (Monitoring == YES) {
        if (nDim == 2) {
          CD_Visc[iMarker]       =  ForceViscous[0]*cos(Alpha) + ForceViscous[1]*sin(Alpha);
          CL_Visc[iMarker]       = -ForceViscous[0]*sin(Alpha) + ForceViscous[1]*cos(Alpha);
          CEff_Visc[iMarker]        = CL_Visc[iMarker] / (CD_Visc[iMarker]+EPS);
          CMz_Visc[iMarker]         = MomentViscous[2];
          CFx_Visc[iMarker]         = ForceViscous[0];
          CFy_Visc[iMarker]         = ForceViscous[1];
          CoPx_Visc[iMarker]        = MomentZ_Force[1];
          CoPy_Visc[iMarker]        = -MomentZ_Force[0];
          CT_Visc[iMarker]          = -CFx_Visc[iMarker];
          CQ_Visc[iMarker]          = -CMz_Visc[iMarker];
          CMerit_Visc[iMarker]      = CT_Visc[iMarker] / (CQ_Visc[iMarker]+EPS);
        }
        if (nDim == 3) {
          CD_Visc[iMarker]       =  ForceViscous[0]*cos(Alpha)*cos(Beta) + ForceViscous[1]*sin(Beta) + ForceViscous[2]*sin(Alpha)*cos(Beta);
          CL_Visc[iMarker]       = -ForceViscous[0]*sin(Alpha) + ForceViscous[2]*cos(Alpha);
          CSF_Visc[iMarker]  = -ForceViscous[0]*sin(Beta)*cos(Alpha) + ForceViscous[1]*cos(Beta) - ForceViscous[2]*sin(Beta)*sin(Alpha);
          CEff_Visc[iMarker]        = CL_Visc[iMarker]/(CD_Visc[iMarker] + EPS);
          CMx_Visc[iMarker]         = MomentViscous[0];
          CMy_Visc[iMarker]         = MomentViscous[1];
          CMz_Visc[iMarker]         = MomentViscous[2];
          CFx_Visc[iMarker]         = ForceViscous[0];
          CFy_Visc[iMarker]         = ForceViscous[1];
          CFz_Visc[iMarker]         = ForceViscous[2];
          CoPx_Visc[iMarker]        =  -MomentY_Force[0];
          CoPz_Visc[iMarker]        = MomentY_Force[2];
          CT_Visc[iMarker]          = -CFz_Visc[iMarker];
          CQ_Visc[iMarker]          = -CMz_Visc[iMarker];
          CMerit_Visc[iMarker]      = CT_Visc[iMarker] / (CQ_Visc[iMarker] + EPS);
        }

        AllBound_CD_Visc       += CD_Visc[iMarker];
        AllBound_CL_Visc       += CL_Visc[iMarker];
        AllBound_CSF_Visc  += CSF_Visc[iMarker];
        AllBound_CMx_Visc         += CMx_Visc[iMarker];
        AllBound_CMy_Visc         += CMy_Visc[iMarker];
        AllBound_CMz_Visc         += CMz_Visc[iMarker];
        AllBound_CFx_Visc         += CFx_Visc[iMarker];
        AllBound_CFy_Visc         += CFy_Visc[iMarker];
        AllBound_CFz_Visc         += CFz_Visc[iMarker];
        AllBound_CoPx_Visc        += CoPx_Visc[iMarker];
        AllBound_CoPy_Visc        += CoPy_Visc[iMarker];
        AllBound_CoPz_Visc        += CoPz_Visc[iMarker];
        AllBound_CT_Visc          += CT_Visc[iMarker];
        AllBound_CQ_Visc          += CQ_Visc[iMarker];

        /*--- Compute the coefficients per surface ---*/

        for (iMarker_Monitoring = 0; iMarker_Monitoring < config->GetnMarker_Monitoring(); iMarker_Monitoring++) {
          Monitoring_Tag = config->GetMarker_Monitoring_TagBound(iMarker_Monitoring);
          Marker_Tag = config->GetMarker_All_TagBound(iMarker);
          if (Marker_Tag == Monitoring_Tag) {
            Surface_CL_Visc[iMarker_Monitoring]      += CL_Visc[iMarker];
            Surface_CD_Visc[iMarker_Monitoring]      += CD_Visc[iMarker];
            Surface_CSF_Visc[iMarker_Monitoring] += CSF_Visc[iMarker];
            Surface_CEff_Visc[iMarker_Monitoring]       += CEff_Visc[iMarker];
            Surface_CFx_Visc[iMarker_Monitoring]        += CFx_Visc[iMarker];
            Surface_CFy_Visc[iMarker_Monitoring]        += CFy_Visc[iMarker];
            Surface_CFz_Visc[iMarker_Monitoring]        += CFz_Visc[iMarker];
            Surface_CMx_Visc[iMarker_Monitoring]        += CMx_Visc[iMarker];
            Surface_CMy_Visc[iMarker_Monitoring]        += CMy_Visc[iMarker];
            Surface_CMz_Visc[iMarker_Monitoring]        += CMz_Visc[iMarker];
          }
        }

      }

    }
  }

  /*--- Update some global coeffients ---*/

  AllBound_CEff_Visc = AllBound_CL_Visc / (AllBound_CD_Visc + EPS);
  AllBound_CMerit_Visc = AllBound_CT_Visc / (AllBound_CQ_Visc + EPS);


#ifdef HAVE_MPI

  /*--- Add AllBound information using all the nodes ---*/

  MyAllBound_CD_Visc        = AllBound_CD_Visc;                      AllBound_CD_Visc = 0.0;
  MyAllBound_CL_Visc        = AllBound_CL_Visc;                      AllBound_CL_Visc = 0.0;
  MyAllBound_CSF_Visc   = AllBound_CSF_Visc;                 AllBound_CSF_Visc = 0.0;
  AllBound_CEff_Visc = 0.0;
  MyAllBound_CMx_Visc          = AllBound_CMx_Visc;                        AllBound_CMx_Visc = 0.0;
  MyAllBound_CMy_Visc          = AllBound_CMy_Visc;                        AllBound_CMy_Visc = 0.0;
  MyAllBound_CMz_Visc          = AllBound_CMz_Visc;                        AllBound_CMz_Visc = 0.0;
  MyAllBound_CFx_Visc          = AllBound_CFx_Visc;                        AllBound_CFx_Visc = 0.0;
  MyAllBound_CFy_Visc          = AllBound_CFy_Visc;                        AllBound_CFy_Visc = 0.0;
  MyAllBound_CFz_Visc          = AllBound_CFz_Visc;                        AllBound_CFz_Visc = 0.0;
  MyAllBound_CoPx_Visc          = AllBound_CoPx_Visc;                        AllBound_CoPx_Visc = 0.0;
  MyAllBound_CoPy_Visc          = AllBound_CoPy_Visc;                        AllBound_CoPy_Visc = 0.0;
  MyAllBound_CoPz_Visc          = AllBound_CoPz_Visc;                        AllBound_CoPz_Visc = 0.0;
  MyAllBound_CT_Visc           = AllBound_CT_Visc;                         AllBound_CT_Visc = 0.0;
  MyAllBound_CQ_Visc           = AllBound_CQ_Visc;                         AllBound_CQ_Visc = 0.0;
  AllBound_CMerit_Visc = 0.0;

  SU2_MPI::Allreduce(&MyAllBound_CD_Visc, &AllBound_CD_Visc, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  SU2_MPI::Allreduce(&MyAllBound_CL_Visc, &AllBound_CL_Visc, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  SU2_MPI::Allreduce(&MyAllBound_CSF_Visc, &AllBound_CSF_Visc, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  AllBound_CEff_Visc = AllBound_CL_Visc / (AllBound_CD_Visc + EPS);
  SU2_MPI::Allreduce(&MyAllBound_CMx_Visc, &AllBound_CMx_Visc, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  SU2_MPI::Allreduce(&MyAllBound_CMy_Visc, &AllBound_CMy_Visc, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  SU2_MPI::Allreduce(&MyAllBound_CMz_Visc, &AllBound_CMz_Visc, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  SU2_MPI::Allreduce(&MyAllBound_CFx_Visc, &AllBound_CFx_Visc, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  SU2_MPI::Allreduce(&MyAllBound_CFy_Visc, &AllBound_CFy_Visc, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  SU2_MPI::Allreduce(&MyAllBound_CFz_Visc, &AllBound_CFz_Visc, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  SU2_MPI::Allreduce(&MyAllBound_CoPx_Visc, &AllBound_CoPx_Visc, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  SU2_MPI::Allreduce(&MyAllBound_CoPy_Visc, &AllBound_CoPy_Visc, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  SU2_MPI::Allreduce(&MyAllBound_CoPz_Visc, &AllBound_CoPz_Visc, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  SU2_MPI::Allreduce(&MyAllBound_CT_Visc, &AllBound_CT_Visc, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  SU2_MPI::Allreduce(&MyAllBound_CQ_Visc, &AllBound_CQ_Visc, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  AllBound_CMerit_Visc = AllBound_CT_Visc / (AllBound_CQ_Visc + EPS);


  /*--- Add the forces on the surfaces using all the nodes ---*/

  MySurface_CL_Visc      = new su2double[config->GetnMarker_Monitoring()];
  MySurface_CD_Visc      = new su2double[config->GetnMarker_Monitoring()];
  MySurface_CSF_Visc = new su2double[config->GetnMarker_Monitoring()];
  MySurface_CEff_Visc       = new su2double[config->GetnMarker_Monitoring()];
  MySurface_CFx_Visc        = new su2double[config->GetnMarker_Monitoring()];
  MySurface_CFy_Visc        = new su2double[config->GetnMarker_Monitoring()];
  MySurface_CFz_Visc        = new su2double[config->GetnMarker_Monitoring()];
  MySurface_CMx_Visc        = new su2double[config->GetnMarker_Monitoring()];
  MySurface_CMy_Visc        = new su2double[config->GetnMarker_Monitoring()];
  MySurface_CMz_Visc        = new su2double[config->GetnMarker_Monitoring()];

  for (iMarker_Monitoring = 0; iMarker_Monitoring < config->GetnMarker_Monitoring(); iMarker_Monitoring++) {

    MySurface_CL_Visc[iMarker_Monitoring]      = Surface_CL_Visc[iMarker_Monitoring];
    MySurface_CD_Visc[iMarker_Monitoring]      = Surface_CD_Visc[iMarker_Monitoring];
    MySurface_CSF_Visc[iMarker_Monitoring] = Surface_CSF_Visc[iMarker_Monitoring];
    MySurface_CEff_Visc[iMarker_Monitoring]       = Surface_CEff_Visc[iMarker_Monitoring];
    MySurface_CFx_Visc[iMarker_Monitoring]        = Surface_CFx_Visc[iMarker_Monitoring];
    MySurface_CFy_Visc[iMarker_Monitoring]        = Surface_CFy_Visc[iMarker_Monitoring];
    MySurface_CFz_Visc[iMarker_Monitoring]        = Surface_CFz_Visc[iMarker_Monitoring];
    MySurface_CMx_Visc[iMarker_Monitoring]        = Surface_CMx_Visc[iMarker_Monitoring];
    MySurface_CMy_Visc[iMarker_Monitoring]        = Surface_CMy_Visc[iMarker_Monitoring];
    MySurface_CMz_Visc[iMarker_Monitoring]        = Surface_CMz_Visc[iMarker_Monitoring];

    Surface_CL_Visc[iMarker_Monitoring]      = 0.0;
    Surface_CD_Visc[iMarker_Monitoring]      = 0.0;
    Surface_CSF_Visc[iMarker_Monitoring] = 0.0;
    Surface_CEff_Visc[iMarker_Monitoring]       = 0.0;
    Surface_CFx_Visc[iMarker_Monitoring]        = 0.0;
    Surface_CFy_Visc[iMarker_Monitoring]        = 0.0;
    Surface_CFz_Visc[iMarker_Monitoring]        = 0.0;
    Surface_CMx_Visc[iMarker_Monitoring]        = 0.0;
    Surface_CMy_Visc[iMarker_Monitoring]        = 0.0;
    Surface_CMz_Visc[iMarker_Monitoring]        = 0.0;
  }

  SU2_MPI::Allreduce(MySurface_CL_Visc, Surface_CL_Visc, config->GetnMarker_Monitoring(), MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  SU2_MPI::Allreduce(MySurface_CD_Visc, Surface_CD_Visc, config->GetnMarker_Monitoring(), MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  SU2_MPI::Allreduce(MySurface_CSF_Visc, Surface_CSF_Visc, config->GetnMarker_Monitoring(), MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  for (iMarker_Monitoring = 0; iMarker_Monitoring < config->GetnMarker_Monitoring(); iMarker_Monitoring++)
    Surface_CEff_Visc[iMarker_Monitoring] = Surface_CL_Visc[iMarker_Monitoring] / (Surface_CD_Visc[iMarker_Monitoring] + EPS);
  SU2_MPI::Allreduce(MySurface_CFx_Visc, Surface_CFx_Visc, config->GetnMarker_Monitoring(), MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  SU2_MPI::Allreduce(MySurface_CFy_Visc, Surface_CFy_Visc, config->GetnMarker_Monitoring(), MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  SU2_MPI::Allreduce(MySurface_CFz_Visc, Surface_CFz_Visc, config->GetnMarker_Monitoring(), MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  SU2_MPI::Allreduce(MySurface_CMx_Visc, Surface_CMx_Visc, config->GetnMarker_Monitoring(), MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  SU2_MPI::Allreduce(MySurface_CMy_Visc, Surface_CMy_Visc, config->GetnMarker_Monitoring(), MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  SU2_MPI::Allreduce(MySurface_CMz_Visc, Surface_CMz_Visc, config->GetnMarker_Monitoring(), MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  delete [] MySurface_CL_Visc; delete [] MySurface_CD_Visc; delete [] MySurface_CSF_Visc;
  delete [] MySurface_CEff_Visc;  delete [] MySurface_CFx_Visc;   delete [] MySurface_CFy_Visc;
  delete [] MySurface_CFz_Visc;   delete [] MySurface_CMx_Visc;   delete [] MySurface_CMy_Visc;
  delete [] MySurface_CMz_Visc;

#endif

  /*--- Update the total coefficients (note that all the nodes have the same value)---*/

  Total_CD          += AllBound_CD_Visc;
  Total_CL          += AllBound_CL_Visc;
  Total_CSF         += AllBound_CSF_Visc;
  Total_CEff        = Total_CL / (Total_CD + EPS);
  Total_CMx         += AllBound_CMx_Visc;
  Total_CMy         += AllBound_CMy_Visc;
  Total_CMz         += AllBound_CMz_Visc;
  Total_CFx         += AllBound_CFx_Visc;
  Total_CFy         += AllBound_CFy_Visc;
  Total_CFz         += AllBound_CFz_Visc;
  Total_CoPx        += AllBound_CoPx_Visc;
  Total_CoPy        += AllBound_CoPy_Visc;
  Total_CoPz        += AllBound_CoPz_Visc;
  Total_CT          += AllBound_CT_Visc;
  Total_CQ          += AllBound_CQ_Visc;
  Total_CMerit      = AllBound_CT_Visc / (AllBound_CQ_Visc + EPS);

  /*--- Update the total coefficients per surface (note that all the nodes have the same value)---*/

  for (iMarker_Monitoring = 0; iMarker_Monitoring < config->GetnMarker_Monitoring(); iMarker_Monitoring++) {
    Surface_CL[iMarker_Monitoring]      += Surface_CL_Visc[iMarker_Monitoring];
    Surface_CD[iMarker_Monitoring]      += Surface_CD_Visc[iMarker_Monitoring];
    Surface_CSF[iMarker_Monitoring] += Surface_CSF_Visc[iMarker_Monitoring];
    Surface_CEff[iMarker_Monitoring]       = Surface_CL[iMarker_Monitoring] / (Surface_CD[iMarker_Monitoring] + EPS);
    Surface_CFx[iMarker_Monitoring]        += Surface_CFx_Visc[iMarker_Monitoring];
    Surface_CFy[iMarker_Monitoring]        += Surface_CFy_Visc[iMarker_Monitoring];
    Surface_CFz[iMarker_Monitoring]        += Surface_CFz_Visc[iMarker_Monitoring];
    Surface_CMx[iMarker_Monitoring]        += Surface_CMx_Visc[iMarker_Monitoring];
    Surface_CMy[iMarker_Monitoring]        += Surface_CMy_Visc[iMarker_Monitoring];
    Surface_CMz[iMarker_Monitoring]        += Surface_CMz_Visc[iMarker_Monitoring];
  }

  delete [] Normal;
}