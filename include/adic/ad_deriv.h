/************************** DISCLAIMER ********************************/
/*                                                                    */
/*   This file was generated on 05/07/01 08:45:14 by the version of   */
/*   ADIC compiled on  08/07/00 18:06:31                              */
/*                                                                    */
/*   ADIC was prepared as an account of work sponsored by an          */
/*   agency of the United States Government and the University of     */
/*   Chicago.  NEITHER THE AUTHOR(S), THE UNITED STATES GOVERNMENT    */
/*   NOR ANY AGENCY THEREOF, NOR THE UNIVERSITY OF CHICAGO, INCLUDING */
/*   ANY OF THEIR EMPLOYEES OR OFFICERS, MAKES ANY WARRANTY, EXPRESS  */
/*   OR IMPLIED, OR ASSUMES ANY LEGAL LIABILITY OR RESPONSIBILITY FOR */
/*   THE ACCURACY, COMPLETENESS, OR USEFULNESS OF ANY INFORMATION OR  */
/*   PROCESS DISCLOSED, OR REPRESENTS THAT ITS USE WOULD NOT INFRINGE */
/*   PRIVATELY OWNED RIGHTS.                                          */
/*                                                                    */
/**********************************************************************/
#if !defined(PETSC_AD_DERIV_H)
#define PETSC_AD_DERIV_H

typedef double InactiveDouble;
typedef float InactiveFloat;

#if defined(__cplusplus)
extern "C" {
#endif

#if !defined(ad_GRAD_PTR) 
#define ad_GRAD_PTR 0
#endif

#if !defined(ad_GRAD_MAX) 
#define ad_GRAD_MAX 54
#endif

#define AD_INIT_MAP()
#define AD_CLEANUP_MAP()
#define AD_GET_DERIV_OBJ(x) ((void*)(&x.value+1))
#define AD_FREE_DERIV_OBJ(x)
typedef struct {
	double value;
	double  grad[ad_GRAD_MAX];
} DERIV_TYPE;

#define DERIV_val(a) ((a).value)

#define DERIV_grad(a) ((a).grad)

/* _FLOAT_INITIALIZER_ is currently incorrect */
#define _FLOAT_INITIALIZER_(x) { x, 0.0 }

void ad_AD_Init();
void ad_AD_Final();
#include "ad_grad.h"

#define nullFunc(x) 0

#if defined(__cplusplus)
}
#endif

#endif

