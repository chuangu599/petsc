
/*
       Provides the registration process for PETSc PetscDraw routines
*/
#include <petsc/private/drawimpl.h>  /*I "petscdraw.h" I*/
#include <petscviewer.h>             /*I "petscviewer.h" I*/
#if defined(PETSC_HAVE_SAWS)
#include <petscviewersaws.h>
#endif

/*
   Contains the list of registered PetscDraw routines
*/
PetscFunctionList PetscDrawList = 0;

#undef __FUNCT__
#define __FUNCT__ "PetscDrawView"
/*@C
   PetscDrawView - Prints the PetscDraw data structure.

   Collective on PetscDraw

   Input Parameters:
+  indraw - the PetscDraw context
-  viewer - visualization context

   See PetscDrawSetFromOptions() for options database keys

   Note:
   The available visualization contexts include
+     PETSC_VIEWER_STDOUT_SELF - standard output (default)
-     PETSC_VIEWER_STDOUT_WORLD - synchronized standard
         output where only the first processor opens
         the file.  All other processors send their
         data to the first processor to print.

   The user can open an alternative visualization context with
   PetscViewerASCIIOpen() - output to a specified file.

   Level: beginner

.keywords: PetscDraw, view

.seealso: PCView(), PetscViewerASCIIOpen()
@*/
PetscErrorCode  PetscDrawView(PetscDraw indraw,PetscViewer viewer)
{
  PetscErrorCode ierr;
  PetscBool      isdraw;
#if defined(PETSC_HAVE_SAWS)
  PetscBool      issaws;
#endif

  PetscFunctionBegin;
  PetscValidHeaderSpecific(indraw,PETSC_DRAW_CLASSID,1);
  if (!viewer) {
    ierr = PetscViewerASCIIGetStdout(PetscObjectComm((PetscObject)indraw),&viewer);CHKERRQ(ierr);
  }
  PetscValidHeaderSpecific(viewer,PETSC_VIEWER_CLASSID,2);
  PetscCheckSameComm(indraw,1,viewer,2);

  ierr = PetscObjectPrintClassNamePrefixType((PetscObject)indraw,viewer);CHKERRQ(ierr);
  ierr = PetscObjectTypeCompare((PetscObject)viewer,PETSCVIEWERDRAW,&isdraw);CHKERRQ(ierr);
#if defined(PETSC_HAVE_SAWS)
  ierr = PetscObjectTypeCompare((PetscObject)viewer,PETSCVIEWERSAWS,&issaws);CHKERRQ(ierr);
#endif
  if (isdraw) {
    PetscDraw draw;
    char      str[36];
    PetscReal x,y,bottom,h;

    ierr = PetscViewerDrawGetDraw(viewer,0,&draw);CHKERRQ(ierr);
    ierr = PetscDrawGetCurrentPoint(draw,&x,&y);CHKERRQ(ierr);
    ierr   = PetscStrcpy(str,"PetscDraw: ");CHKERRQ(ierr);
    ierr   = PetscStrcat(str,((PetscObject)indraw)->type_name);CHKERRQ(ierr);
    ierr   = PetscDrawStringBoxed(draw,x,y,PETSC_DRAW_RED,PETSC_DRAW_BLACK,str,NULL,&h);CHKERRQ(ierr);
    bottom = y - h;
    ierr = PetscDrawPushCurrentPoint(draw,x,bottom);CHKERRQ(ierr);
#if defined(PETSC_HAVE_SAWS)
  } else if (issaws) {
    PetscMPIInt rank;

    ierr = PetscObjectName((PetscObject)indraw);CHKERRQ(ierr);
    ierr = MPI_Comm_rank(PETSC_COMM_WORLD,&rank);CHKERRQ(ierr);
    if (!((PetscObject)indraw)->amsmem && !rank) {
      ierr = PetscObjectViewSAWs((PetscObject)indraw,viewer);CHKERRQ(ierr);
    }
#endif
  } else if (indraw->ops->view) {
    ierr = (*indraw->ops->view)(indraw,viewer);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "PetscDrawCreate"
/*@C
   PetscDrawCreate - Creates a graphics context.

   Collective on MPI_Comm

   Input Parameter:
+  comm - MPI communicator
.  display - X display when using X windows
.  title - optional title added to top of window
.  x,y - coordinates of lower left corner of window or PETSC_DECIDE
-  w, h - width and height of window or PETSC_DECIDE or PETSC_DRAW_HALF_SIZE, PETSC_DRAW_FULL_SIZE,
          or PETSC_DRAW_THIRD_SIZE or PETSC_DRAW_QUARTER_SIZE

   Output Parameter:
.  draw - location to put the PetscDraw context

   Level: beginner

   Concepts: graphics^creating context
   Concepts: drawing^creating context

.seealso: PetscDrawSetFromOptions(), PetscDrawDestroy(), PetscDrawSetType()
@*/
PetscErrorCode  PetscDrawCreate(MPI_Comm comm,const char display[],const char title[],int x,int y,int w,int h,PetscDraw *indraw)
{
  PetscDraw      draw;
  PetscErrorCode ierr;
  PetscReal      dpause = 0.0;
  PetscBool      flag;

  PetscFunctionBegin;
  ierr = PetscDrawInitializePackage();CHKERRQ(ierr);
  *indraw = 0;
  ierr = PetscHeaderCreate(draw,PETSC_DRAW_CLASSID,"Draw","Graphics","Draw",comm,PetscDrawDestroy,PetscDrawView);CHKERRQ(ierr);

  draw->data    = NULL;
  ierr          = PetscStrallocpy(display,&draw->display);CHKERRQ(ierr);
  ierr          = PetscStrallocpy(title,&draw->title);CHKERRQ(ierr);
  draw->x       = x;
  draw->y       = y;
  draw->w       = w;
  draw->h       = h;
  draw->pause   = 0.0;
  draw->coor_xl = 0.0;
  draw->coor_xr = 1.0;
  draw->coor_yl = 0.0;
  draw->coor_yr = 1.0;
  draw->port_xl = 0.0;
  draw->port_xr = 1.0;
  draw->port_yl = 0.0;
  draw->port_yr = 1.0;
  draw->popup   = NULL;

  ierr = PetscOptionsGetReal(NULL,NULL,"-draw_pause",&dpause,&flag);CHKERRQ(ierr);
  if (flag) draw->pause = dpause;
  draw->savefilename  = NULL;
  draw->savefilecount = 0;
  draw->savefilemovie = PETSC_FALSE;

  ierr = PetscDrawSetCurrentPoint(draw,.5,.9);CHKERRQ(ierr);

  draw->boundbox_xl  = .5;
  draw->boundbox_xr  = .5;
  draw->boundbox_yl  = .9;
  draw->boundbox_yr  = .9;

  *indraw = draw;
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "PetscDrawSetType"
/*@C
   PetscDrawSetType - Builds graphics object for a particular implementation

   Collective on PetscDraw

   Input Parameter:
+  draw      - the graphics context
-  type      - for example, PETSC_DRAW_X

   Options Database Command:
.  -draw_type  <type> - Sets the type; use -help for a list of available methods (for instance, x)

   See PetscDrawSetFromOptions for additional options database keys

   Level: intermediate

   Notes:
   See "petsc/include/petscdraw.h" for available methods (for instance,
   PETSC_DRAW_X)

   Concepts: drawing^X windows
   Concepts: X windows^graphics
   Concepts: drawing^Microsoft Windows

.seealso: PetscDrawSetFromOptions(), PetscDrawCreate(), PetscDrawDestroy()
@*/
PetscErrorCode  PetscDrawSetType(PetscDraw draw,PetscDrawType type)
{
  PetscErrorCode ierr,(*r)(PetscDraw);
  PetscBool      match;
  PetscBool      flg=PETSC_FALSE;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(draw,PETSC_DRAW_CLASSID,1);
  PetscValidCharPointer(type,2);

  ierr = PetscObjectTypeCompare((PetscObject)draw,type,&match);CHKERRQ(ierr);
  if (match) PetscFunctionReturn(0);

  /*  User requests no graphics */
  ierr = PetscOptionsHasName(((PetscObject)draw)->options,NULL,"-nox",&flg);CHKERRQ(ierr);

  /*
     This is not ideal, but it allows codes to continue to run if X graphics
   was requested but is not installed on this machine. Mostly this is for
   testing.
   */
#if !defined(PETSC_HAVE_X)
  if (!flg) {
    ierr = PetscStrcmp(type,PETSC_DRAW_X,&match);CHKERRQ(ierr);
    if (match) {
      PetscBool dontwarn = PETSC_TRUE;
      flg  = PETSC_TRUE;
      ierr = PetscOptionsHasName(NULL,NULL,"-nox_warning",&dontwarn);CHKERRQ(ierr);
      if (!dontwarn) (*PetscErrorPrintf)("PETSc installed without X windows on this machine\nproceeding without graphics\n");
    }
  }
#endif
  if (flg) type = PETSC_DRAW_NULL;

  ierr =  PetscFunctionListFind(PetscDrawList,type,&r);CHKERRQ(ierr);
  if (!r) SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_ARG_UNKNOWN_TYPE,"Unknown PetscDraw type given: %s",type);
  if (draw->ops->destroy) {ierr = (*draw->ops->destroy)(draw);CHKERRQ(ierr);}
  ierr = PetscMemzero(draw->ops,sizeof(struct _PetscDrawOps));CHKERRQ(ierr);
  ierr = PetscObjectChangeTypeName((PetscObject)draw,type);CHKERRQ(ierr);
  ierr = (*r)(draw);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "PetscDrawGetType"
/*@C
   PetscDrawGetType - Gets the PetscDraw type as a string from the PetscDraw object.

   Not Collective

   Input Parameter:
.  draw - Krylov context

   Output Parameters:
.  name - name of PetscDraw method

   Level: advanced

@*/
PetscErrorCode  PetscDrawGetType(PetscDraw draw,PetscDrawType *type)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(draw,PETSC_DRAW_CLASSID,1);
  PetscValidPointer(type,2);
  *type = ((PetscObject)draw)->type_name;
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "PetscDrawRegister"
/*@C
   PetscDrawRegister - Adds a method to the graphics package.

   Not Collective

   Input Parameters:
+  name_solver - name of a new user-defined graphics class
-  routine_create - routine to create method context

   Level: developer

   Notes:
   PetscDrawRegister() may be called multiple times to add several user-defined graphics classes

   Sample usage:
.vb
   PetscDrawRegister("my_draw_type", MyDrawCreate);
.ve

   Then, your specific graphics package can be chosen with the procedural interface via
$     PetscDrawSetType(ksp,"my_draw_type")
   or at runtime via the option
$     -draw_type my_draw_type

   Concepts: graphics^registering new draw classes
   Concepts: PetscDraw^registering new draw classes

.seealso: PetscDrawRegisterAll(), PetscDrawRegisterDestroy()
@*/
PetscErrorCode  PetscDrawRegister(const char *sname,PetscErrorCode (*function)(PetscDraw))
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = PetscFunctionListAdd(&PetscDrawList,sname,function);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "PetscDrawSetFromOptions"
/*@
   PetscDrawSetFromOptions - Sets the graphics type from the options database.
      Defaults to a PETSc X windows graphics.

   Collective on PetscDraw

   Input Parameter:
.     draw - the graphics context

   Options Database Keys:
+   -nox - do not use X graphics (ignore graphics calls, but run program correctly)
.   -nox_warning - when X windows support is not installed this prevents the warning message from being printed
.   -draw_pause <pause amount> -- -1 indicates wait for mouse input, -2 indicates pause when window is to be destroyed
.   -draw_marker_type - <x,point>
.   -draw_save [optional filename] - (X windows only) saves each image before it is cleared to a file
.   -draw_save_final_image [optional filename] - (X windows only) saves the final image displayed in a window
.   -draw_save_movie - converts image files to a movie  at the end of the run. See PetscDrawSetSave()
-   -draw_save_single_file - saves each new image in the same file, normally each new image is saved in a new file with filename_%d

   Level: intermediate

   Notes:
    Must be called after PetscDrawCreate() before the PetscDraw is used.

    Concepts: drawing^setting options
    Concepts: graphics^setting options

.seealso: PetscDrawCreate(), PetscDrawSetType(), PetscDrawSetSave(), PetscDrawSetSaveFinalImage()

@*/
PetscErrorCode  PetscDrawSetFromOptions(PetscDraw draw)
{
  PetscErrorCode    ierr;
  PetscBool         flg,nox;
  char              vtype[256];
  const char        *def;
#if !defined(PETSC_USE_WINDOWS_GRAPHICS) && !defined(PETSC_HAVE_X)
  PetscBool         warn;
#endif

  PetscFunctionBegin;
  PetscValidHeaderSpecific(draw,PETSC_DRAW_CLASSID,1);

  ierr = PetscDrawRegisterAll();CHKERRQ(ierr);

  if (((PetscObject)draw)->type_name) def = ((PetscObject)draw)->type_name;
  else {
    ierr = PetscOptionsHasName(((PetscObject)draw)->options,NULL,"-nox",&nox);CHKERRQ(ierr);
    def  = PETSC_DRAW_NULL;
#if defined(PETSC_USE_WINDOWS_GRAPHICS)
    if (!nox) def = PETSC_DRAW_WIN32;
#elif defined(PETSC_HAVE_X)
    if (!nox) def = PETSC_DRAW_X;
#elif defined(PETSC_HAVE_GLUT)
    if (!nox) def = PETSC_DRAW_GLUT;
#elif defined(PETSC_HAVE_OPENGLES)
    if (!nox) def = PETSC_DRAW_OPENGLES;
#else
    ierr = PetscOptionsHasName(NULL,NULL,"-nox_warning",&warn);CHKERRQ(ierr);
    if (!nox && !warn) (*PetscErrorPrintf)("PETSc installed without X windows, Microsoft Graphics, OpenGL ES, or GLUT/OpenGL on this machine\nproceeding without graphics\n");
#endif
  }
  ierr = PetscObjectOptionsBegin((PetscObject)draw);CHKERRQ(ierr);
  ierr = PetscOptionsFList("-draw_type","Type of graphical output","PetscDrawSetType",PetscDrawList,def,vtype,256,&flg);CHKERRQ(ierr);
  if (flg) {
    ierr = PetscDrawSetType(draw,vtype);CHKERRQ(ierr);
  } else if (!((PetscObject)draw)->type_name) {
    ierr = PetscDrawSetType(draw,def);CHKERRQ(ierr);
  }
  ierr = PetscOptionsName("-nox","Run without graphics","None",&nox);CHKERRQ(ierr);
#if defined(PETSC_HAVE_X)
  {
    char      filename[PETSC_MAX_PATH_LEN];
    PetscBool save,movie = PETSC_FALSE;
    ierr = PetscOptionsBool("-draw_save_movie","Make a movie from the images saved (X Windows only)","PetscDrawSetSave",movie,&movie,NULL);CHKERRQ(ierr);
    ierr = PetscOptionsBool("-draw_save_single_file","Each new image replaces previous image in file","PetscDrawSetSave",draw->savesinglefile,&draw->savesinglefile,NULL);CHKERRQ(ierr);
    ierr = PetscOptionsString("-draw_save","Save graphics to file (X Windows only)","PetscDrawSetSave",filename,filename,PETSC_MAX_PATH_LEN,&save);CHKERRQ(ierr);
    if (save) {
      ierr = PetscDrawSetSave(draw,filename,movie);CHKERRQ(ierr);
    }
    ierr = PetscOptionsString("-draw_save_final_image","Save graphics to file (X Windows only)","PetscDrawSetSaveFinalImage",filename,filename,PETSC_MAX_PATH_LEN,&save);CHKERRQ(ierr);
    if (save) {
      ierr = PetscDrawSetSaveFinalImage(draw,filename);CHKERRQ(ierr);
    }
  }
#endif
  ierr = PetscOptionsReal("-draw_pause","Amount of time that program pauses after plots","PetscDrawSetPause",draw->pause,&draw->pause,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsEnum("-draw_marker_type","Type of marker to use on plots","PetscDrawSetMarkerType",PetscDrawMarkerTypes,(PetscEnum)draw->markertype,(PetscEnum *)&draw->markertype,NULL);CHKERRQ(ierr);

  /* process any options handlers added with PetscObjectAddOptionsHandler() */
  ierr = PetscObjectProcessOptionsHandlers(PetscOptionsObject,(PetscObject)draw);CHKERRQ(ierr);

  ierr = PetscDrawViewFromOptions(draw,NULL,"-draw_view");CHKERRQ(ierr);
  ierr = PetscOptionsEnd();CHKERRQ(ierr);
  PetscFunctionReturn(0);
}
