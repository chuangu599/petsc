
static char help[] = "Tests I/O of vectors for different data formats (binary,HDF5) and illustrates the use of user-defined event logging\n\n";

#include <petscvec.h>
#include <petscviewerhdf5.h>

/* Note:  Most applications would not read and write a vector within
  the same program.  This example is intended only to demonstrate
  both input and output and is written for use with either 1,2,or 4 processors. */

int main(int argc,char **args)
{
  PetscErrorCode    ierr;
  PetscMPIInt       rank,size;
  PetscInt          i,m = 20,low,high,ldim,iglobal,lsize;
  PetscScalar       v;
  Vec               u;
  PetscViewer       viewer;
  PetscBool         vstage2,vstage3,mpiio_use,isbinary = PETSC_FALSE;
#if defined(PETSC_HAVE_HDF5)
  PetscBool         ishdf5 = PETSC_FALSE;
#endif
#if defined(PETSC_HAVE_ADIOS)
  PetscBool         isadios = PETSC_FALSE;
#endif
  PetscScalar const *values;
#if defined(PETSC_USE_LOG)
  PetscLogEvent  VECTOR_GENERATE,VECTOR_READ;
#endif

  ierr = PetscInitialize(&argc,&args,(char*)0,help);if (ierr) return ierr;
  mpiio_use = vstage2 = vstage3 = PETSC_FALSE;

  ierr = PetscOptionsGetBool(NULL,NULL,"-binary",&isbinary,NULL);CHKERRQ(ierr);
#if defined(PETSC_HAVE_HDF5)
  ierr = PetscOptionsGetBool(NULL,NULL,"-hdf5",&ishdf5,NULL);CHKERRQ(ierr);
#endif
#if defined(PETSC_HAVE_ADIOS)
  ierr = PetscOptionsGetBool(NULL,NULL,"-adios",&isadios,NULL);CHKERRQ(ierr);
#endif
  ierr = PetscOptionsGetBool(NULL,NULL,"-mpiio",&mpiio_use,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsGetBool(NULL,NULL,"-sizes_set",&vstage2,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsGetBool(NULL,NULL,"-type_set",&vstage3,NULL);CHKERRQ(ierr);

  ierr = MPI_Comm_rank(PETSC_COMM_WORLD,&rank);CHKERRMPI(ierr);
  ierr = MPI_Comm_size(PETSC_COMM_WORLD,&size);CHKERRMPI(ierr);
  ierr = PetscOptionsGetInt(NULL,NULL,"-m",&m,NULL);CHKERRQ(ierr);

  /* PART 1:  Generate vector, then write it in the given data format */

  ierr = PetscLogEventRegister("Generate Vector",VEC_CLASSID,&VECTOR_GENERATE);CHKERRQ(ierr);
  ierr = PetscLogEventBegin(VECTOR_GENERATE,0,0,0,0);CHKERRQ(ierr);
  /* Generate vector */
  ierr = VecCreate(PETSC_COMM_WORLD,&u);CHKERRQ(ierr);
  ierr = PetscObjectSetName((PetscObject)u, "Test_Vec");CHKERRQ(ierr);
  ierr = VecSetSizes(u,PETSC_DECIDE,m);CHKERRQ(ierr);
  ierr = VecSetFromOptions(u);CHKERRQ(ierr);
  ierr = VecGetOwnershipRange(u,&low,&high);CHKERRQ(ierr);
  ierr = VecGetLocalSize(u,&ldim);CHKERRQ(ierr);
  for (i=0; i<ldim; i++) {
    iglobal = i + low;
    v       = (PetscScalar)(i + low);
    ierr    = VecSetValues(u,1,&iglobal,&v,INSERT_VALUES);CHKERRQ(ierr);
  }
  ierr = VecAssemblyBegin(u);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(u);CHKERRQ(ierr);
  ierr = VecView(u,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);

  if (isbinary) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"writing vector in binary to vector.dat ...\n");CHKERRQ(ierr);
    ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,"vector.dat",FILE_MODE_WRITE,&viewer);CHKERRQ(ierr);
#if defined(PETSC_HAVE_HDF5)
  } else if (ishdf5) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"writing vector in hdf5 to vector.dat ...\n");CHKERRQ(ierr);
    ierr = PetscViewerHDF5Open(PETSC_COMM_WORLD,"vector.dat",FILE_MODE_WRITE,&viewer);CHKERRQ(ierr);
#endif
#if defined(PETSC_HAVE_ADIOS)
  } else if (isadios) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"writing vector in adios to vector.dat ...\n");CHKERRQ(ierr);
    ierr = PetscViewerADIOSOpen(PETSC_COMM_WORLD,"vector.dat",FILE_MODE_WRITE,&viewer);CHKERRQ(ierr);
#endif
  } else SETERRQ(PETSC_COMM_WORLD,PETSC_ERR_SUP,"No data format specified, run with one of -binary -hdf5 -adios options");
  ierr = VecView(u,viewer);CHKERRQ(ierr);
  ierr = PetscViewerDestroy(&viewer);CHKERRQ(ierr);
  ierr = VecDestroy(&u);CHKERRQ(ierr);

  ierr = PetscLogEventEnd(VECTOR_GENERATE,0,0,0,0);CHKERRQ(ierr);

  /* PART 2:  Read in vector in binary format */

  /* Read new vector in binary format */
  ierr = PetscLogEventRegister("Read Vector",VEC_CLASSID,&VECTOR_READ);CHKERRQ(ierr);
  ierr = PetscLogEventBegin(VECTOR_READ,0,0,0,0);CHKERRQ(ierr);
  if (mpiio_use) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Using MPI IO for reading the vector\n");CHKERRQ(ierr);
    ierr = PetscOptionsSetValue(NULL,"-viewer_binary_mpiio","");CHKERRQ(ierr);
  }
  if (isbinary) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"reading vector in binary from vector.dat ...\n");CHKERRQ(ierr);
    ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,"vector.dat",FILE_MODE_READ,&viewer);CHKERRQ(ierr);
    ierr = PetscViewerBinarySetFlowControl(viewer,2);CHKERRQ(ierr);
#if defined(PETSC_HAVE_HDF5)
  } else if (ishdf5) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"reading vector in hdf5 from vector.dat ...\n");CHKERRQ(ierr);
    ierr = PetscViewerHDF5Open(PETSC_COMM_WORLD,"vector.dat",FILE_MODE_READ,&viewer);CHKERRQ(ierr);
#endif
#if defined(PETSC_HAVE_ADIOS)
  } else if (isadios) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"reading vector in adios from vector.dat ...\n");CHKERRQ(ierr);
    ierr = PetscViewerADIOSOpen(PETSC_COMM_WORLD,"vector.dat",FILE_MODE_READ,&viewer);CHKERRQ(ierr);
#endif
  }
  ierr = VecCreate(PETSC_COMM_WORLD,&u);CHKERRQ(ierr);
  ierr = PetscObjectSetName((PetscObject) u,"Test_Vec");CHKERRQ(ierr);

  if (vstage2) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Setting vector sizes...\n");CHKERRQ(ierr);
    if (size > 1) {
      if (rank == 0) {
        lsize = m/size + size;
        ierr  = VecSetSizes(u,lsize,m);CHKERRQ(ierr);
      } else if (rank == size-1) {
        lsize = m/size - size;
        ierr  = VecSetSizes(u,lsize,m);CHKERRQ(ierr);
      } else {
        lsize = m/size;
        ierr  = VecSetSizes(u,lsize,m);CHKERRQ(ierr);
      }
    } else {
      ierr = VecSetSizes(u,m,m);CHKERRQ(ierr);
    }
  }

  if (vstage3) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Setting vector type...\n");CHKERRQ(ierr);
    ierr = VecSetType(u, VECMPI);CHKERRQ(ierr);
  }
  ierr = VecLoad(u,viewer);CHKERRQ(ierr);
  ierr = PetscViewerDestroy(&viewer);CHKERRQ(ierr);
  ierr = PetscLogEventEnd(VECTOR_READ,0,0,0,0);CHKERRQ(ierr);
  ierr = VecView(u,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);
  ierr = VecGetArrayRead(u,&values);CHKERRQ(ierr);
  ierr = VecGetLocalSize(u,&ldim);CHKERRQ(ierr);
  ierr = VecGetOwnershipRange(u,&low,NULL);CHKERRQ(ierr);
  for (i=0; i<ldim; i++) {
    PetscCheckFalse(values[i] != (PetscScalar)(i + low),PETSC_COMM_WORLD,PETSC_ERR_SUP,"Data check failed!");
  }
  ierr = VecRestoreArrayRead(u,&values);CHKERRQ(ierr);

  /* Free data structures */
  ierr = VecDestroy(&u);CHKERRQ(ierr);
  ierr = PetscFinalize();
  return ierr;
}

/*TEST

     test:
       nsize: 2
       args: -binary

     test:
       suffix: 2
       nsize: 3
       args: -binary

     test:
       suffix: 3
       nsize: 5
       args: -binary

     test:
       suffix: 4
       requires: hdf5
       nsize: 2
       args: -hdf5

     test:
       suffix: 5
       nsize: 4
       args: -binary -sizes_set

     test:
       suffix: 6
       requires: hdf5
       nsize: 4
       args: -hdf5 -sizes_set

TEST*/
