/* $Id: ispai.c,v 1.22 2001/02/12 19:21:54 bsmith Exp bsmith $*/

/* 
   3/99 Modified by Stephen Barnard to support SPAI version 3.0 
*/

/*
      Provides an interface to the SPAI Sparse Approximate Inverse Preconditioner
   Code written by Stephen Barnard.

      Note: there is some BAD memory bleeding below!

      This code needs work

   1) get rid of all memory bleeding
   2) fix PETSc/interface so that it gets if the matrix is symmetric from the matrix
      rather than having the sp flag for PC_SPAI

*/

#include "src/sles/pc/pcimpl.h"        /*I "petscpc.h" I*/

/*
    These are the SPAI include files
*/
EXTERN_C_BEGIN
#include "spai.h"
#include "matrix.h"
#include "read_mm_matrix.h"
EXTERN_C_END

EXTERN int ConvertMatToMatrix(Mat,Mat,matrix**);
EXTERN int ConvertMatrixToMat(matrix *,Mat *);
EXTERN int MM_to_PETSC(char *,char *,char *);

typedef struct {

  matrix *B;              /* matrix in SPAI format */
  matrix *BT;             /* transpose of matrix in SPAI format */
  matrix *M;              /* the approximate inverse in SPAI format */

  Mat    PM;              /* the approximate inverse PETSc format */

  double epsilon;         /* tolerance */
  int    nbsteps;         /* max number of "improvement" steps per line */
  int    max;             /* max dimensions of is_I, q, etc. */
  int    maxnew;          /* max number of new entries per step */
  int    block_size;      /* constant block size */
  int    cache_size;      /* one of (1,2,3,4,5,6) indicting size of cache */
  int    verbose;         /* SPAI prints timing and statistics */

  int    sp;              /* symmetric nonzero pattern */

} PC_SPAI;

/**********************************************************************/

#undef __FUNC__  
#define __FUNC__ "PCSetUp_SPAI"
static int PCSetUp_SPAI(PC pc)
{
  PC_SPAI *ispai = (PC_SPAI*)pc->data;
  int      ierr;
  Mat      AT;
  Mat      PBT;
  MPI_Comm comm;

  PetscFunctionBegin;
  ierr = PetscObjectGetComm((PetscObject)pc->pmat,&comm);CHKERRQ(ierr);

  init_SPAI();

  if (ispai->sp) {
    ierr = ConvertMatToMatrix(pc->pmat,pc->pmat,&ispai->B);CHKERRQ(ierr);
  } else {
    /* Use the transpose to get the column nonzero structure. */
    ierr = MatTranspose(pc->pmat,&AT);CHKERRQ(ierr);
    ierr = ConvertMatToMatrix(pc->pmat,AT,&ispai->B);CHKERRQ(ierr);
    ierr = MatDestroy(AT);CHKERRQ(ierr);
  }

  /* Destroy the transpose */
  /* Don't know how to do it. PETSc developers? */
    
  /* construct SPAI preconditioner */
  /* FILE *messages */     /* file for warning messages */
  /* double epsilon */     /* tolerance */
  /* int nbsteps */        /* max number of "improvement" steps per line */
  /* int max */            /* max dimensions of is_I, q, etc. */
  /* int maxnew */         /* max number of new entries per step */
  /* int block_size */     /* block_size == 1 specifies scalar elments
                              block_size == n specifies nxn constant-block elements
                              block_size == 0 specifies variable-block elements */
  /* int cache_size */     /* one of (1,2,3,4,5,6) indicting size of cache */
                           /* cache_size == 0 indicates no caching */
  /* int    verbose    */  /* verbose == 0 specifies that SPAI is silent
                              verbose == 1 prints timing and matrix statistics */

  ispai->M = bspai(ispai->B,
		   stdout,
		   ispai->epsilon,
		   ispai->nbsteps,
		   ispai->max,
		   ispai->maxnew,
		   ispai->block_size,
		   ispai->cache_size,
		   ispai->verbose);

  if (!ispai->M) SETERRQ(1,"Unable to create SPAI preconditioner");

  ierr = ConvertMatrixToMat(ispai->M,&ispai->PM);CHKERRQ(ierr);

  /* free the SPAI matrices */
  sp_free_matrix(ispai->B);
  sp_free_matrix(ispai->M);

  PetscFunctionReturn(0);
}

/**********************************************************************/

#undef __FUNC__  
#define __FUNC__ "PCApply_SPAI"
static int PCApply_SPAI(PC pc,Vec x,Vec y)
{
  PC_SPAI *ispai = (PC_SPAI*)pc->data;
  int      ierr;

  PetscFunctionBegin;
  /* Now using PETSc's multiply */
  ierr = MatMult(ispai->PM,x,y);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/**********************************************************************/

#undef __FUNC__  
#define __FUNC__ "PCDestroy_SPAI"
static int PCDestroy_SPAI(PC pc)
{
  int     ierr;
  PC_SPAI *ispai = (PC_SPAI*)pc->data;

  PetscFunctionBegin;
  ierr = MatDestroy(ispai->PM);CHKERRQ(ierr);
  ierr = PetscFree(ispai);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/**********************************************************************/

#undef __FUNC__  
#define __FUNC__ "PCView_SPAI"
static int PCView_SPAI(PC pc,PetscViewer viewer)
{
  PC_SPAI    *ispai = (PC_SPAI*)pc->data;
  int        ierr;
  PetscTruth isascii;

  PetscFunctionBegin;
  ierr = PetscTypeCompare((PetscObject)viewer,PETSC_VIEWER_ASCII,&isascii);CHKERRQ(ierr);
  if (isascii) {  
    ierr = PetscViewerASCIIPrintf(viewer,"    SPAI preconditioner\n");CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,"    epsilon %g\n",   ispai->epsilon);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,"    nbsteps %d\n",   ispai->nbsteps);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,"    max %d\n",       ispai->max);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,"    maxnew %d\n",    ispai->maxnew);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,"    block_size %d\n",ispai->block_size);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,"    cache_size %d\n",ispai->cache_size);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,"    verbose %d\n",   ispai->verbose);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,"    sp %d\n",        ispai->sp);CHKERRQ(ierr);

  }
  PetscFunctionReturn(0);
}

EXTERN_C_BEGIN
#undef __FUNC__  
#define __FUNC__ "PCSPAISetEpsilon_SPAI"
int PCSPAISetEpsilon_SPAI(PC pc,double epsilon)
{
  PC_SPAI    *ispai = (PC_SPAI*)pc->data;
  PetscFunctionBegin;
  ispai->epsilon = epsilon;
  PetscFunctionReturn(0);
}
EXTERN_C_END
    
/**********************************************************************/

EXTERN_C_BEGIN
#undef __FUNC__  
#define __FUNC__ "PCSPAISetNBSteps_SPAI"
int PCSPAISetNBSteps_SPAI(PC pc,int nbsteps)
{
  PC_SPAI    *ispai = (PC_SPAI*)pc->data;
  PetscFunctionBegin;
  ispai->nbsteps = nbsteps;
  PetscFunctionReturn(0);
}
EXTERN_C_END

/**********************************************************************/

/* added 1/7/99 g.h. */
EXTERN_C_BEGIN
#undef __FUNC__  
#define __FUNC__ "PCSPAISetMax_SPAI"
int PCSPAISetMax_SPAI(PC pc,int max)
{
  PC_SPAI    *ispai = (PC_SPAI*)pc->data;
  PetscFunctionBegin;
  ispai->max = max;
  PetscFunctionReturn(0);
}
EXTERN_C_END

/**********************************************************************/

EXTERN_C_BEGIN
#undef __FUNC__  
#define __FUNC__ "PCSPAISetMaxNew_SPAI"
int PCSPAISetMaxNew_SPAI(PC pc,int maxnew)
{
  PC_SPAI    *ispai = (PC_SPAI*)pc->data;
  PetscFunctionBegin;
  ispai->maxnew = maxnew;
  PetscFunctionReturn(0);
}
EXTERN_C_END

/**********************************************************************/

EXTERN_C_BEGIN
#undef __FUNC__  
#define __FUNC__ "PCSPAISetBlockSize_SPAI"
int PCSPAISetBlockSize_SPAI(PC pc,int block_size)
{
  PC_SPAI    *ispai = (PC_SPAI*)pc->data;
  PetscFunctionBegin;
  ispai->block_size = block_size;
  PetscFunctionReturn(0);
}
EXTERN_C_END

/**********************************************************************/

EXTERN_C_BEGIN
#undef __FUNC__  
#define __FUNC__ "PCSPAISetCacheSize_SPAI"
int PCSPAISetCacheSize_SPAI(PC pc,int cache_size)
{
  PC_SPAI    *ispai = (PC_SPAI*)pc->data;
  PetscFunctionBegin;
  ispai->cache_size = cache_size;
  PetscFunctionReturn(0);
}
EXTERN_C_END

/**********************************************************************/

EXTERN_C_BEGIN
#undef __FUNC__  
#define __FUNC__ "PCSPAISetVerbose_SPAI"
int PCSPAISetVerbose_SPAI(PC pc,int verbose)
{
  PC_SPAI    *ispai = (PC_SPAI*)pc->data;
  PetscFunctionBegin;
  ispai->verbose = verbose;
  PetscFunctionReturn(0);
}
EXTERN_C_END

/**********************************************************************/

EXTERN_C_BEGIN
#undef __FUNC__  
#define __FUNC__ "PCSPAISetSp_SPAI"
int PCSPAISetSp_SPAI(PC pc,int sp)
{
  PC_SPAI    *ispai = (PC_SPAI*)pc->data;
  PetscFunctionBegin;
  ispai->sp = sp;
  PetscFunctionReturn(0);
}
EXTERN_C_END

/* -------------------------------------------------------------------*/

#undef __FUNC__  
#define __FUNC__ "PCSPAISetEpsilon"
int PCSPAISetEpsilon(PC pc,double epsilon)
{
  int ierr,(*f)(PC,double);
  PetscFunctionBegin;
  ierr = PetscObjectQueryFunction((PetscObject)pc,"PCSPAISetEpsilon_C",(void **)&f);CHKERRQ(ierr);
  if (f) {
    ierr = (*f)(pc,epsilon);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}
    
/**********************************************************************/

#undef __FUNC__  
#define __FUNC__ "PCSPAISetNBSteps"
int PCSPAISetNBSteps(PC pc,int nbsteps)
{
  int ierr,(*f)(PC,int);
  PetscFunctionBegin;
  ierr = PetscObjectQueryFunction((PetscObject)pc,"PCSPAISetNBSteps_C",(void **)&f);CHKERRQ(ierr);
  if (f) {
    ierr = (*f)(pc,nbsteps);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

/**********************************************************************/

/* added 1/7/99 g.h. */
#undef __FUNC__  
#define __FUNC__ "PCSPAISetMax"
int PCSPAISetMax(PC pc,int max)
{
  int ierr,(*f)(PC,int);
  PetscFunctionBegin;
  ierr = PetscObjectQueryFunction((PetscObject)pc,"PCSPAISetMax_C",(void **)&f);CHKERRQ(ierr);
  if (f) {
    ierr = (*f)(pc,max);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

/**********************************************************************/

#undef __FUNC__  
#define __FUNC__ "PCSPAISetMaxNew"
int PCSPAISetMaxNew(PC pc,int maxnew)
{
  int ierr,(*f)(PC,int);
  PetscFunctionBegin;
  ierr = PetscObjectQueryFunction((PetscObject)pc,"PCSPAISetMaxNew_C",(void **)&f);CHKERRQ(ierr);
  if (f) {
    ierr = (*f)(pc,maxnew);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

/**********************************************************************/

#undef __FUNC__  
#define __FUNC__ "PCSPAISetBlockSize"
int PCSPAISetBlockSize(PC pc,int block_size)
{
  int ierr,(*f)(PC,int);
  PetscFunctionBegin;
  ierr = PetscObjectQueryFunction((PetscObject)pc,"PCSPAISetBlockSize_C",(void **)&f);CHKERRQ(ierr);
  if (f) {
    ierr = (*f)(pc,block_size);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

/**********************************************************************/

#undef __FUNC__  
#define __FUNC__ "PCSPAISetCacheSize"
int PCSPAISetCacheSize(PC pc,int cache_size)
{
  int ierr,(*f)(PC,int);
  PetscFunctionBegin;
  ierr = PetscObjectQueryFunction((PetscObject)pc,"PCSPAISetCacheSize_C",(void **)&f);CHKERRQ(ierr);
  if (f) {
    ierr = (*f)(pc,cache_size);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

/**********************************************************************/

#undef __FUNC__  
#define __FUNC__ "PCSPAISetVerbose"
int PCSPAISetVerbose(PC pc,int verbose)
{
  int ierr,(*f)(PC,int);
  PetscFunctionBegin;
  ierr = PetscObjectQueryFunction((PetscObject)pc,"PCSPAISetVerbose_C",(void **)&f);CHKERRQ(ierr);
  if (f) {
    ierr = (*f)(pc,verbose);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

/**********************************************************************/

#undef __FUNC__  
#define __FUNC__ "PCSPAISetSp"
int PCSPAISetSp(PC pc,int sp)
{
  int ierr,(*f)(PC,int);
  PetscFunctionBegin;
  ierr = PetscObjectQueryFunction((PetscObject)pc,"PCSPAISetSp_C",(void **)&f);CHKERRQ(ierr);
  if (f) {
    ierr = (*f)(pc,sp);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

/**********************************************************************/

/**********************************************************************/

#undef __FUNC__  
#define __FUNC__ "PCSetFromOptions_SPAI"
static int PCSetFromOptions_SPAI(PC pc)
{
  PC_SPAI    *ispai = (PC_SPAI*)pc->data;
  int        ierr,nbsteps,max,maxnew,block_size,cache_size,verbose,sp;
  double     epsilon;
  PetscTruth flg;

  PetscFunctionBegin;
  ierr = PetscOptionsHead("SPAI options");CHKERRQ(ierr);
    ierr = PetscOptionsDouble("-pc_spai_epsilon","","PCSPAISetEpsilon",ispai->epsilon,&epsilon,&flg);CHKERRQ(ierr);
    if (flg) {
      ierr = PCSPAISetEpsilon(pc,epsilon);CHKERRQ(ierr);
    }
    ierr = PetscOptionsInt("-pc_spai_nbsteps","","PCSPAISetNBSteps",ispai->nbsteps,&nbsteps,&flg);CHKERRQ(ierr);
    if (flg) {
      ierr = PCSPAISetNBSteps(pc,nbsteps);CHKERRQ(ierr);
    }
    /* added 1/7/99 g.h. */
    ierr = PetscOptionsInt("-pc_spai_max","","PCSPAISetMax",ispai->max,&max,&flg);CHKERRQ(ierr);
    if (flg) {
      ierr = PCSPAISetMax(pc,max);CHKERRQ(ierr);
    }
    ierr = PetscOptionsInt("-pc_spai_maxnew","","PCSPAISetMaxNew",ispai->maxnew,&maxnew,&flg);CHKERRQ(ierr);
    if (flg) {
      ierr = PCSPAISetMaxNew(pc,maxnew);CHKERRQ(ierr);
    } 
    ierr = PetscOptionsInt("-pc_spai_block_size","","PCSPAISetBlockSize",ispai->block_size,&block_size,&flg);CHKERRQ(ierr);
    if (flg) {
      ierr = PCSPAISetBlockSize(pc,block_size);CHKERRQ(ierr);
    }
    ierr = PetscOptionsInt("-pc_spai_cache_size","","PCSPAISetCacheSize",ispai->cache_size,&cache_size,&flg);CHKERRQ(ierr);
    if (flg) {
      ierr = PCSPAISetCacheSize(pc,cache_size);CHKERRQ(ierr);
    }
    ierr = PetscOptionsInt("-pc_spai_verbose","","PCSPAISetVerbose",ispai->verbose,&verbose,&flg);CHKERRQ(ierr);
    if (flg) {
      ierr = PCSPAISetVerbose(pc,verbose);CHKERRQ(ierr);
    }
    ierr = PetscOptionsInt("-pc_spai_sp","","PCSPAISetSp",ispai->sp,&sp,&flg);CHKERRQ(ierr);
    if (flg) {
      ierr = PCSPAISetSp(pc,sp);CHKERRQ(ierr);
    }
  ierr = PetscOptionsTail();CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/**********************************************************************/

EXTERN_C_BEGIN
/*
   PCCreate_SPAI - Creates the preconditioner context for the SPAI 
                   preconditioner written by Stephen Barnard.

*/
#undef __FUNC__  
#define __FUNC__ "PCCreate_SPAI"
int PCCreate_SPAI(PC pc)
{
  PC_SPAI *ispai;
  int     ierr;

  PetscFunctionBegin;
  ierr               = PetscNew(PC_SPAI,&ispai);CHKERRQ(ierr);
  pc->data           = (void*)ispai;

  pc->ops->destroy         = PCDestroy_SPAI;
  pc->ops->apply           = PCApply_SPAI;
  pc->ops->applyrichardson = 0;
  pc->ops->setup           = PCSetUp_SPAI;
  pc->ops->view            = PCView_SPAI;
  pc->ops->setfromoptions  = PCSetFromOptions_SPAI;

  pc->name          = 0;
  ispai->epsilon    = .4;  
  ispai->nbsteps    = 5;        
  ispai->max        = 5000;            
  ispai->maxnew     = 5;         
  ispai->block_size = 1;     
  ispai->cache_size = 5;     
  ispai->verbose    = 0;     

  ispai->sp         = 1;     

  ierr = PetscObjectComposeFunctionDynamic((PetscObject)pc,"PCSPAISetEpsilon_C",
                    "PCSPAISetEpsilon_SPAI",
                     PCSPAISetEpsilon_SPAI);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunctionDynamic((PetscObject)pc,"PCSPAISetNBSteps_C",
                    "PCSPAISetNBSteps_SPAI",
                     PCSPAISetNBSteps_SPAI);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunctionDynamic((PetscObject)pc,"PCSPAISetMax_C",
                    "PCSPAISetMax_SPAI",
                     PCSPAISetMax_SPAI);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunctionDynamic((PetscObject)pc,"PCSPAISetMaxNew_CC",
                    "PCSPAISetMaxNew_SPAI",
                     PCSPAISetMaxNew_SPAI);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunctionDynamic((PetscObject)pc,"PCSPAISetBlockSize_C",
                    "PCSPAISetBlockSize_SPAI",
                     PCSPAISetBlockSize_SPAI);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunctionDynamic((PetscObject)pc,"PCSPAISetCacheSize_C",
                    "PCSPAISetCacheSize_SPAI",
                     PCSPAISetCacheSize_SPAI);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunctionDynamic((PetscObject)pc,"PCSPAISetVerbose_C",
                    "PCSPAISetVerbose_SPAI",
                     PCSPAISetVerbose_SPAI);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunctionDynamic((PetscObject)pc,"PCSPAISetSp_C",
                    "PCSPAISetSp_SPAI",
                     PCSPAISetSp_SPAI);CHKERRQ(ierr);

  PetscFunctionReturn(0);
}
EXTERN_C_END

/**********************************************************************/

/*
   Converts from a PETSc matrix to an SPAI matrix 
*/
#undef __FUNC__  
#define __FUNC__ "ConvertMatToMatrix"
int ConvertMatToMatrix(Mat A,Mat AT,matrix **B)
{
  matrix   *M;
  int      i,j,col;
  int      row_indx;
  int      len,pe,local_indx,start_indx;
  int      *mapping;
  int      ierr,*cols;
  double   *vals;
  int      *num_ptr,n,mnl,nnl,rank,size,nz,rstart,rend;
  MPI_Comm comm;
  struct compressed_lines *rows;

  PetscFunctionBegin;
  ierr = PetscObjectGetComm((PetscObject)A,&comm);CHKERRQ(ierr);
 
  ierr = MPI_Comm_size(comm,&size);CHKERRQ(ierr);
  ierr = MPI_Comm_rank(comm,&rank);CHKERRQ(ierr);
  ierr = MatGetSize(A,&n,&n);CHKERRQ(ierr);
  ierr = MatGetLocalSize(A,&mnl,&nnl);CHKERRQ(ierr);

  ierr = MPI_Barrier(PETSC_COMM_WORLD);CHKERRQ(ierr);

  M = new_matrix((void *)comm);
							      
  M->n = n;
  M->bs = 1;
  M->max_block_size = 1;

  M->mnls          = malloc(sizeof(int)*size);
  M->start_indices = malloc(sizeof(int)*size);
  M->pe            = malloc(sizeof(int)*n);
  M->block_sizes   = malloc(sizeof(int)*n);
  for (i=0; i<n; i++) M->block_sizes[i] = 1;

  ierr = MPI_Allgather(&mnl,1,MPI_INT,M->mnls,1,MPI_INT,PETSC_COMM_WORLD);CHKERRQ(ierr);

  M->start_indices[0] = 0;
  for (i=1; i<size; i++) {
    M->start_indices[i] = M->start_indices[i-1] + M->mnls[i-1];
  }

  M->mnl = M->mnls[M->myid];
  M->my_start_index = M->start_indices[M->myid];

  for (i=0; i<size; i++) {
    start_indx = M->start_indices[i];
    for (j=0; j<M->mnls[i]; j++)
      M->pe[start_indx+j] = i;
  }

  if (AT) {
    M->lines = new_compressed_lines(M->mnls[rank],1);CHKERRQ(ierr);
  } else {
    M->lines = new_compressed_lines(M->mnls[rank],0);CHKERRQ(ierr);
  }

  rows     = M->lines;

  /* Determine the mapping from global indices to pointers */
  ierr       = PetscMalloc(M->n*sizeof(int),&mapping);CHKERRQ(ierr);
  pe         = 0;
  local_indx = 0;
  for (i=0; i<M->n; i++) {
    if (local_indx >= M->mnls[pe]) {
      pe++;
      local_indx = 0;
    }
    mapping[i] = local_indx + M->start_indices[pe];
    local_indx++;
  }


  ierr = PetscMalloc(mnl*sizeof(int),&num_ptr);CHKERRQ(ierr);

  /*********************************************************/
  /************** Set up the row structure *****************/
  /*********************************************************/

  /* count number of nonzeros in every row */
  ierr = MatGetOwnershipRange(A,&rstart,&rend);CHKERRQ(ierr);
  for (i=rstart; i<rend; i++) {
    ierr = MatGetRow(A,i,&num_ptr[i-rstart],PETSC_NULL,PETSC_NULL);CHKERRQ(ierr);
    ierr = MatRestoreRow(A,i,&num_ptr[i-rstart],PETSC_NULL,PETSC_NULL);CHKERRQ(ierr);
  }

  /* allocate buffers */
  len = 0;
  for (i=0; i<n; i++) {
    if (len < num_ptr[i]) len = num_ptr[i];
  }

  for (i=rstart; i<rend; i++) {
    row_indx             = i-rstart;
    len                  = num_ptr[row_indx];
    rows->ptrs[row_indx] = malloc(len*sizeof(int));
    rows->A[row_indx]    = malloc(len*sizeof(double));
  }

  /* copy the matrix */
  for (i=rstart; i<rend; i++) {
    row_indx = i - rstart;
    ierr     = MatGetRow(A,i,&nz,&cols,&vals);CHKERRQ(ierr);
    for (j=0; j<nz; j++) {
      col = cols[j];
      len = rows->len[row_indx]++;
      rows->ptrs[row_indx][len] = mapping[col];
      rows->A[row_indx][len]    = vals[j];
    }
    rows->slen[row_indx] = rows->len[row_indx];
    ierr = MatRestoreRow(A,i,&nz,&cols,&vals);CHKERRQ(ierr);
  }


  /************************************************************/
  /************** Set up the column structure *****************/
  /*********************************************************/

  if (AT) {

    /* count number of nonzeros in every column */
    for (i=rstart; i<rend; i++) {
      ierr = MatGetRow(AT,i,&num_ptr[i-rstart],PETSC_NULL,PETSC_NULL);CHKERRQ(ierr);
      ierr = MatRestoreRow(AT,i,&num_ptr[i-rstart],PETSC_NULL,PETSC_NULL);CHKERRQ(ierr);
    }

    /* allocate buffers */
    len = 0;
    for (i=0; i<n; i++) {
      if (len < num_ptr[i]) len = num_ptr[i];
    }

    for (i=rstart; i<rend; i++) {
      row_indx = i-rstart;
      len      = num_ptr[row_indx];
      rows->rptrs[row_indx]     = malloc(len*sizeof(int));
    }

    /* copy the matrix (i.e., the structure) */
    for (i=rstart; i<rend; i++) {
      row_indx = i - rstart;
      ierr     = MatGetRow(AT,i,&nz,&cols,&vals);CHKERRQ(ierr);
      for (j=0; j<nz; j++) {
	col = cols[j];
	len = rows->rlen[row_indx]++;
	rows->rptrs[row_indx][len] = mapping[col];
      }
      ierr     = MatRestoreRow(AT,i,&nz,&cols,&vals);CHKERRQ(ierr);
    }
  }

  ierr = PetscFree(num_ptr);CHKERRQ(ierr);
  ierr = PetscFree(mapping);CHKERRQ(ierr);

  order_pointers(M);
  M->maxnz = calc_maxnz(M);

  *B = M;

  PetscFunctionReturn(0);
}

/**********************************************************************/

/*
   Converts from an SPAI matrix B  to a PETSc matrix PB.
   This assumes that the the SPAI matrix B is stored in
   COMPRESSED-ROW format.
*/
#undef __FUNC__  
#define __FUNC__ "ConvertMatrixToMat"
int ConvertMatrixToMat(matrix *B,Mat *PB)
{
  int    size,rank;
  int    ierr;
  int    m,n,M,N;
  int    d_nz,o_nz;
  int    *d_nnz,*o_nnz;
  int    i,k,global_row,global_col,first_diag_col,last_diag_col;
  Scalar val;

  PetscFunctionBegin;
  ierr = MPI_Comm_size(PETSC_COMM_WORLD,&size);CHKERRQ(ierr);
  ierr = MPI_Comm_rank(PETSC_COMM_WORLD,&rank);CHKERRQ(ierr);
  
  m = n = B->mnls[rank];
  d_nz = o_nz = 0;

  /* Determine preallocation for MatCreateMPIAIJ */
  ierr = PetscMalloc(m*sizeof(int),&d_nnz);CHKERRQ(ierr);
  ierr = PetscMalloc(m*sizeof(int),&o_nnz);CHKERRQ(ierr);
  for (i=0; i<m; i++) d_nnz[i] = o_nnz[i] = 0;
  first_diag_col = B->start_indices[rank];
  last_diag_col = first_diag_col + B->mnls[rank];
  for (i=0; i<B->mnls[rank]; i++) {
    for (k=0; k<B->lines->len[i]; k++) {
      global_col = B->lines->ptrs[i][k];
      if ((global_col >= first_diag_col) && (global_col <= last_diag_col))
	d_nnz[i]++;
      else
	o_nnz[i]++;
    }
  }

  M = N = B->n;
  ierr = MatCreateMPIAIJ(PETSC_COMM_WORLD,m,n,M,N,d_nz,d_nnz,o_nz,o_nnz,PB);CHKERRQ(ierr);

  for (i=0; i<B->mnls[rank]; i++) {
    global_row = B->start_indices[rank]+i;
    for (k=0; k<B->lines->len[i]; k++) {
      global_col = B->lines->ptrs[i][k];
      val = B->lines->A[i][k];
      ierr = MatSetValues(*PB,1,&global_row,1,&global_col,&val,ADD_VALUES);CHKERRQ(ierr);
    }
  }

  ierr = PetscFree(d_nnz);CHKERRQ(ierr);
  ierr = PetscFree(o_nnz);CHKERRQ(ierr);

  ierr = MatAssemblyBegin(*PB,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = MatAssemblyEnd(*PB,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);

  PetscFunctionReturn(0);
}

/**********************************************************************/

/*
   Converts from an SPAI vector v  to a PETSc vec Pv.
*/
#undef __FUNC__  
#define __FUNC__ "ConvertVectorToVec"
int ConvertVectorToVec(vector *v,Vec *Pv)
{
  int size,rank,ierr,m,M,i,*mnls,*start_indices,*global_indices;
  
  PetscFunctionBegin;
  ierr = MPI_Comm_size(PETSC_COMM_WORLD,&size);CHKERRQ(ierr);
  ierr = MPI_Comm_rank(PETSC_COMM_WORLD,&rank);CHKERRQ(ierr);
  
  m = v->mnl;
  M = v->n;
  
  
  ierr = VecCreateMPI(PETSC_COMM_WORLD,m,M,Pv);CHKERRQ(ierr);

  ierr = PetscMalloc(size*sizeof(int),&mnls);CHKERRQ(ierr);
  ierr = MPI_Allgather((void*)&v->mnl,1,MPI_INT,(void*)mnls,1,MPI_INT,PETSC_COMM_WORLD);CHKERRQ(ierr);
  
  ierr = PetscMalloc(size*sizeof(int),&start_indices);CHKERRQ(ierr);
  start_indices[0] = 0;
  for (i=1; i<size; i++) 
    start_indices[i] = start_indices[i-1] +mnls[i-1];
  
  ierr = PetscMalloc(v->mnl*sizeof(int),&global_indices);CHKERRQ(ierr);
  for (i=0; i<v->mnl; i++) 
    global_indices[i] = start_indices[rank] + i;

  ierr = PetscFree(mnls);CHKERRQ(ierr);
  ierr = PetscFree(start_indices);CHKERRQ(ierr);
  
  ierr = VecSetValues(*Pv,v->mnl,global_indices,v->v,INSERT_VALUES);CHKERRQ(ierr);

  ierr = PetscFree(global_indices);CHKERRQ(ierr);

  ierr = VecAssemblyBegin(*Pv);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(*Pv);CHKERRQ(ierr);
 
  PetscFunctionReturn(0);
}

/**********************************************************************/

/*

  Reads a matrix and a RHS vector in Matrix Market format and writes them
  in PETSc binary format.

  !!!! Warning!!!!! PETSc supports only serial execution of this routine.
  
  f0 <input_file> : matrix in Matrix Market format
  f1 <input_file> : vector in Matrix Market array format
  f2 <output_file> : matrix and vector in PETSc format

*/
#undef __FUNC__  
#define __FUNC__ "MM_to_PETSC"
int MM_to_PETSC(char *f0,char *f1,char *f2)
{
  Mat         A_PETSC;          /* matrix */
  Vec         b_PETSC;          /* RHS */
  PetscViewer fd;               /* viewer */
  int         ierr;
  matrix      *A_spai;
  vector      *b_spai;

  PetscFunctionBegin;
  A_spai = read_mm_matrix(f0,1,1,1,0,0,0,NULL);
  b_spai = read_rhs_for_matrix(f1,A_spai);

  ierr = ConvertMatrixToMat(A_spai,&A_PETSC);CHKERRQ(ierr);
  ierr = ConvertVectorToVec(b_spai,&b_PETSC);CHKERRQ(ierr);

  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,f1,PETSC_BINARY_CREATE,&fd);CHKERRQ(ierr);
  ierr = MatView(A_PETSC,fd);CHKERRQ(ierr);
  ierr = VecView(b_PETSC,fd);CHKERRQ(ierr);

  PetscFunctionReturn(0);
}



