//****************************************************************
//* This file is part of the AsFem framework
//* A Simple Finite Element Method program (AsFem)
//* All rights reserved, Yang Bai @ CopyRight 2020
//* https://github.com/yangbai90/AsFem.git
//* Licensed under GNU GPLv3, please see LICENSE for details
//* https://www.gnu.org/licenses/gpl-3.0.en.html
//****************************************************************

#include "NonlinearSolver/NonlinearSolver.h"

PetscErrorCode Monitor(SNES snes,PetscInt iters,PetscReal rnorm,void* ctx){
    MonitorCtx *user=(MonitorCtx*)ctx;
    user->iters=iters;
    user->rnorm=rnorm;
    SNESGetSolutionNorm(snes,&user->dunorm);
    user->enorm=rnorm*user->dunorm;
    if(iters==0){
        user->rnorm0=rnorm;
        user->dunorm0=user->dunorm;
        user->enorm0=user->enorm;
    }
    if(user->IsDepDebug){
        PetscPrintf(PETSC_COMM_WORLD,"***    SNES solver: iters=%3d , |R|=%14.6e                    ***\n",iters,rnorm);
    }
    // PetscPrintf(PETSC_COMM_WORLD,"***    SNES solver: iters=%3d ,|R|=%14.6e,|E|=%14.6e,|dU|=%14.6e***\n",iters,rnorm,user->enorm,user->dunorm);
    return 0;
}

PetscErrorCode FormResidual(SNES snes,Vec U,Vec RHS,void *ctx){
    AppCtx *user=(AppCtx*)ctx;
    int i;

    user->_bcSystem.ApplyInitialBC(user->_mesh,user->_dofHandler,user->_fectrlinfo.t,U);

    // calculate the current velocity
    VecWAXPY(user->_solution._V,-1.0,user->_solution._Uold,U);//V=-Uold+Unew
    VecScale(user->_solution._V,user->_fectrlinfo.ctan[1]);//V=V*1.0/dt

    user->_feSystem.FormFE(3,user->_fectrlinfo.t,user->_fectrlinfo.dt,user->_fectrlinfo.ctan,
                           user->_mesh,user->_dofHandler,user->_fe,user->_elmtSystem,
                           user->_mateSystem,
                           U,user->_solution._V,
                           user->_solution._Hist,user->_solution._HistOld,user->_solution._Proj,
                           user->_equationSystem._AMATRIX,RHS);
    
    user->_bcSystem.SetBCPenaltyFactor(user->_feSystem.GetMaxAMatrixValue()*1.0e5);

    user->_bcSystem.ApplyBC(user->_mesh,user->_dofHandler,user->_fe,
                        user->_fectrlinfo.t,user->_fectrlinfo.ctan,
                        user->_equationSystem._AMATRIX,RHS,U);

    SNESGetMaxNonlinearStepFailures(snes,&i);

    return 0;
}

PetscErrorCode FormJacobian(SNES snes,Vec U,Mat A,Mat B,void *ctx){
    AppCtx *user=(AppCtx*)ctx;
    int i;

    user->_feSystem.ResetMaxAMatrixValue();

    user->_bcSystem.ApplyInitialBC(user->_mesh,user->_dofHandler,user->_fectrlinfo.t,U);

    //*** calculate the current velocity
    VecWAXPY(user->_solution._V,-1.0,user->_solution._Uold,U);//V=-Uold+Unew
    VecScale(user->_solution._V,user->_fectrlinfo.ctan[1]);//V=V*1.0/dt

    user->_feSystem.FormFE(6,user->_fectrlinfo.t,user->_fectrlinfo.dt,user->_fectrlinfo.ctan,
                           user->_mesh,user->_dofHandler,user->_fe,user->_elmtSystem,
                           user->_mateSystem,
                           U,user->_solution._V,
                           user->_solution._Hist,user->_solution._HistOld,user->_solution._Proj,
                           A,user->_equationSystem._RHS);
    
    user->_bcSystem.SetBCPenaltyFactor(user->_feSystem.GetMaxAMatrixValue()*1.0e5);

    user->_bcSystem.ApplyBC(user->_mesh,user->_dofHandler,user->_fe,
                        user->_fectrlinfo.t,user->_fectrlinfo.ctan,
                        A,user->_equationSystem._RHS,U);

    MatScale(A,-1.0);
    MatGetSize(B,&i,&i);
    SNESGetMaxNonlinearStepFailures(snes,&i);

    return 0;
}


bool NonlinearSolver::SSolve(Mesh &mesh,DofHandler &dofHandler,
               ElmtSystem &elmtSystem,MateSystem &mateSystem,
               BCSystem &bcSystem,ICSystem &icSystem,
               Solution &solution,EquationSystem &equationSystem,
               FE &fe,FESystem &feSystem,
               FeCtrlInfo &fectrlinfo){

    _appctx=AppCtx{mesh,dofHandler,
                   bcSystem,icSystem,
                   elmtSystem,mateSystem,
                   solution,equationSystem,
                   fe,feSystem,
                   fectrlinfo
                   };

    _monctx=MonitorCtx{0.0,1.0,
            0.0,1.0,
            0.0,1.0,
            0,
            fectrlinfo.IsDepDebug};


    _appctx._bcSystem.ApplyInitialBC(_appctx._mesh,_appctx._dofHandler,1.0,_appctx._solution._Unew);
    
    SNESSetFunction(_snes,_appctx._equationSystem._RHS,FormResidual,&_appctx);

    SNESSetJacobian(_snes,_appctx._equationSystem._AMATRIX,_appctx._equationSystem._AMATRIX,FormJacobian,&_appctx);

    SNESMonitorSet(_snes,Monitor,&_monctx,0);

    SNESSetForceIteration(_snes,PETSC_TRUE);

    SNESSetFromOptions(_snes);

    SNESSolve(_snes,NULL,_appctx._solution._Unew);

    SNESGetConvergedReason(_snes,&_snesreason);
    
    _Iters=_monctx.iters;

    if(_snesreason==SNES_CONVERGED_FNORM_ABS){
        if(fectrlinfo.IsDepDebug){
            PetscPrintf(PETSC_COMM_WORLD,"*** Convergent for |R|<atol, final iters=%3d                    !!!   ***\n",_monctx.iters+1);
        }
        return true;
    }
    else if(_snesreason==SNES_CONVERGED_FNORM_RELATIVE){
        if(fectrlinfo.IsDepDebug){
            PetscPrintf(PETSC_COMM_WORLD,"*** Convergent for |R|<rtol*|R0|, final iters=%3d               !!!   ***\n",_monctx.iters+1);
        }
        return true;
    }
    else if(_snesreason==SNES_CONVERGED_SNORM_RELATIVE){
        if(fectrlinfo.IsDepDebug){
            PetscPrintf(PETSC_COMM_WORLD,"*** Convergent for |delta x|<stol|x|, final iters=%3d           !!!   ***\n",_monctx.iters+1);
        }
        return true;
    }
    else{
        PetscPrintf(PETSC_COMM_WORLD,"*** Divergent, SNES nonlinear solver failed, iters=%3d          !!!   ***\n",_monctx.iters+1);
        // PetscPrintf(PETSC_COMM_WORLD,"*************************************************************************\n");
        return false;
    }

}

