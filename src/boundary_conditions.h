#ifndef BOUNDARY_CONDITIONS_H
#define BOUNDARY_CONDITIONS_H

#include "mfem.hpp"
#include "boundary_conditions.h"
#include "config/Config.h"
using namespace mfem;


Array<int> GetDirichletAttributes(Mesh *mesh, const std::shared_ptr<const Config>&);
void ApplyDirichletValues(GridFunction &V, const Array<int> &dirichlet_attr, const std::shared_ptr<const Config>&);

#endif
