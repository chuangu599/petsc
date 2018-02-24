
#include <petsc/private/kspimpl.h>
#include <petsc/private/vecimpl.h>

#define G(a,b)    (plcg->G[((b)*(max_it+1))+(a)])
#define gamma(a)  (plcg->gamma[(a)])
#define delta(a)  (plcg->delta[(a)])
#define sigma(a)  (plcg->sigma[(a)])
#define req(a)    (plcg->req[(a)])

typedef struct KSP_CG_PIPE_L_s KSP_CG_PIPE_L;
struct KSP_CG_PIPE_L_s {
  PetscInt    l;      /* pipeline depth */
  Vec         *Z;     /* Z vector (shifted base) */
  Vec         *V;     /* V vector (original base) */
  Vec         z_2;    /* additional vector needed when l == 1 */
  Vec         p,u,up,upp; /* some work vectors */
  PetscScalar *G;     /* such that Z = VG (actually a band matrix, storage could be improved to save memory)*/
  PetscScalar *gamma,*delta;
  PetscReal   lmin,lmax; /* min and max eigen values estimates to compute base shifts */
  PetscScalar *sigma; /* base shifts */
  MPI_Request *req;   /* request array for asynchronous global collective */
};

/**
 * KSPSetUp_PIPELCG - Sets up the workspace needed by the PIPELCG method.
 *
 * This is called once, usually automatically by KSPSolve() or KSPSetUp()
 * but can be called directly by KSPSetUp()
 */
static PetscErrorCode KSPSetUp_PIPELCG(KSP ksp)
{
  PetscErrorCode ierr;
  KSP_CG_PIPE_L  *plcg = (KSP_CG_PIPE_L*) ksp->data;
  PetscInt       l=plcg->l,max_it=ksp->max_it;

  PetscFunctionBegin;
  if (max_it < 1) {
    SETERRQ1(PetscObjectComm((PetscObject)ksp),PETSC_ERR_ARG_OUTOFRANGE,"%s: max_it argument must be positive.",((PetscObject)ksp)->type_name);
  }
  if (l < 1) {
    SETERRQ1(PetscObjectComm((PetscObject)ksp),PETSC_ERR_ARG_OUTOFRANGE,"%s: pipel argument must be positive.",((PetscObject)ksp)->type_name);
  }
  if (l > max_it) {
    SETERRQ1(PetscObjectComm((PetscObject)ksp),PETSC_ERR_ARG_OUTOFRANGE,"%s: pipel argument must be less than max_it.",((PetscObject)ksp)->type_name);
  }

  ierr = KSPSetWorkVecs(ksp,4);CHKERRQ(ierr); /* get work vectors needed by PIPELCG */
  plcg->p   = ksp->work[0];
  plcg->u   = ksp->work[1];
  plcg->up  = ksp->work[2];
  plcg->upp = ksp->work[3];

  ierr = VecDuplicateVecs(plcg->p,l+1,&plcg->Z);CHKERRQ(ierr);
  ierr = VecDuplicateVecs(plcg->p,2*l+1,&plcg->V);CHKERRQ(ierr);
  if (l == 1) {
    ierr = VecDuplicate(plcg->p,&plcg->z_2);CHKERRQ(ierr);
  }

  ierr = PetscCalloc1((max_it+1)*(max_it+1),&plcg->G);CHKERRQ(ierr);
  ierr = PetscCalloc1(max_it+1,&plcg->gamma);CHKERRQ(ierr);
  ierr = PetscCalloc1(max_it+1,&plcg->delta);CHKERRQ(ierr);
  ierr = PetscCalloc1(l,&plcg->sigma);CHKERRQ(ierr);
  ierr = PetscCalloc1(max_it+1,&plcg->req);CHKERRQ(ierr);


  PetscFunctionReturn(0);
}

static PetscErrorCode KSPDestroy_PIPELCG(KSP ksp)
{
  KSP_CG_PIPE_L *plcg = (KSP_CG_PIPE_L*) ksp->data;
  PetscInt      ierr=0,l=plcg->l;

  ierr = PetscFree(plcg->G);CHKERRQ(ierr);
  ierr = PetscFree(plcg->gamma);CHKERRQ(ierr);
  ierr = PetscFree(plcg->delta);CHKERRQ(ierr);
  ierr = PetscFree(plcg->sigma);CHKERRQ(ierr);
  ierr = PetscFree(plcg->req);CHKERRQ(ierr);
  ierr = VecDestroyVecs(l+1,&plcg->Z);CHKERRQ(ierr);
  ierr = VecDestroyVecs(2*l+1,&plcg->V);CHKERRQ(ierr);
  if (l == 1) {
    ierr = VecDestroy(&plcg->z_2);CHKERRQ(ierr);
  }
  KSPDestroyDefault(ksp);
  PetscFunctionReturn(0);
}

static PetscErrorCode KSPSetFromOptions_PIPELCG(PetscOptionItems *PetscOptionsObject,KSP ksp)
{
  PetscInt      ierr=0;
  KSP_CG_PIPE_L *plcg = (KSP_CG_PIPE_L*) ksp->data;
  PetscBool     flag=PETSC_FALSE;

  PetscFunctionBegin;
  ierr = PetscOptionsHead(PetscOptionsObject,"KSP PIPELCG options");CHKERRQ(ierr);
  ierr = PetscOptionsInt("-ksp_pipel","Pipeline length","",plcg->l,&plcg->l,&flag);CHKERRQ(ierr);
  if (!flag) plcg->l = 1;
  ierr = PetscOptionsReal("-ksp_lmin","Estimate for smallest eigenvalue","",plcg->lmin,&plcg->lmin,&flag);CHKERRQ(ierr);
  if (!flag) plcg->lmin = 0.0;
  ierr = PetscOptionsReal("-ksp_lmax","Estimate for largest eigenvalue","",plcg->lmax,&plcg->lmax,&flag);CHKERRQ(ierr);
  if (!flag) plcg->lmax = 0.0;
  ierr = PetscOptionsTail();CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

static PetscErrorCode KSPView_PIPELCG(KSP ksp,PetscViewer viewer)
{
  KSP_CG_PIPE_L  *plcg = (KSP_CG_PIPE_L*)ksp->data;
  PetscErrorCode ierr=0;
  PetscBool      iascii=PETSC_FALSE,isstring=PETSC_FALSE;

  PetscFunctionBegin;
  ierr = PetscObjectTypeCompare((PetscObject)viewer,PETSCVIEWERASCII,&iascii);CHKERRQ(ierr);
  ierr = PetscObjectTypeCompare((PetscObject)viewer,PETSCVIEWERSTRING,&isstring);CHKERRQ(ierr);
  if (iascii) {
    ierr = PetscViewerASCIIPrintf(viewer,"  Pipeline depth: %d\n", plcg->l);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,"  Minimal eigen value estimate %g\n",plcg->lmin);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,"  Maximal eigen value estimate %g\n",plcg->lmax);CHKERRQ(ierr);
  } else if (isstring) {
    ierr = PetscViewerStringSPrintf(viewer,"  Pipeline depth: %d\n", plcg->l);CHKERRQ(ierr);
    ierr = PetscViewerStringSPrintf(viewer,"  Minimal eigen value estimate %g\n",plcg->lmin);CHKERRQ(ierr);
    ierr = PetscViewerStringSPrintf(viewer,"  Maximal eigen value estimate %g\n",plcg->lmax);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

static PetscErrorCode KSPSolve_InnerLoop_PIPELCG(KSP ksp)
{
  KSP_CG_PIPE_L *plcg = (KSP_CG_PIPE_L*) ksp->data;
  Mat           A=NULL,Pmat=NULL;
  PetscInt      it=0,max_it=ksp->max_it,ierr=0,l=plcg->l,i=0,j=0,k=0;
  PetscInt      start=0,middle=0,end=0;
  Vec           *Z = plcg->Z,*V=plcg->V;
  Vec           x=NULL,p=NULL,u=NULL,up=NULL,upp=NULL,temp=NULL,z_2=NULL;
  PetscScalar   sum_dummy=0.0,beta=0.0,eta=0.0,zeta=0.0,lam=0.0,invbeta2=0.0;
  PetscReal     dp=0.0;

  x   = ksp->vec_sol;
  p   = plcg->p;
  u   = plcg->u;
  up  = plcg->up;
  upp = plcg->upp;
  z_2 = plcg->z_2;

  ierr = PCGetOperators(ksp->pc,&A,&Pmat);CHKERRQ(ierr);

  for (it = 0; it < max_it+l; ++it) {
    /* ----------------------------------- */
    /* Multiplication  z_{it+1} =  Az_{it} */
    /* ----------------------------------- */
    ierr = VecCopy(up,upp);CHKERRQ(ierr);
    ierr = VecCopy(u,up);CHKERRQ(ierr);
    if (it < l) {
      /* SpMV and Prec */
      ierr = MatMult(A,Z[l-it],u);CHKERRQ(ierr);
      ierr = KSP_PCApply(ksp,u,Z[l-it-1]);CHKERRQ(ierr);
      /* Apply shift */
      ierr = VecAXPY(Z[l-it-1],-sigma(it),Z[l-it]);CHKERRQ(ierr);
      ierr = VecAXPY(u,-sigma(it),Z[l-it]);CHKERRQ(ierr);
    } else {
      /* Shift the Z vector pointers */
      if (l == 1) {
        ierr = VecCopy(Z[l],z_2);CHKERRQ(ierr);
      }
      temp = Z[l];
      for (i = l; i > 0; --i) {
        Z[i] = Z[i-1];
      }
      Z[0] = temp;
      ierr = MatMult(A,Z[1],u);CHKERRQ(ierr);
      ierr = KSP_PCApply(ksp,u,Z[0]);CHKERRQ(ierr);
    }

    /* ----------------------------------- */
    /* Adjust the G matrix */
    /* ----------------------------------- */
    if (it >= l) {
      if (it == l) {
        /* MPI_Wait for G(0,0),scale V0 and Z and u vectors with 1/beta */
        MPI_Wait(&req(0),MPI_STATUS_IGNORE);
        beta = PetscSqrtScalar(G(0,0));
        G(0,0) = 1.0;
        ierr = VecAXPY(V[2*l],1.0/beta,p);CHKERRQ(ierr);
        for (j = 0; j <= l; ++j) {
          ierr = VecScale(Z[j],1.0/beta);CHKERRQ(ierr);
        }
        ierr = VecScale(u,1.0/beta);CHKERRQ(ierr);
        ierr = VecScale(up,1.0/beta);CHKERRQ(ierr);
        ierr = VecScale(upp,1.0/beta);CHKERRQ(ierr);
      }

      /* MPI_Wait until the dot products,started l iterations ago,are completed */
      MPI_Wait(&req(it-l+1),MPI_STATUS_IGNORE);
      if (it <= 2*l-1) {
        invbeta2 = 1.0 / (beta * beta);
        /* scale column 1 up to column l of G with 1/beta^2 */
        for (j = PetscMax(it-3*l+1,0); j <= it-l+1; ++j) {
          G(j,it-l+1) *= invbeta2;
        }
      }

      for (j = PetscMax(it-2*l+2,0); j <= it-l; ++j) {
        sum_dummy = 0.0;
        for (k = PetscMax(it-3*l+1,0); k <= j-1; ++k) {
          sum_dummy = sum_dummy + G(k,j) * G(k,it-l+1);
        }
        G(j,it-l+1) = (G(j,it-l+1) - sum_dummy) / G(j,j);
      }

      sum_dummy = 0.0;
      for (k = PetscMax(it-3*l+1,0); k <= it-l; ++k) {
        sum_dummy = sum_dummy + G(k,it-l+1) * G(k,it-l+1);
      }

      /* Breakdown check */
      if (G(it-l+1,it-l+1) - sum_dummy < 0) {
        PetscPrintf(PETSC_COMM_WORLD,"sqrt breakdown in iteration %d: value is %e\n",ksp->its+1,G(it-l+1,it-l+1) - sum_dummy);
        /* End hanging dot-products in the pipeline before exiting for-loop */
        start = it-l+2;
        end = PetscMin(it+1,max_it+1);  /* !warning! 'it' can actually be greater than 'max_it' */
        for (i = start; i < end; ++i) {
          MPI_Wait(&req(i),MPI_STATUS_IGNORE);
        }
        break;
      }
      G(it-l+1,it-l+1) = PetscSqrtReal(G(it-l+1,it-l+1) - sum_dummy);

      if (it < 2*l) {
        if (it == l) {
          gamma(it-l) = (G(it-l,it-l+1) + sigma(it-l) * G(it-l,it-l)) / G(it-l,it-l);
        } else {
          gamma(it-l) = (G(it-l,it-l+1) + sigma(it-l) * G(it-l,it-l)
                         - delta(it-l-1) * G(it-l-1,it-l)) / G(it-l,it-l);
        }
        delta(it-l) = G(it-l+1,it-l+1) / G(it-l,it-l);
      } else if (it == 2*l) {
        gamma(it-l) = (G(it-l,it-l) * gamma(it-2*l)
                       + G(it-l,it-l+1) * delta(it-2*l)
                       - G(it-l-1,it-l) * delta(it-l-1)) / G(it-l,it-l);
        delta(it-l) = (G(it-l+1,it-l+1) * delta(it-2*l)) / G(it-l,it-l);
      } else {
        gamma(it-l) = (G(it-l,it-l-1) * delta(it-2*l-1)
                       + G(it-l,it-l) * gamma(it-2*l)
                       + G(it-l,it-l+1) * delta(it-2*l)
                       - G(it-l-1,it-l) * delta(it-l-1)) / G(it-l,it-l);
        delta(it-l) = (G(it-l+1,it-l+1) * delta(it-2*l)) / G(it-l,it-l);
      }

      /* -------------------------------------------- */
      /* Recursively compute the next V and Z vectors */
      /* -------------------------------------------- */
      /* Recurrence V vectors */
      if (it < 3*l) {
        ierr = VecAXPY(V[3*l-it-1],1.0/G(it-l+1,it-l+1),Z[l]);CHKERRQ(ierr);
        for (j = PetscMax(it-3*l+1,0); j <= it-l; ++j) {
          ierr = VecAXPY(V[3*l-it-1],-G(j,it-l+1)/G(it-l+1,it-l+1),V[2*l-j]);CHKERRQ(ierr);
        }
      } else {
        /* Shift the V vector pointers */
        temp = V[2*l];
        for (i = 2*l; i>0; i--) {
          V[i] = V[i-1];
        }
        V[0] = temp;

        ierr = VecSet(V[0],0.0);
        ierr = VecAXPY(V[0],1.0/G(it-l+1,it-l+1),Z[l]) ;CHKERRQ(ierr);
        for (j = PetscMax(it-3*l+1,0); j <= it-l; ++j) {
          ierr = VecAXPY(V[0],-G(j,it-l+1)/G(it-l+1,it-l+1),V[it-l+1-j]);CHKERRQ(ierr);
        }
      }
      /* Recurrence Z vectors */
      if (it > l) {
        if (l == 1) {
          ierr = VecAXPY(Z[0],-delta(it-l-1),z_2);CHKERRQ(ierr);
        } else {
          ierr = VecAXPY(Z[0],-delta(it-l-1),Z[2]);CHKERRQ(ierr);
        }
        ierr = VecAXPY(u,-delta(it-l-1),upp);CHKERRQ(ierr); /* Recurrence u vectors */
      }
      ierr = VecAXPY(Z[0],-gamma(it-l),Z[1]);CHKERRQ(ierr);
      ierr = VecScale(Z[0],1.0/delta(it-l));CHKERRQ(ierr);
      /* Recurrence u vectors */
      ierr = VecAXPY(u,-gamma(it-l),up);CHKERRQ(ierr);
      ierr = VecScale(u,1.0/delta(it-l));CHKERRQ(ierr);
    }

    /* ---------------------------------------- */
    /* Compute and communicate the dot products */
    /* ---------------------------------------- */
    if (it < l) {
      /* dot-product (Z_{it+1},z_j) */
      for (j = 0; j < it+2; ++j) {
        u->ops->dot_local(u,Z[l-j],&G(j,it+1));
      }
      MPI_Iallreduce(MPI_IN_PLACE,&G(0,it+1),it+2,MPIU_SCALAR,MPI_SUM,PETSC_COMM_WORLD,&req(it+1));
    } else if ((it >= l) && (it < max_it)) {
      start = PetscMax(0,it-2*l+1);
      middle = it-l+2;
      end = it+2;
      for (j = start; j < middle; ++j) { /* dot-product (Z_{it+1},v_j) */
        temp = (it < 3*l) ? plcg->V[2*l-j] : plcg->V[it-l+1-j];
        u->ops->dot_local(u,temp,&G(j,it+1));
      }
      for (j = middle; j < end; ++j) { /* dot-product (Z_{it+1},z_j) */
        u->ops->dot_local(u,plcg->Z[it+1-j],&G(j,it+1));
      }
      MPI_Iallreduce(MPI_IN_PLACE,&G(start,it+1),end-start,MPIU_SCALAR,MPI_SUM,PETSC_COMM_WORLD,&req(it+1));
    }

    /* ----------------------------------------- */
    /* Compute solution vector and residual norm */
    /* ----------------------------------------- */
    if (it >= l) {
      if (it == l) {
        if (ksp->its != 0) {
          ++ ksp->its;
        }
        eta = gamma(0);
        zeta = beta;
        VecCopy(V[2*l],p);
        VecScale(p,1.0/eta);
        VecAXPY(x,zeta,p);

        dp         = beta;
        ksp->rnorm = dp;
        ierr       = KSPLogResidualHistory(ksp,dp);CHKERRQ(ierr);
        ierr       = KSPMonitor(ksp,ksp->its,dp);CHKERRQ(ierr);
        ierr       = (*ksp->converged)(ksp,ksp->its,dp,&ksp->reason,ksp->cnvP);CHKERRQ(ierr);
      } else if (it > l) {
        k = it-l;
        ++ ksp->its;
        lam = delta(k-1) / eta;
        eta = gamma(k) - lam * delta(k-1);
        zeta = -lam * zeta;
        VecScale(p,-delta(k-1)/eta);
        VecAXPY(p,1.0/eta,(it < 3*l) ? V[3*l-it] : V[1]);
        VecAXPY(x,zeta,p);

        dp         = PetscAbsReal(zeta);
        ksp->rnorm = dp;
        ierr       = KSPLogResidualHistory(ksp,dp);CHKERRQ(ierr);
        ierr       = KSPMonitor(ksp,ksp->its,dp);CHKERRQ(ierr);
        ierr       = (*ksp->converged)(ksp,ksp->its,dp,&ksp->reason,ksp->cnvP);CHKERRQ(ierr);
      }
      if (ksp->its >= max_it-1) {
        ksp->reason = KSP_DIVERGED_ITS;
      }
      if (ksp->reason) {
        /* End hanging dot-products in the pipeline before exiting for-loop */
        start = it-l+2;
        end = PetscMin(it+2,max_it+1); /* !warning! 'it' can actually be greater than 'max_it' */
        for (i = start; i < end; ++i) {
          MPI_Wait(&req(i),MPI_STATUS_IGNORE);
        }
        break;
      }
    }
  } /* End inner for loop */
  PetscFunctionReturn(0);
}

static PetscErrorCode KSPSolve_ReInitData_PIPELCG(KSP ksp)
{
  KSP_CG_PIPE_L *plcg = (KSP_CG_PIPE_L*) ksp->data;
  PetscInt      ierr=0,i=0,j=0,l=plcg->l,max_it=ksp->max_it;

  PetscFunctionBegin;
  ierr = VecSet(plcg->up,0.0);CHKERRQ(ierr);
  ierr = VecSet(plcg->upp,0.0);CHKERRQ(ierr);
  for (i = 0; i < l+1; ++i) {
    ierr = VecSet(plcg->Z[i],0.0);CHKERRQ(ierr);
  }
  for (i = 0; i < (2*l+1); ++i) {
    ierr = VecSet(plcg->V[i],0.0);CHKERRQ(ierr);
  }
  for (i = 0; i < (max_it+1); ++i) {
    gamma(i) = 0.0;
    delta(i) = 0.0;
    for (j = 0; j < (max_it+1); ++j) {
      G(i,j) = 0.0;
    }
  }
  PetscFunctionReturn(0);
}

/**
 * KSPSolve_PIPELCG - This routine actually applies the pipelined(l) conjugate gradient method
 *
 * Input Parameter:
 *     ksp - the Krylov space object that was set to use conjugate gradient,by,for
 *     example,KSPCreate(MPI_Comm,KSP *ksp); KSPSetType(ksp,KSPCG);
 */
static PetscErrorCode KSPSolve_PIPELCG(KSP ksp)
{
  PetscErrorCode ierr=0;
  KSP_CG_PIPE_L  *plcg = (KSP_CG_PIPE_L*) ksp->data;
  Mat            A=NULL,Pmat=NULL;
  Vec            b=NULL,x=NULL,p=NULL,u=NULL;
  PetscInt       max_it=ksp->max_it,l=plcg->l;
  PetscInt       i=0,outer_it=0,curr_guess_zero=0;
  PetscScalar    lmin=plcg->lmin,lmax=plcg->lmax;
  PetscBool      diagonalscale=PETSC_FALSE;
  PCType         pctype=PCNONE;

  PetscFunctionBegin;
  ierr = PCGetDiagonalScale(ksp->pc,&diagonalscale);CHKERRQ(ierr);
  if (diagonalscale) {
    SETERRQ1(PetscObjectComm((PetscObject)ksp),PETSC_ERR_SUP,"Krylov method %s does not support diagonal scaling",((PetscObject)ksp)->type_name);
  }

  x = ksp->vec_sol;
  b = ksp->vec_rhs;
  p = plcg->p;
  u = plcg->u;

  ierr = PCGetOperators(ksp->pc,&A,&Pmat);CHKERRQ(ierr);
  ierr = PCGetType(ksp->pc,&pctype);CHKERRQ(ierr);
  PetscPrintf(PETSC_COMM_WORLD,"PCType is: %s \n",pctype);
  PetscPrintf(PETSC_COMM_WORLD,"KSPNormType is: %d \n",ksp->normtype);

  if (strcmp(pctype,PCNONE) == 0) {
    for (i = 0; i < l; ++i) {
      sigma(i) = (0.5*(lmin+lmax) + (0.5*(lmax-lmin) * PetscCosReal(PETSC_PI*(2.0*i+1.0)/(2.0*l))));
    }
  } else {
    for (i = 0; i < l; ++i) {
      sigma(i) = 0.0;
    }
  }

  ksp->its = 0;
  outer_it = 0;
  curr_guess_zero = !! ksp->guess_zero;

  while (ksp->its < max_it) { /* OUTER LOOP (gmres-like restart to handle breakdowns) */
    /* * */
    /* RESTART LOOP */
    /* * */
    if (!curr_guess_zero) {
      ierr = KSP_MatMult(ksp,A,x,u);CHKERRQ(ierr);  /* u <- b - Ax */
      ierr = VecAYPX(u,-1.0,b);CHKERRQ(ierr);
    } else {
      ierr = VecCopy(b,u);CHKERRQ(ierr);            /* u <- b (x is 0) */
    }
    ierr = KSP_PCApply(ksp,u,p);CHKERRQ(ierr);      /* p <- Bu */

    if (outer_it > 0) {
      /* Re-initialize Z,V,gamma,delta,G,u,up,upp after restart occurred */
      ierr = KSPSolve_ReInitData_PIPELCG(ksp);CHKERRQ(ierr);
    }

    u->ops->dot_local(u,p,&G(0,0));
    MPI_Iallreduce(MPI_IN_PLACE,&G(0,0),1,MPIU_SCALAR,MPI_SUM,PETSC_COMM_WORLD,&req(0));
    ierr = VecCopy(p,plcg->Z[l]);CHKERRQ(ierr);

    ierr = KSPSolve_InnerLoop_PIPELCG(ksp);CHKERRQ(ierr);

    if (ksp->reason) break; /* convergence or divergence */
    ++ outer_it;
    curr_guess_zero = 0;
  }

  if (ksp->its >= max_it-1) {
    ksp->reason = KSP_DIVERGED_ITS;
  }
  PetscFunctionReturn(0);
}

/*MC
 KSPPIPELCG - Pipelined conjugate gradient method with deeper pipelines.
 M*/
PETSC_EXTERN
PetscErrorCode KSPCreate_PIPELCG(KSP ksp)
{
  PetscErrorCode ierr;
  KSP_CG_PIPE_L  *plcg = NULL;

  PetscFunctionBegin;
  ierr = PetscNewLog(ksp,&plcg);CHKERRQ(ierr);
  ksp->data = (void*) plcg;

  ierr = KSPSetSupportedNorm(ksp,KSP_NORM_UNPRECONDITIONED,PC_LEFT,2);CHKERRQ(ierr);
  ierr = KSPSetSupportedNorm(ksp,KSP_NORM_NATURAL,PC_LEFT,2);CHKERRQ(ierr);

  ksp->ops->setup          = KSPSetUp_PIPELCG;
  ksp->ops->solve          = KSPSolve_PIPELCG;
  ksp->ops->destroy        = KSPDestroy_PIPELCG;
  ksp->ops->view           = KSPView_PIPELCG;
  ksp->ops->setfromoptions = KSPSetFromOptions_PIPELCG;
  ksp->ops->buildsolution  = KSPBuildSolutionDefault;
  ksp->ops->buildresidual  = KSPBuildResidualDefault;

  PetscFunctionReturn(0);
}
