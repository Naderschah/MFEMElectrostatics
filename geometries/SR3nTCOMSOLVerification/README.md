
### Comparing COMSOL to MFEM implementation

Using the COMSOL exported geometry and matching parameters a check is done to verify this simulation works comparably. 

Exporting the geometry directly prooved to not be possible due to some CAD errors in the COMSOL kernel. Importing the DXF also prooved not possible due to the complexity of the model and a failure to recitify the geometry. Exporting the mesh failed due to the only format that actually exports the boundarie and volume attributes being the mphtxt and mphbin files which while I did find a conversion tool can not be converted in a way that I can then use the mesh file anywhere.

So now we are doing SALOME Shaper. 