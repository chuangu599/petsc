/*

*/
#include "pcimpl.h"

int PCiNoneApply(PC ptr,Vec x,Vec y)
{
  return VecCopy(x,y);
}

int PCiNoneCreate(PC pc)
{
  pc->apply   = PCiNoneApply;
  pc->destroy = 0;
  pc->setup   = 0;
  return 0;
}
