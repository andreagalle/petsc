/* 
 GAMG geometric-algebric multiogrid PC - Mark Adams 2011
 */
#include <../src/ksp/pc/impls/gamg/gamg.h>

PetscLogEvent gamg_setup_stages[NUM_SET];

/* Private context for the GAMG preconditioner */
typedef struct gamg_TAG {
  PetscInt       m_dim;
  PetscInt       m_Nlevels;
  PetscInt       m_data_sz;
  PetscReal     *m_data; /* blocked vector of vertex data on fine grid (coordinates) */
} PC_GAMG;
 
#define TOP_GRID_LIM 100

/* -----------------------------------------------------------------------------*/
#undef __FUNCT__
#define __FUNCT__ "PCReset_GAMG"
PetscErrorCode PCReset_GAMG(PC pc)
{
  PetscErrorCode  ierr;
  PC_MG           *mg = (PC_MG*)pc->data;
  PC_GAMG         *pc_gamg = (PC_GAMG*)mg->innerctx;

  PetscFunctionBegin;
  ierr = PetscFree(pc_gamg->m_data);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/* -------------------------------------------------------------------------- */
/*
   PCSetCoordinates_GAMG

   Input Parameter:
   .  pc - the preconditioner context
*/
EXTERN_C_BEGIN
#undef __FUNCT__
#define __FUNCT__ "PCSetCoordinates_GAMG"
PetscErrorCode PCSetCoordinates_GAMG(PC pc, const int ndm,PetscReal *coords )
{
  PC_MG          *mg = (PC_MG*)pc->data;
  PC_GAMG        *pc_gamg = (PC_GAMG*)mg->innerctx;
  PetscErrorCode ierr;
  PetscInt       bs, my0, tt;
  Mat            mat = pc->pmat;
  PetscInt       arrsz;

  PetscFunctionBegin;
  ierr  = MatGetBlockSize( mat, &bs );               CHKERRQ( ierr );
  ierr  = MatGetOwnershipRange( mat, &my0, &tt ); CHKERRQ(ierr);
  arrsz = (tt-my0)/bs*ndm;

  // put coordinates
  if (!pc_gamg->m_data || (pc_gamg->m_data_sz != arrsz)) {
    ierr = PetscFree( pc_gamg->m_data );  CHKERRQ(ierr);
    ierr = PetscMalloc(arrsz*sizeof(double), &pc_gamg->m_data ); CHKERRQ(ierr);
  }

  /* copy data in */
  for(tt=0;tt<arrsz;tt++){
    pc_gamg->m_data[tt] = coords[tt];
  }
  pc_gamg->m_data_sz = arrsz;
  pc_gamg->m_dim = ndm;

  PetscFunctionReturn(0);
}
EXTERN_C_END

/* -------------------------------------------------------------------------- */
/*
   partitionLevel

   Input Parameter:
   . a_Amat_fine - matrix on this fine (k) level
   . a_dime - 2 or 3
   In/Output Parameter:
   . a_P_inout - prolongation operator to the next level (k-1)
   . a_coarse_crds - coordinates that need to be moved
   . a_active_proc - number of active procs
   Output Parameter:
   . a_Amat_crs - coarse matrix that is created (k-1)
*/

#undef __FUNCT__
#define __FUNCT__ "partitionLevel"
PetscErrorCode partitionLevel( Mat a_Amat_fine,
                               PetscInt a_dim,
                               Mat *a_P_inout,
                               PetscReal **a_coarse_crds,
                               PetscMPIInt *a_active_proc,
                               Mat *a_Amat_crs
                            )
{
  PetscErrorCode   ierr;
  Mat              Amat, Pnew, Pold = *a_P_inout;
  IS               new_indices,isnum;
  MPI_Comm         wcomm = ((PetscObject)a_Amat_fine)->comm;
  PetscMPIInt      nactive_procs,mype,npe;
  PetscInt         Istart,Iend,Istart0,Iend0,ncrs0,ncrs_new,bs=1; /* bs ??? */

  PetscFunctionBegin;
  ierr = MPI_Comm_rank(wcomm,&mype);CHKERRQ(ierr);
  ierr = MPI_Comm_size(wcomm,&npe);CHKERRQ(ierr);
  /* RAP */
  ierr = MatPtAP( a_Amat_fine, Pold, MAT_INITIAL_MATRIX, 2.0, &Amat ); CHKERRQ(ierr);
  ierr = MatGetOwnershipRange( Amat, &Istart0, &Iend0 );    CHKERRQ(ierr); /* x2 size of 'a_coarse_crds' */
  ncrs0 = (Iend0 - Istart0)/bs;

  /* Repartition Amat_{k} and move colums of P^{k}_{k-1} and coordinates accordingly */
  {
    PetscInt        neq,N,counts[npe];
    IS              isnewproc;
    PetscMPIInt     new_npe,targ_npe;

    ierr = MatGetSize( Amat, &neq, &N );CHKERRQ(ierr);
#define MIN_EQ_PROC 100
    nactive_procs = *a_active_proc;
    targ_npe = neq/MIN_EQ_PROC; /* hardwire min. number of eq/proc */
    if( targ_npe == 0 || neq < TOP_GRID_LIM ) new_npe = 1; /* chop coarsest grid */
    else if (targ_npe >= nactive_procs ) new_npe = nactive_procs; /* no change */
    else {
      PetscMPIInt     factstart,fact;
      new_npe = -9999;
      factstart = nactive_procs;
      for(fact=factstart;fact>0;fact--){ /* try to find a better number of procs */
        if( nactive_procs%fact==0 && neq/(nactive_procs/fact) > MIN_EQ_PROC ) {
          new_npe = nactive_procs/fact;
        }
      }
      assert(new_npe != -9999);
    }
    *a_active_proc = new_npe; /* output for next time */
PetscPrintf(PETSC_COMM_WORLD,"\t\t%s num active procs = %d (N loc = %d)\n",__FUNCT__,new_npe,ncrs0);
    { /* partition: get 'isnewproc' */
      MatPartitioning  mpart;
      Mat              adj;
      const PetscInt  *is_idx;
      PetscInt         is_sz,kk,jj,old_fact=(npe/nactive_procs),new_fact=(npe/new_npe),*isnewproc_idx;
      /* create sub communicator  */
      MPI_Comm cm,new_comm;
      int membershipKey = mype % old_fact;
      ierr = MPI_Comm_split(wcomm, membershipKey, mype, &cm); CHKERRQ(ierr);
      ierr = PetscCommDuplicate( cm, &new_comm, PETSC_NULL ); CHKERRQ(ierr);
      ierr = MPI_Comm_free( &cm );                            CHKERRQ(ierr);

      /* MatPartitioningApply call MatConvert, which is collective */
      ierr = MatConvert(Amat,MATMPIADJ,MAT_INITIAL_MATRIX,&adj);CHKERRQ(ierr);
      if( membershipKey == 0 ) {
        /* hack to fix global data that pmetis.c uses in 'adj' */
        for( kk=0 , jj=0 ; kk<=npe ; jj++, kk += old_fact ) {
          adj->rmap->range[jj] = adj->rmap->range[kk];
        }
        ierr = MatPartitioningCreate( new_comm, &mpart ); CHKERRQ(ierr);
        ierr = MatPartitioningSetAdjacency( mpart, adj ); CHKERRQ(ierr);
        ierr = MatPartitioningSetFromOptions( mpart ); CHKERRQ(ierr);
        ierr = MatPartitioningSetNParts( mpart, new_npe ); CHKERRQ(ierr);
        ierr = MatPartitioningApply( mpart, &isnewproc ); CHKERRQ(ierr);
        ierr = MatPartitioningDestroy( &mpart ); CHKERRQ(ierr);
        /* collect IS info */
        ierr = ISGetLocalSize( isnewproc, &is_sz );        CHKERRQ(ierr);
        ierr = PetscMalloc( is_sz*sizeof(PetscInt), &isnewproc_idx ); CHKERRQ(ierr);
        ierr = ISGetIndices( isnewproc, &is_idx );     CHKERRQ(ierr);
        for(kk=0;kk<is_sz;kk++){
          /* spread partitioning across machine - probably the right thing to do but machine specific */
          isnewproc_idx[kk] = is_idx[kk] * new_fact;
        }
        ierr = ISRestoreIndices( isnewproc, &is_idx );     CHKERRQ(ierr);
        ierr = ISDestroy( &isnewproc );                    CHKERRQ(ierr);
      }
      else {
        isnewproc_idx = 0;
        is_sz = 0;
      }
      ierr = MatDestroy( &adj );                       CHKERRQ(ierr);
      ierr = MPI_Comm_free( &new_comm );    CHKERRQ(ierr);
      ierr = ISCreateGeneral( wcomm, is_sz, isnewproc_idx, PETSC_COPY_VALUES, &isnewproc );
      if( membershipKey == 0 ) {
        ierr = PetscFree( isnewproc_idx );  CHKERRQ(ierr);
      }
    }

    /*
     Create an index set from the isnewproc index set to indicate the mapping TO
     */
    ierr = ISPartitioningToNumbering( isnewproc, &isnum ); CHKERRQ(ierr);
    /*
     Determine how many elements are assigned to each processor
     */
    ierr = ISPartitioningCount( isnewproc, npe, counts ); CHKERRQ(ierr);
    ierr = ISDestroy( &isnewproc );                       CHKERRQ(ierr);
    ncrs_new = counts[mype];
  }

  { /* Create a vector to contain the newly ordered element information */
    const PetscInt *idx;
    PetscInt        i,j,k;
    IS              isscat;
    PetscScalar    *array;
    Vec             src_crd, dest_crd;
    PetscReal      *coords = *a_coarse_crds;
    VecScatter      vecscat;

    ierr = VecCreate( wcomm, &dest_crd );
    ierr = VecSetSizes( dest_crd, a_dim*ncrs_new, PETSC_DECIDE ); CHKERRQ(ierr);
    ierr = VecSetFromOptions( dest_crd ); CHKERRQ(ierr); /*funny vector-get global options?*/
    /*
     There are three data items per cell (element), the integer vertex numbers of its three
     coordinates (we convert to double to use the scatter) (one can think
     of the vectors of having a block size of 3, then there is one index in idx[] for each element)
     */
    {
      PetscInt tidx[ncrs0*a_dim];
      ierr = ISGetIndices( isnum, &idx ); CHKERRQ(ierr);
      for (i=0,j=0; i<ncrs0; i++) {
        for (k=0; k<a_dim; k++) tidx[j++] = idx[i]*a_dim + k;
      }
      ierr = ISCreateGeneral( wcomm, a_dim*ncrs0, tidx, PETSC_COPY_VALUES, &isscat );
      CHKERRQ(ierr);
      ierr = ISRestoreIndices( isnum, &idx ); CHKERRQ(ierr);
    }
    /*
     Create a vector to contain the original vertex information for each element
     */
    ierr = VecCreateSeq( PETSC_COMM_SELF, a_dim*ncrs0, &src_crd ); CHKERRQ(ierr);
    ierr = VecGetArray( src_crd, &array );                        CHKERRQ(ierr);
    for (i=0; i<a_dim*ncrs0; i++) array[i] = coords[i];
    ierr = VecRestoreArray( src_crd, &array );                     CHKERRQ(ierr);
    /*
     Scatter the element vertex information (still in the original vertex ordering)
     to the correct processor
     */
    ierr = VecScatterCreate( src_crd, PETSC_NULL, dest_crd, isscat, &vecscat);
    CHKERRQ(ierr);
    ierr = ISDestroy( &isscat );       CHKERRQ(ierr);
    ierr = VecScatterBegin(vecscat,src_crd,dest_crd,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
    ierr = VecScatterEnd(vecscat,src_crd,dest_crd,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
    ierr = VecScatterDestroy( &vecscat );       CHKERRQ(ierr);
    ierr = VecDestroy( &src_crd );       CHKERRQ(ierr);
    /*
     Put the element vertex data into a new allocation of the gdata->ele
     */
    ierr = PetscFree( *a_coarse_crds );    CHKERRQ(ierr);
    ierr = PetscMalloc( a_dim*ncrs_new*sizeof(PetscReal), a_coarse_crds );    CHKERRQ(ierr);
    coords = *a_coarse_crds; /* convient */
    ierr = VecGetArray( dest_crd, &array );    CHKERRQ(ierr);
    for (i=0; i<a_dim*ncrs_new; i++) coords[i] = PetscRealPart(array[i]);
    ierr = VecRestoreArray( dest_crd, &array );    CHKERRQ(ierr);
    ierr = VecDestroy( &dest_crd );    CHKERRQ(ierr);
  }
  /*
   Invert for MatGetSubMatrix
   */
  ierr = ISInvertPermutation( isnum, ncrs_new, &new_indices ); CHKERRQ(ierr);
  ierr = ISSort( new_indices ); CHKERRQ(ierr); /* is this needed? */
  ierr = ISDestroy( &isnum ); CHKERRQ(ierr);
  /* A_crs output */
  ierr = MatGetSubMatrix( Amat, new_indices, new_indices, MAT_INITIAL_MATRIX, a_Amat_crs );
  CHKERRQ(ierr);
  ierr = MatDestroy( &Amat ); CHKERRQ(ierr);
  Amat = *a_Amat_crs;
  /* prolongator */
  ierr = MatGetOwnershipRange( Pold, &Istart, &Iend );    CHKERRQ(ierr);
  {
    IS findices;
    ierr = ISCreateStride(wcomm,Iend-Istart,Istart,1,&findices);   CHKERRQ(ierr);
    ierr = MatGetSubMatrix( Pold, findices, new_indices, MAT_INITIAL_MATRIX, &Pnew );
    CHKERRQ(ierr);
    ierr = ISDestroy( &findices ); CHKERRQ(ierr);
  }
  ierr = MatDestroy( a_P_inout ); CHKERRQ(ierr);
  *a_P_inout = Pnew; /* output */

  ierr = ISDestroy( &new_indices ); CHKERRQ(ierr);

  PetscFunctionReturn(0);
}

#define GAMG_MAXLEVELS 20
#if defined(PETSC_USE_LOG)
PetscLogStage  gamg_stages[20];
#endif
/* -------------------------------------------------------------------------- */
/*
   PCSetUp_GAMG - Prepares for the use of the GAMG preconditioner
                    by setting data structures and options.

   Input Parameter:
.  pc - the preconditioner context

   Application Interface Routine: PCSetUp()

   Notes:
   The interface routine PCSetUp() is not usually called directly by
   the user, but instead is called by PCApply() if necessary.
*/
#undef __FUNCT__
#define __FUNCT__ "PCSetUp_GAMG"
PetscErrorCode PCSetUp_GAMG( PC pc )
{
  PetscErrorCode  ierr;
  PC_MG           *mg = (PC_MG*)pc->data;
  PC_GAMG         *pc_gamg = (PC_GAMG*)mg->innerctx;
  Mat              Amat = pc->mat, Pmat = pc->pmat;
  PetscBool        isSeq, isMPI;
  PetscInt         fine_level, level, level1, M, N, bs, lidx;
  MPI_Comm         wcomm = ((PetscObject)pc)->comm;
  PetscMPIInt      mype,npe,nactivepe;
  PetscBool isOK;

  Mat Aarr[GAMG_MAXLEVELS], Parr[GAMG_MAXLEVELS];  PetscReal *coarse_crds = 0, *crds = pc_gamg->m_data;
  
  PetscFunctionBegin;
  ierr = MPI_Comm_rank(wcomm,&mype);CHKERRQ(ierr);
  ierr = MPI_Comm_size(wcomm,&npe);CHKERRQ(ierr);
  if (pc->setupcalled){
    /* no state data in GAMG to destroy (now) */
    ierr = PCReset_MG(pc); CHKERRQ(ierr);
  }
  if (!pc_gamg->m_data) SETERRQ(wcomm,PETSC_ERR_SUP,"PCSetUp_GAMG called before PCSetCoordinates");
  /* setup special features of PCGAMG */
  ierr = PetscTypeCompare((PetscObject)Amat, MATSEQAIJ, &isSeq);CHKERRQ(ierr);
  ierr = PetscTypeCompare((PetscObject)Amat, MATMPIAIJ, &isMPI);CHKERRQ(ierr);
  if (isMPI) {
  } else if (isSeq) {
  } else SETERRQ1(wcomm,PETSC_ERR_ARG_WRONG, "Matrix type '%s' cannot be used with GAMG. GAMG can only handle AIJ matrices.",((PetscObject)Amat)->type_name);

  /* GAMG requires input of fine-grid matrix. It determines nlevels. */
  ierr = MatGetBlockSize( Amat, &bs ); CHKERRQ(ierr);
  if(bs!=1) SETERRQ1(wcomm,PETSC_ERR_ARG_WRONG, "GAMG only supports scalar prblems bs = '%d'.",bs);

  /* Get A_i and R_i */
  ierr = MatGetSize( Amat, &M, &N );CHKERRQ(ierr);
PetscPrintf(PETSC_COMM_WORLD,"[%d]%s level %d N=%d\n",0,__FUNCT__,0,N);
  for (level=0, Aarr[0] = Pmat, nactivepe = npe;
       level < GAMG_MAXLEVELS-1 && (level==0 || M>TOP_GRID_LIM); /* hard wired stopping logic */
       level++ ) {
    level1 = level + 1;
    ierr = PetscLogEventBegin(gamg_setup_stages[SET1],0,0,0,0);CHKERRQ(ierr);
    ierr = createProlongation( Aarr[level], crds, pc_gamg->m_dim,
                               &Parr[level1], &coarse_crds, &isOK );
    CHKERRQ(ierr);
    ierr = PetscLogEventEnd(gamg_setup_stages[SET1],0,0,0,0);CHKERRQ(ierr);

    ierr = PetscFree( crds ); CHKERRQ( ierr );
    if(level==0) Aarr[0] = Amat; /* use Pmat for finest level setup, but use mat for solver */
    if( isOK ) {
      ierr = PetscLogEventBegin(gamg_setup_stages[SET2],0,0,0,0);CHKERRQ(ierr);
      ierr = partitionLevel( Aarr[level], pc_gamg->m_dim, &Parr[level1], &coarse_crds, &nactivepe, &Aarr[level1] );
      CHKERRQ(ierr);
      ierr = PetscLogEventEnd(gamg_setup_stages[SET2],0,0,0,0);CHKERRQ(ierr);
      ierr = MatGetSize( Aarr[level1], &M, &N );CHKERRQ(ierr);
PetscPrintf(PETSC_COMM_WORLD,"[%d]%s done level %d N=%d, active pe = %d\n",0,__FUNCT__,level1,N,nactivepe);
    }
    else{
      break;
    }
    crds = coarse_crds;
  }
  ierr = PetscFree( coarse_crds ); CHKERRQ( ierr );
PetscPrintf(PETSC_COMM_WORLD,"\t[%d]%s %d levels\n",0,__FUNCT__,level + 1);
  pc_gamg->m_data = 0; /* destroyed coordinate data */
  pc_gamg->m_Nlevels = level + 1;
  fine_level = level;
  ierr = PCMGSetLevels(pc,pc_gamg->m_Nlevels,PETSC_NULL);CHKERRQ(ierr);

  /* set default smoothers */
  for (level=1,lidx=pc_gamg->m_Nlevels-2;
       level<=fine_level;
       level++,lidx--) {
    PetscReal emax = 3.0, emin;
    KSP smoother; PC subpc;
    ierr = PCMGGetSmoother(pc,level,&smoother);CHKERRQ(ierr);
    ierr = KSPSetType(smoother,KSPCHEBYCHEV);CHKERRQ(ierr);
    ierr = KSPGetPC(smoother,&subpc);CHKERRQ(ierr);
    ierr = PCSetType(subpc,PCJACOBI);CHKERRQ(ierr);
    { /* eigen estimate 'emax' */
      KSP eksp; Mat Amat = Aarr[lidx];
      Vec bb, xx; PC pc;
      PetscInt N1, N0, tt;
      ierr = MatGetVecs( Amat, &bb, 0 );         CHKERRQ(ierr);
      ierr = MatGetVecs( Amat, &xx, 0 );         CHKERRQ(ierr);
      {
	PetscRandom    rctx;
	ierr = PetscRandomCreate(wcomm,&rctx);CHKERRQ(ierr);
	ierr = PetscRandomSetFromOptions(rctx);CHKERRQ(ierr);
	ierr = VecSetRandom(bb,rctx);CHKERRQ(ierr);
	ierr = PetscRandomDestroy( &rctx ); CHKERRQ(ierr);
      }
      ierr = KSPCreate(wcomm,&eksp);CHKERRQ(ierr);
      ierr = KSPSetInitialGuessNonzero( eksp, PETSC_FALSE ); CHKERRQ(ierr);
      ierr = KSPSetOperators( eksp, Amat, Amat, DIFFERENT_NONZERO_PATTERN ); CHKERRQ( ierr );
      ierr = KSPGetPC( eksp, &pc );CHKERRQ( ierr );
      ierr = PCSetType( pc, PCJACOBI ); CHKERRQ(ierr); /* should be same as above */
      ierr = KSPSetTolerances( eksp, PETSC_DEFAULT, PETSC_DEFAULT, PETSC_DEFAULT, 5 );
      CHKERRQ(ierr);
      ierr = KSPSetConvergenceTest( eksp, KSPSkipConverged, 0, 0 ); CHKERRQ(ierr);
      ierr = KSPSetNormType( eksp, KSP_NORM_NONE );                 CHKERRQ(ierr);

      ierr = KSPSetComputeSingularValues( eksp,PETSC_TRUE ); CHKERRQ(ierr);
      ierr = KSPSolve(eksp,bb,xx); CHKERRQ(ierr);
      ierr = KSPComputeExtremeSingularValues( eksp, &emax, &emin ); CHKERRQ(ierr);

      ierr = VecDestroy( &xx );       CHKERRQ(ierr);
      ierr = VecDestroy( &bb );       CHKERRQ(ierr);
      ierr = KSPDestroy( &eksp );       CHKERRQ(ierr);

      ierr = MatGetSize( Amat, &N1, &tt );         CHKERRQ(ierr);
      ierr = MatGetSize( Aarr[lidx+1], &N0, &tt );CHKERRQ(ierr);
      emin = emax/((PetscReal)N1/(PetscReal)N0); /* this should be about the coarsening rate */
    }
    ierr = KSPChebychevSetEigenvalues( smoother, emax, emin );CHKERRQ(ierr);
  }
  /* should be called in PCSetFromOptions_GAMG(), but cannot be called prior to PCMGSetLevels() */
  ierr = PCSetFromOptions_MG(pc); CHKERRQ(ierr);
  {
    PetscBool galerkin;
    ierr = PCMGGetGalerkin( pc,  &galerkin); CHKERRQ(ierr);
    if(galerkin){
      SETERRQ(wcomm,PETSC_ERR_ARG_WRONG, "GAMG does galerkin manually so it must not be used in PC_MG.");
    }
  }
  {
    char str[32];
    sprintf(str,"MG Level %d (%d)",0,pc_gamg->m_Nlevels-1);
    PetscLogStageRegister(str, &gamg_stages[fine_level]);
  }
  /* create coarse level and the interpolation between the levels */
  for (level=0,lidx=pc_gamg->m_Nlevels-1;
       level<fine_level;
       level++,lidx--){
    level1 = level + 1;
    ierr = PCMGSetInterpolation(pc,level1,Parr[lidx]);CHKERRQ(ierr);
    if( !PETSC_TRUE ) {
      PetscViewer viewer; char fname[32];
      sprintf(fname,"Amat_%d.m",lidx); 
      ierr = PetscViewerASCIIOpen( wcomm, fname, &viewer );  CHKERRQ(ierr);
      ierr = PetscViewerSetFormat( viewer, PETSC_VIEWER_ASCII_MATLAB);  CHKERRQ(ierr);
      ierr = MatView( Aarr[lidx], viewer ); CHKERRQ(ierr);
      ierr = PetscViewerDestroy( &viewer );
    }
    ierr = MatDestroy( &Parr[lidx] );  CHKERRQ(ierr);
    {
      KSP smoother;
      ierr = PCMGGetSmoother(pc,level,&smoother); CHKERRQ(ierr);
      ierr = KSPSetOperators( smoother, Aarr[lidx], Aarr[lidx], DIFFERENT_NONZERO_PATTERN );
      CHKERRQ(ierr);
      ierr = KSPSetNormType( smoother, KSP_NORM_NONE ); CHKERRQ(ierr);
    }
    ierr = MatDestroy( &Aarr[lidx] );  CHKERRQ(ierr);
    {
      char str[32];
      sprintf(str,"MG Level %d (%d)",level+1,lidx-1); 
      PetscLogStageRegister(str, &gamg_stages[lidx-1]);
    }
  }
  { /* fine level (no P) */
    KSP smoother;
    ierr = PCMGGetSmoother(pc,fine_level,&smoother); CHKERRQ(ierr);
    ierr = KSPSetOperators( smoother, Aarr[0], Aarr[0], DIFFERENT_NONZERO_PATTERN );
    CHKERRQ(ierr);
    ierr = KSPSetNormType( smoother, KSP_NORM_NONE ); CHKERRQ(ierr);
  }
  /* setupcalled is set to 0 so that MG is setup from scratch */
  pc->setupcalled = 0;
  ierr = PCSetUp_MG(pc);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/* -------------------------------------------------------------------------- */
/*
   PCDestroy_GAMG - Destroys the private context for the GAMG preconditioner
   that was created with PCCreate_GAMG().

   Input Parameter:
.  pc - the preconditioner context

   Application Interface Routine: PCDestroy()
*/
#undef __FUNCT__
#define __FUNCT__ "PCDestroy_GAMG"
PetscErrorCode PCDestroy_GAMG(PC pc)
{
  PetscErrorCode  ierr;
  PC_MG           *mg = (PC_MG*)pc->data;
  PC_GAMG         *pc_gamg= (PC_GAMG*)mg->innerctx;

  PetscFunctionBegin;
  ierr = PCReset_GAMG(pc);CHKERRQ(ierr);
  ierr = PetscFree(pc_gamg);CHKERRQ(ierr);
  ierr = PCDestroy_MG(pc);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "PCSetFromOptions_GAMG"
PetscErrorCode PCSetFromOptions_GAMG(PC pc)
{
  /* PetscErrorCode  ierr; */
  /* PC_MG           *mg = (PC_MG*)pc->data; */
  /* PC_GAMG         *pc_gamg = (PC_GAMG*)mg->innerctx; */
  /* MPI_Comm        comm = ((PetscObject)pc)->comm; */

  PetscFunctionBegin;
  PetscFunctionReturn(0);
}

/* -------------------------------------------------------------------------- */
/*
 PCCreate_GAMG - Creates a GAMG preconditioner context, PC_GAMG

   Input Parameter:
.  pc - the preconditioner context

   Application Interface Routine: PCCreate()

  */
 /* MC
     PCGAMG - Use algebraic multigrid preconditioning. This preconditioner requires you provide
       fine grid discretization matrix and coordinates on the fine grid.

   Options Database Key:
   Multigrid options(inherited)
+  -pc_mg_cycles <1>: 1 for V cycle, 2 for W-cycle (MGSetCycles)
.  -pc_mg_smoothup <1>: Number of post-smoothing steps (MGSetNumberSmoothUp)
.  -pc_mg_smoothdown <1>: Number of pre-smoothing steps (MGSetNumberSmoothDown)
   -pc_mg_type <multiplicative>: (one of) additive multiplicative full cascade kascade
   GAMG options:

   Level: intermediate
  Concepts: multigrid

.seealso:  PCCreate(), PCSetType(), PCType (for list of available types), PC, PCMGType,
           PCMGSetLevels(), PCMGGetLevels(), PCMGSetType(), MPSetCycles(), PCMGSetNumberSmoothDown(),
           PCMGSetNumberSmoothUp(), PCMGGetCoarseSolve(), PCMGSetResidual(), PCMGSetInterpolation(),
           PCMGSetRestriction(), PCMGGetSmoother(), PCMGGetSmootherUp(), PCMGGetSmootherDown(),
           PCMGSetCyclesOnLevel(), PCMGSetRhs(), PCMGSetX(), PCMGSetR()
M */

EXTERN_C_BEGIN
#undef __FUNCT__
#define __FUNCT__ "PCCreate_GAMG"
PetscErrorCode  PCCreate_GAMG(PC pc)
{
  PetscErrorCode  ierr;
  PC_GAMG         *pc_gamg;
  PC_MG           *mg;
  PetscClassId     cookie;

  PetscFunctionBegin;
  /* PCGAMG is an inherited class of PCMG. Initialize pc as PCMG */
  ierr = PCSetType(pc,PCMG);CHKERRQ(ierr); /* calls PCCreate_MG() and MGCreate_Private() */
  ierr = PetscObjectChangeTypeName((PetscObject)pc,PCGAMG);CHKERRQ(ierr);

  /* create a supporting struct and attach it to pc */
  ierr = PetscNewLog(pc,PC_GAMG,&pc_gamg);CHKERRQ(ierr);
  mg = (PC_MG*)pc->data;
  mg->innerctx = pc_gamg;

  pc_gamg->m_Nlevels    = -1;

  /* overwrite the pointers of PCMG by the functions of PCGAMG */
  pc->ops->setfromoptions = PCSetFromOptions_GAMG;
  pc->ops->setup          = PCSetUp_GAMG;
  pc->ops->reset          = PCReset_GAMG;
  pc->ops->destroy        = PCDestroy_GAMG;

  ierr = PetscObjectComposeFunctionDynamic( (PetscObject)pc,
					    "PCSetCoordinates_C",
					    "PCSetCoordinates_GAMG",
					    PCSetCoordinates_GAMG);CHKERRQ(ierr);

  PetscClassIdRegister("GAMG Setup",&cookie);
  PetscLogEventRegister("GAMG-createProl", cookie, &gamg_setup_stages[SET1]);
  PetscLogEventRegister("GAMG-partLevel", cookie, &gamg_setup_stages[SET2]);
  PetscLogEventRegister("GAMG-MIS Graph", cookie, &gamg_setup_stages[SET3]);
  PetscLogEventRegister("GAMG-MIS-Agg", cookie, &gamg_setup_stages[SET4]);
  PetscLogEventRegister("GAMG-growSupp", cookie, &gamg_setup_stages[SET5]);
  PetscLogEventRegister("GAMG-tri-Prol", cookie, &gamg_setup_stages[SET6]);
  PetscLogEventRegister("GAMG-find-prol", cookie, &gamg_setup_stages[FIND_V]);

  PetscFunctionReturn(0);
}
EXTERN_C_END
