#ifndef lint
static char vcid[] = "$Id: senddense.c,v 1.4 1995/03/27 22:59:15 bsmith Exp curfman $";
#endif
/* This is part of the MatlabSockettool package. Here are the routines
   to send a dense matrix to Matlab.

 
    Usage: Fortran: putmat(machine, portnumber, m, n, matrix)
           C:       putmatrix(machine, portnumber, m, n, matrix)

       char   *machine    e.g. "condor"
       int    portnumber  [  5000 < portnumber < 5010 ]
       int    m,n         number of rows and columns in matrix
       double *matrix     fortran style matrix
 
        Written by Barry Smith, bsmith@mcs.anl.gov 4/14/92
*/
#include "matlab.h"

/*@
   ViewerMatlabPutArray - Passes an array to a Matlab viewer.

  Input Paramters:
.  viewer - obtained from ViewerMatlabOpen()
.  m, n - number of rows and columns of array
.  array - the array stored in Fortran 77 style (matrix or vector data) 

   Notes:
   Most users should not call this routine, but instead should employ
   either
$     MatView(Mat matrix,Viewer viewer)
$
$              or
$
$     VecView(Vec vector,Viewer viewer)

.keywords: Viewer, Matlab, put, dense, array, vector

.seealso: ViewerMatlabOpen(), MatView(), VecView()
@*/
int ViewerMatlabPutArray(Viewer viewer,int m,int n,Scalar *array)
{
  int t = viewer->port,type = DENSEREAL,one = 1, zero = 0;
  if (write_int(t,&type,1))       SETERR(1,"writing type");
  if (write_int(t,&m,1))          SETERR(1,"writing number columns");
  if (write_int(t,&n,1))          SETERR(1,"writing number rows");
#if !defined(PETSC_COMPLEX)
  if (write_int(t,&zero,1))          SETERR(1,"writing complex");
  if (write_double(t,array,m*n)) SETERR(1,"writing dense array");
#else
  if (write_int(t,&one,1))          SETERR(1,"writing complex");
  if (write_double(t,(double*)array,2*m*n)) SETERR(1,"writing dense array");
#endif
  return 0;
}

