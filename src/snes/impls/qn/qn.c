#include <petsc/private/snesimpl.h> /*I "petscsnes.h" I*/
#include <petscdm.h>

#define H(i,j)  qn->dXdFmat[i*qn->m + j]

const char *const SNESQNScaleTypes[] =        {"DEFAULT","NONE","SCALAR","DIAGONAL","JACOBIAN","SNESQNScaleType","SNES_QN_SCALING_",NULL};
const char *const SNESQNRestartTypes[] =      {"DEFAULT","NONE","POWELL","PERIODIC","SNESQNRestartType","SNES_QN_RESTART_",NULL};
const char *const SNESQNTypes[] =             {"LBFGS","BROYDEN","BADBROYDEN","SNESQNType","SNES_QN_",NULL};

typedef struct {
  Mat               B;                    /* Quasi-Newton approximation Matrix (MATLMVM) */
  PetscInt          m;                    /* The number of kept previous steps */
  PetscReal         *lambda;              /* The line search history of the method */
  PetscBool         monflg;
  PetscViewer       monitor;
  PetscReal         powell_gamma;         /* Powell angle restart condition */
  PetscReal         scaling;              /* scaling of H0 */
  SNESQNType        type;                 /* the type of quasi-newton method used */
  SNESQNScaleType   scale_type;           /* the type of scaling used */
  SNESQNRestartType restart_type;         /* determine the frequency and type of restart conditions */
} SNES_QN;

static PetscErrorCode SNESSolve_QN(SNES snes)
{
  SNES_QN              *qn = (SNES_QN*) snes->data;
  Vec                  X,Xold;
  Vec                  F,W;
  Vec                  Y,D,Dold;
  PetscInt             i, i_r;
  PetscReal            fnorm,xnorm,ynorm,gnorm;
  SNESLineSearchReason lssucceed;
  PetscBool            badstep,powell,periodic;
  PetscScalar          DolddotD,DolddotDold;
  SNESConvergedReason  reason;

  /* basically just a regular newton's method except for the application of the Jacobian */

  PetscFunctionBegin;
  PetscCheck(!snes->xl && !snes->xu && !snes->ops->computevariablebounds,PetscObjectComm((PetscObject)snes),PETSC_ERR_ARG_WRONGSTATE, "SNES solver %s does not support bounds", ((PetscObject)snes)->type_name);

  PetscCall(PetscCitationsRegister(SNESCitation,&SNEScite));
  F    = snes->vec_func;                /* residual vector */
  Y    = snes->vec_sol_update;          /* search direction generated by J^-1D*/
  W    = snes->work[3];
  X    = snes->vec_sol;                 /* solution vector */
  Xold = snes->work[0];

  /* directions generated by the preconditioned problem with F_pre = F or x - M(x, b) */
  D    = snes->work[1];
  Dold = snes->work[2];

  snes->reason = SNES_CONVERGED_ITERATING;

  PetscCall(PetscObjectSAWsTakeAccess((PetscObject)snes));
  snes->iter = 0;
  snes->norm = 0.;
  PetscCall(PetscObjectSAWsGrantAccess((PetscObject)snes));

  if (snes->npc && snes->npcside== PC_LEFT && snes->functype == SNES_FUNCTION_PRECONDITIONED) {
    PetscCall(SNESApplyNPC(snes,X,NULL,F));
    PetscCall(SNESGetConvergedReason(snes->npc,&reason));
    if (reason < 0  && reason != SNES_DIVERGED_MAX_IT) {
      snes->reason = SNES_DIVERGED_INNER;
      PetscFunctionReturn(0);
    }
    PetscCall(VecNorm(F,NORM_2,&fnorm));
  } else {
    if (!snes->vec_func_init_set) {
      PetscCall(SNESComputeFunction(snes,X,F));
    } else snes->vec_func_init_set = PETSC_FALSE;

    PetscCall(VecNorm(F,NORM_2,&fnorm));
    SNESCheckFunctionNorm(snes,fnorm);
  }
  if (snes->npc && snes->npcside== PC_LEFT && snes->functype == SNES_FUNCTION_UNPRECONDITIONED) {
    PetscCall(SNESApplyNPC(snes,X,F,D));
    PetscCall(SNESGetConvergedReason(snes->npc,&reason));
    if (reason < 0  && reason != SNES_DIVERGED_MAX_IT) {
      snes->reason = SNES_DIVERGED_INNER;
      PetscFunctionReturn(0);
    }
  } else {
    PetscCall(VecCopy(F,D));
  }

  PetscCall(PetscObjectSAWsTakeAccess((PetscObject)snes));
  snes->norm = fnorm;
  PetscCall(PetscObjectSAWsGrantAccess((PetscObject)snes));
  PetscCall(SNESLogConvergenceHistory(snes,fnorm,0));
  PetscCall(SNESMonitor(snes,0,fnorm));

  /* test convergence */
  PetscCall((*snes->ops->converged)(snes,0,0.0,0.0,fnorm,&snes->reason,snes->cnvP));
  if (snes->reason) PetscFunctionReturn(0);

  if (snes->npc && snes->npcside== PC_RIGHT) {
    PetscCall(PetscLogEventBegin(SNES_NPCSolve,snes->npc,X,0,0));
    PetscCall(SNESSolve(snes->npc,snes->vec_rhs,X));
    PetscCall(PetscLogEventEnd(SNES_NPCSolve,snes->npc,X,0,0));
    PetscCall(SNESGetConvergedReason(snes->npc,&reason));
    if (reason < 0 && reason != SNES_DIVERGED_MAX_IT) {
      snes->reason = SNES_DIVERGED_INNER;
      PetscFunctionReturn(0);
    }
    PetscCall(SNESGetNPCFunction(snes,F,&fnorm));
    PetscCall(VecCopy(F,D));
  }

  /* general purpose update */
  if (snes->ops->update) {
    PetscCall((*snes->ops->update)(snes, snes->iter));
  }

  /* scale the initial update */
  if (qn->scale_type == SNES_QN_SCALE_JACOBIAN) {
    PetscCall(SNESComputeJacobian(snes,X,snes->jacobian,snes->jacobian_pre));
    SNESCheckJacobianDomainerror(snes);
    PetscCall(KSPSetOperators(snes->ksp,snes->jacobian,snes->jacobian_pre));
    PetscCall(MatLMVMSetJ0KSP(qn->B, snes->ksp));
  }

  for (i = 0, i_r = 0; i < snes->max_its; i++, i_r++) {
    /* update QN approx and calculate step */
    PetscCall(MatLMVMUpdate(qn->B, X, D));
    PetscCall(MatSolve(qn->B, D, Y));

    /* line search for lambda */
    ynorm = 1; gnorm = fnorm;
    PetscCall(VecCopy(D, Dold));
    PetscCall(VecCopy(X, Xold));
    PetscCall(SNESLineSearchApply(snes->linesearch, X, F, &fnorm, Y));
    if (snes->reason == SNES_DIVERGED_FUNCTION_COUNT) break;
    PetscCall(SNESLineSearchGetReason(snes->linesearch, &lssucceed));
    PetscCall(SNESLineSearchGetNorms(snes->linesearch, &xnorm, &fnorm, &ynorm));
    badstep = PETSC_FALSE;
    if (lssucceed) {
      if (++snes->numFailures >= snes->maxFailures) {
        snes->reason = SNES_DIVERGED_LINE_SEARCH;
        break;
      }
      badstep = PETSC_TRUE;
    }

    /* convergence monitoring */
    PetscCall(PetscInfo(snes,"fnorm=%18.16e, gnorm=%18.16e, ynorm=%18.16e, lssucceed=%d\n",(double)fnorm,(double)gnorm,(double)ynorm,(int)lssucceed));

    if (snes->npc && snes->npcside== PC_RIGHT) {
      PetscCall(PetscLogEventBegin(SNES_NPCSolve,snes->npc,X,0,0));
      PetscCall(SNESSolve(snes->npc,snes->vec_rhs,X));
      PetscCall(PetscLogEventEnd(SNES_NPCSolve,snes->npc,X,0,0));
      PetscCall(SNESGetConvergedReason(snes->npc,&reason));
      if (reason < 0 && reason != SNES_DIVERGED_MAX_IT) {
        snes->reason = SNES_DIVERGED_INNER;
        PetscFunctionReturn(0);
      }
      PetscCall(SNESGetNPCFunction(snes,F,&fnorm));
    }

    PetscCall(SNESSetIterationNumber(snes, i+1));
    snes->norm = fnorm;
    snes->xnorm = xnorm;
    snes->ynorm = ynorm;

    PetscCall(SNESLogConvergenceHistory(snes,snes->norm,snes->iter));
    PetscCall(SNESMonitor(snes,snes->iter,snes->norm));

    /* set parameter for default relative tolerance convergence test */
    PetscCall((*snes->ops->converged)(snes,snes->iter,xnorm,ynorm,fnorm,&snes->reason,snes->cnvP));
    if (snes->reason) PetscFunctionReturn(0);
    if (snes->npc && snes->npcside== PC_LEFT && snes->functype == SNES_FUNCTION_UNPRECONDITIONED) {
      PetscCall(SNESApplyNPC(snes,X,F,D));
      PetscCall(SNESGetConvergedReason(snes->npc,&reason));
      if (reason < 0  && reason != SNES_DIVERGED_MAX_IT) {
        snes->reason = SNES_DIVERGED_INNER;
        PetscFunctionReturn(0);
      }
    } else {
      PetscCall(VecCopy(F, D));
    }

    /* general purpose update */
    if (snes->ops->update) {
      PetscCall((*snes->ops->update)(snes, snes->iter));
    }

    /* restart conditions */
    powell = PETSC_FALSE;
    if (qn->restart_type == SNES_QN_RESTART_POWELL && i_r > 1) {
      /* check restart by Powell's Criterion: |F^T H_0 Fold| > powell_gamma * |Fold^T H_0 Fold| */
      if (qn->scale_type == SNES_QN_SCALE_JACOBIAN) {
        PetscCall(MatMult(snes->jacobian_pre,Dold,W));
      } else {
        PetscCall(VecCopy(Dold,W));
      }
      PetscCall(VecDotBegin(W, Dold, &DolddotDold));
      PetscCall(VecDotBegin(W, D, &DolddotD));
      PetscCall(VecDotEnd(W, Dold, &DolddotDold));
      PetscCall(VecDotEnd(W, D, &DolddotD));
      if (PetscAbs(PetscRealPart(DolddotD)) > qn->powell_gamma*PetscAbs(PetscRealPart(DolddotDold))) powell = PETSC_TRUE;
    }
    periodic = PETSC_FALSE;
    if (qn->restart_type == SNES_QN_RESTART_PERIODIC) {
      if (i_r>qn->m-1) periodic = PETSC_TRUE;
    }
    /* restart if either powell or periodic restart is satisfied. */
    if (badstep || powell || periodic) {
      if (qn->monflg) {
        PetscCall(PetscViewerASCIIAddTab(qn->monitor,((PetscObject)snes)->tablevel+2));
        if (powell) {
          PetscCall(PetscViewerASCIIPrintf(qn->monitor, "Powell restart! |%14.12e| > %6.4f*|%14.12e| i_r = %" PetscInt_FMT "\n", (double)PetscRealPart(DolddotD), (double)qn->powell_gamma, (double)PetscRealPart(DolddotDold),i_r));
        } else {
          PetscCall(PetscViewerASCIIPrintf(qn->monitor, "Periodic restart! i_r = %" PetscInt_FMT "\n", i_r));
        }
        PetscCall(PetscViewerASCIISubtractTab(qn->monitor,((PetscObject)snes)->tablevel+2));
      }
      i_r = -1;
      if (qn->scale_type == SNES_QN_SCALE_JACOBIAN) {
        PetscCall(SNESComputeJacobian(snes,X,snes->jacobian,snes->jacobian_pre));
        SNESCheckJacobianDomainerror(snes);
      }
      PetscCall(MatLMVMReset(qn->B, PETSC_FALSE));
    }
  }
  if (i == snes->max_its) {
    PetscCall(PetscInfo(snes, "Maximum number of iterations has been reached: %" PetscInt_FMT "\n", snes->max_its));
    if (!snes->reason) snes->reason = SNES_DIVERGED_MAX_IT;
  }
  PetscFunctionReturn(0);
}

static PetscErrorCode SNESSetUp_QN(SNES snes)
{
  SNES_QN        *qn = (SNES_QN*)snes->data;
  DM             dm;
  PetscInt       n, N;

  PetscFunctionBegin;

  if (!snes->vec_sol) {
    PetscCall(SNESGetDM(snes,&dm));
    PetscCall(DMCreateGlobalVector(dm,&snes->vec_sol));
  }
  PetscCall(SNESSetWorkVecs(snes,4));

  if (qn->scale_type == SNES_QN_SCALE_JACOBIAN) {
    PetscCall(SNESSetUpMatrices(snes));
  }
  if (snes->npcside== PC_LEFT && snes->functype == SNES_FUNCTION_DEFAULT) {snes->functype = SNES_FUNCTION_UNPRECONDITIONED;}

  /* set method defaults */
  if (qn->scale_type == SNES_QN_SCALE_DEFAULT) {
    if (qn->type == SNES_QN_BADBROYDEN) {
      qn->scale_type = SNES_QN_SCALE_NONE;
    } else {
      qn->scale_type = SNES_QN_SCALE_SCALAR;
    }
  }
  if (qn->restart_type == SNES_QN_RESTART_DEFAULT) {
    if (qn->type == SNES_QN_LBFGS) {
      qn->restart_type = SNES_QN_RESTART_POWELL;
    } else {
      qn->restart_type = SNES_QN_RESTART_PERIODIC;
    }
  }

  /* Set up the LMVM matrix */
  switch (qn->type) {
    case SNES_QN_BROYDEN:
      PetscCall(MatSetType(qn->B, MATLMVMBROYDEN));
      qn->scale_type = SNES_QN_SCALE_NONE;
      break;
    case SNES_QN_BADBROYDEN:
      PetscCall(MatSetType(qn->B, MATLMVMBADBROYDEN));
      qn->scale_type = SNES_QN_SCALE_NONE;
      break;
    default:
      PetscCall(MatSetType(qn->B, MATLMVMBFGS));
      switch (qn->scale_type) {
        case SNES_QN_SCALE_NONE:
          PetscCall(MatLMVMSymBroydenSetScaleType(qn->B, MAT_LMVM_SYMBROYDEN_SCALE_NONE));
          break;
        case SNES_QN_SCALE_SCALAR:
          PetscCall(MatLMVMSymBroydenSetScaleType(qn->B, MAT_LMVM_SYMBROYDEN_SCALE_SCALAR));
          break;
        case SNES_QN_SCALE_JACOBIAN:
          PetscCall(MatLMVMSymBroydenSetScaleType(qn->B, MAT_LMVM_SYMBROYDEN_SCALE_USER));
          break;
        case SNES_QN_SCALE_DIAGONAL:
        case SNES_QN_SCALE_DEFAULT:
        default:
          break;
      }
      break;
  }
  PetscCall(VecGetLocalSize(snes->vec_sol, &n));
  PetscCall(VecGetSize(snes->vec_sol, &N));
  PetscCall(MatSetSizes(qn->B, n, n, N, N));
  PetscCall(MatSetUp(qn->B));
  PetscCall(MatLMVMReset(qn->B, PETSC_TRUE));
  PetscCall(MatLMVMSetHistorySize(qn->B, qn->m));
  PetscCall(MatLMVMAllocate(qn->B, snes->vec_sol, snes->vec_func));
  PetscFunctionReturn(0);
}

static PetscErrorCode SNESReset_QN(SNES snes)
{
  SNES_QN        *qn;

  PetscFunctionBegin;
  if (snes->data) {
    qn = (SNES_QN*)snes->data;
    PetscCall(MatDestroy(&qn->B));
  }
  PetscFunctionReturn(0);
}

static PetscErrorCode SNESDestroy_QN(SNES snes)
{
  PetscFunctionBegin;
  PetscCall(SNESReset_QN(snes));
  PetscCall(PetscFree(snes->data));
  PetscCall(PetscObjectComposeFunction((PetscObject)snes,"",NULL));
  PetscFunctionReturn(0);
}

static PetscErrorCode SNESSetFromOptions_QN(PetscOptionItems *PetscOptionsObject,SNES snes)
{

  SNES_QN           *qn    = (SNES_QN*)snes->data;
  PetscBool         flg;
  SNESLineSearch    linesearch;
  SNESQNRestartType rtype = qn->restart_type;
  SNESQNScaleType   stype = qn->scale_type;
  SNESQNType        qtype = qn->type;

  PetscFunctionBegin;
  PetscOptionsHeadBegin(PetscOptionsObject,"SNES QN options");
  PetscCall(PetscOptionsInt("-snes_qn_m","Number of past states saved for L-BFGS methods","SNESQN",qn->m,&qn->m,NULL));
  PetscCall(PetscOptionsReal("-snes_qn_powell_gamma","Powell angle tolerance",          "SNESQN", qn->powell_gamma, &qn->powell_gamma, NULL));
  PetscCall(PetscOptionsBool("-snes_qn_monitor",         "Monitor for the QN methods",      "SNESQN", qn->monflg, &qn->monflg, NULL));
  PetscCall(PetscOptionsEnum("-snes_qn_scale_type","Scaling type","SNESQNSetScaleType",SNESQNScaleTypes,(PetscEnum)stype,(PetscEnum*)&stype,&flg));
  if (flg) PetscCall(SNESQNSetScaleType(snes,stype));

  PetscCall(PetscOptionsEnum("-snes_qn_restart_type","Restart type","SNESQNSetRestartType",SNESQNRestartTypes,(PetscEnum)rtype,(PetscEnum*)&rtype,&flg));
  if (flg) PetscCall(SNESQNSetRestartType(snes,rtype));

  PetscCall(PetscOptionsEnum("-snes_qn_type","Quasi-Newton update type","",SNESQNTypes,(PetscEnum)qtype,(PetscEnum*)&qtype,&flg));
  if (flg) PetscCall(SNESQNSetType(snes,qtype));
  PetscCall(MatSetFromOptions(qn->B));
  PetscOptionsHeadEnd();
  if (!snes->linesearch) {
    PetscCall(SNESGetLineSearch(snes, &linesearch));
    if (!((PetscObject)linesearch)->type_name) {
      if (qn->type == SNES_QN_LBFGS) {
        PetscCall(SNESLineSearchSetType(linesearch, SNESLINESEARCHCP));
      } else if (qn->type == SNES_QN_BROYDEN) {
        PetscCall(SNESLineSearchSetType(linesearch, SNESLINESEARCHBASIC));
      } else {
        PetscCall(SNESLineSearchSetType(linesearch, SNESLINESEARCHL2));
      }
    }
  }
  if (qn->monflg) {
    PetscCall(PetscViewerASCIIGetStdout(PetscObjectComm((PetscObject)snes), &qn->monitor));
  }
  PetscFunctionReturn(0);
}

static PetscErrorCode SNESView_QN(SNES snes, PetscViewer viewer)
{
  SNES_QN        *qn    = (SNES_QN*)snes->data;
  PetscBool      iascii;

  PetscFunctionBegin;
  PetscCall(PetscObjectTypeCompare((PetscObject) viewer, PETSCVIEWERASCII, &iascii));
  if (iascii) {
    PetscCall(PetscViewerASCIIPrintf(viewer,"  type is %s, restart type is %s, scale type is %s\n",SNESQNTypes[qn->type],SNESQNRestartTypes[qn->restart_type],SNESQNScaleTypes[qn->scale_type]));
    PetscCall(PetscViewerASCIIPrintf(viewer,"  Stored subspace size: %" PetscInt_FMT "\n", qn->m));
  }
  PetscFunctionReturn(0);
}

/*@
    SNESQNSetRestartType - Sets the restart type for SNESQN.

    Logically Collective on SNES

    Input Parameters:
+   snes - the iterative context
-   rtype - restart type

    Options Database:
+   -snes_qn_restart_type <powell,periodic,none> - set the restart type
-   -snes_qn_m <m> - sets the number of stored updates and the restart period for periodic

    Level: intermediate

    SNESQNRestartTypes:
+   SNES_QN_RESTART_NONE - never restart
.   SNES_QN_RESTART_POWELL - restart based upon descent criteria
-   SNES_QN_RESTART_PERIODIC - restart after a fixed number of iterations

@*/
PetscErrorCode SNESQNSetRestartType(SNES snes, SNESQNRestartType rtype)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_CLASSID,1);
  PetscTryMethod(snes,"SNESQNSetRestartType_C",(SNES,SNESQNRestartType),(snes,rtype));
  PetscFunctionReturn(0);
}

/*@
    SNESQNSetScaleType - Sets the scaling type for the inner inverse Jacobian in SNESQN.

    Logically Collective on SNES

    Input Parameters:
+   snes - the iterative context
-   stype - scale type

    Options Database:
.   -snes_qn_scale_type <diagonal,none,scalar,jacobian> - Scaling type

    Level: intermediate

    SNESQNScaleTypes:
+   SNES_QN_SCALE_NONE - don't scale the problem
.   SNES_QN_SCALE_SCALAR - use Shanno scaling
.   SNES_QN_SCALE_DIAGONAL - scale with a diagonalized BFGS formula (see Gilbert and Lemarechal 1989), available
-   SNES_QN_SCALE_JACOBIAN - scale by solving a linear system coming from the Jacobian you provided with SNESSetJacobian() computed at the first iteration
                             of QN and at ever restart.

.seealso: SNES, SNESQN, SNESLineSearch, SNESQNScaleType, SNESSetJacobian()
@*/

PetscErrorCode SNESQNSetScaleType(SNES snes, SNESQNScaleType stype)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_CLASSID,1);
  PetscTryMethod(snes,"SNESQNSetScaleType_C",(SNES,SNESQNScaleType),(snes,stype));
  PetscFunctionReturn(0);
}

PetscErrorCode SNESQNSetScaleType_QN(SNES snes, SNESQNScaleType stype)
{
  SNES_QN *qn = (SNES_QN*)snes->data;

  PetscFunctionBegin;
  qn->scale_type = stype;
  if (stype == SNES_QN_SCALE_JACOBIAN) snes->usesksp = PETSC_TRUE;
  PetscFunctionReturn(0);
}

PetscErrorCode SNESQNSetRestartType_QN(SNES snes, SNESQNRestartType rtype)
{
  SNES_QN *qn = (SNES_QN*)snes->data;

  PetscFunctionBegin;
  qn->restart_type = rtype;
  PetscFunctionReturn(0);
}

/*@
    SNESQNSetType - Sets the quasi-Newton variant to be used in SNESQN.

    Logically Collective on SNES

    Input Parameters:
+   snes - the iterative context
-   qtype - variant type

    Options Database:
.   -snes_qn_type <lbfgs,broyden,badbroyden> - quasi-Newton type

    Level: beginner

    SNESQNTypes:
+   SNES_QN_LBFGS - LBFGS variant
.   SNES_QN_BROYDEN - Broyden variant
-   SNES_QN_BADBROYDEN - Bad Broyden variant

@*/

PetscErrorCode SNESQNSetType(SNES snes, SNESQNType qtype)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_CLASSID,1);
  PetscTryMethod(snes,"SNESQNSetType_C",(SNES,SNESQNType),(snes,qtype));
  PetscFunctionReturn(0);
}

PetscErrorCode SNESQNSetType_QN(SNES snes, SNESQNType qtype)
{
  SNES_QN *qn = (SNES_QN*)snes->data;

  PetscFunctionBegin;
  qn->type = qtype;
  PetscFunctionReturn(0);
}

/* -------------------------------------------------------------------------- */
/*MC
      SNESQN - Limited-Memory Quasi-Newton methods for the solution of nonlinear systems.

      Options Database:

+     -snes_qn_m <m> - Number of past states saved for the L-Broyden methods.
+     -snes_qn_restart_type <powell,periodic,none> - set the restart type
.     -snes_qn_powell_gamma - Angle condition for restart.
.     -snes_qn_powell_descent - Descent condition for restart.
.     -snes_qn_type <lbfgs,broyden,badbroyden> - QN type
.     -snes_qn_scale_type <diagonal,none,scalar,jacobian> - scaling performed on inner Jacobian
.     -snes_linesearch_type <cp, l2, basic> - Type of line search.
-     -snes_qn_monitor - Monitors the quasi-newton Jacobian.

      Notes:
    This implements the L-BFGS, Broyden, and "Bad" Broyden algorithms for the solution of F(x) = b using
      previous change in F(x) and x to form the approximate inverse Jacobian using a series of multiplicative rank-one
      updates.

      When using a nonlinear preconditioner, one has two options as to how the preconditioner is applied.  The first of
      these options, sequential, uses the preconditioner to generate a new solution and function and uses those at this
      iteration as the current iteration's values when constructing the approximate Jacobian.  The second, composed,
      perturbs the problem the Jacobian represents to be P(x, b) - x = 0, where P(x, b) is the preconditioner.

      Uses left nonlinear preconditioning by default.

      References:
+   * -   Kelley, C.T., Iterative Methods for Linear and Nonlinear Equations, Chapter 8, SIAM, 1995.
.   * -   R. Byrd, J. Nocedal, R. Schnabel, Representations of Quasi Newton Matrices and their use in Limited Memory Methods,
      Technical Report, Northwestern University, June 1992.
.   * -   Peter N. Brown, Alan C. Hindmarsh, Homer F. Walker, Experiments with Quasi-Newton Methods in Solving Stiff ODE
      Systems, SIAM J. Sci. Stat. Comput. Vol 6(2), April 1985.
.   * -   Peter R. Brune, Matthew G. Knepley, Barry F. Smith, and Xuemin Tu, "Composing Scalable Nonlinear Algebraic Solvers",
       SIAM Review, 57(4), 2015
.   * -   Griewank, Andreas. "Broyden updating, the good and the bad!." Doc. Math (2012): 301-315.
.   * -   Gilbert, Jean Charles, and Claude Lemar{\'e}chal. "Some numerical experiments with variable-storage quasi-Newton algorithms."
      Mathematical programming 45.1-3 (1989): 407-435.
-   * -   Dener A., Munson T. "Accelerating Limited-Memory Quasi-Newton Convergence for Large-Scale Optimization"
      Computational Science - ICCS 2019. ICCS 2019. Lecture Notes in Computer Science, vol 11538. Springer, Cham

      Level: beginner

.seealso:  SNESCreate(), SNES, SNESSetType(), SNESNEWTONLS, SNESNEWTONTR

M*/
PETSC_EXTERN PetscErrorCode SNESCreate_QN(SNES snes)
{
  SNES_QN        *qn;
  const char     *optionsprefix;

  PetscFunctionBegin;
  snes->ops->setup          = SNESSetUp_QN;
  snes->ops->solve          = SNESSolve_QN;
  snes->ops->destroy        = SNESDestroy_QN;
  snes->ops->setfromoptions = SNESSetFromOptions_QN;
  snes->ops->view           = SNESView_QN;
  snes->ops->reset          = SNESReset_QN;

  snes->npcside= PC_LEFT;

  snes->usesnpc = PETSC_TRUE;
  snes->usesksp = PETSC_FALSE;

  snes->alwayscomputesfinalresidual = PETSC_TRUE;

  if (!snes->tolerancesset) {
    snes->max_funcs = 30000;
    snes->max_its   = 10000;
  }

  PetscCall(PetscNewLog(snes,&qn));
  snes->data          = (void*) qn;
  qn->m               = 10;
  qn->scaling         = 1.0;
  qn->monitor         = NULL;
  qn->monflg          = PETSC_FALSE;
  qn->powell_gamma    = 0.9999;
  qn->scale_type      = SNES_QN_SCALE_DEFAULT;
  qn->restart_type    = SNES_QN_RESTART_DEFAULT;
  qn->type            = SNES_QN_LBFGS;

  PetscCall(MatCreate(PetscObjectComm((PetscObject)snes), &qn->B));
  PetscCall(SNESGetOptionsPrefix(snes, &optionsprefix));
  PetscCall(MatSetOptionsPrefix(qn->B, optionsprefix));

  PetscCall(PetscObjectComposeFunction((PetscObject)snes,"SNESQNSetScaleType_C",SNESQNSetScaleType_QN));
  PetscCall(PetscObjectComposeFunction((PetscObject)snes,"SNESQNSetRestartType_C",SNESQNSetRestartType_QN));
  PetscCall(PetscObjectComposeFunction((PetscObject)snes,"SNESQNSetType_C",SNESQNSetType_QN));
  PetscFunctionReturn(0);
}
