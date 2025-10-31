#ifndef BOUNDARY_CONDITIONS_H
#define BOUNDARY_CONDITIONS_H

#include "mfem.hpp"
#include "boundary_conditions.h"
using namespace mfem;

struct Config; // forward declaration
Array<int> GetDirichletAttributes(Mesh *mesh, const Config& cfg);
void ApplyDirichletValues(GridFunction &V, const Array<int> &dirichlet_attr, const Config& cfg);

#endif
