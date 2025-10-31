#ifndef COMPUTEELECTRICFIELD_H
#define COMPUTEELECTRICFIELD_H

#include "mfem.hpp"
#include "constants.h"
using namespace mfem;

void ComputeElectricField(GridFunction &V, GridFunction &E_y); 
void ComputeFieldMagnitude(GridFunction &E, GridFunction &Emag);

#endif