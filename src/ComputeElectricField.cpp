#include "ComputeElectricField.h"

// Computes E_y = -dV/dy at nodes (approximate)
void ComputeElectricField(GridFunction &V, GridFunction &E)
{
    // Ensure E is vector-valued
    int dim = V.FESpace()->GetMesh()->Dimension();
    if (E.Size() != V.Size() * dim)
    {
        std::cerr << "Error: E must be allocated as a vector-valued GridFunction "
                   << "with dim*V.Size() DOFs." << std::endl;
        return;
    }

    // Wrap the scalar GridFunction as a gradient coefficient
    GradientGridFunctionCoefficient gradV(&V); // computes ∇V
    E.ProjectDiscCoefficient(gradV, GridFunction::ARITHMETIC); 
    E *= -0.01 ;// Convert V/m → V/cm for visualization and negate
}


void ComputeFieldMagnitude(GridFunction &E, GridFunction &Emag)
{
    Mesh *mesh = Emag.FESpace()->GetMesh();
    int dim = mesh->Dimension();
    int nverts = mesh->GetNV();

    // Vector GridFunction stores components in contiguous blocks:
    // [Ex(v0..vn-1), Ey(v0..vn-1), (Ez...)]
    for (int i = 0; i < nverts; i++)
    {
        double sum = 0.0;
        for (int d = 0; d < dim; d++)
        {
            double comp = E(d * nverts + i);
            sum += comp * comp;
        }
        Emag(i) = std::sqrt(sum);
    }
}
