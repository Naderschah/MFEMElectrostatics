#ifndef SOLVER_H
#define SOLVER_H

#include "mfem.hpp"
#include "constants.h"
#include "boundary_conditions.h"
using namespace mfem;

struct Config; // forward declaration
GridFunction SolvePoisson(FiniteElementSpace &fespace, const Array<int> &dirichlet_attr, const Config& cfg);


#endif
