## Basic SinglePhase TPC 

Check the capacitor readme for basics 

### Geometry

The scripting language is too finicky, the internals of the electrode rings with wires are created well for the cathode, however, for some reason I can not figure out the inner ring surface does not mesh correclty if it is not at z = 0.05 ring creation height. So here we switch to the Cpp API to figure out if this is language specific or some oddity of the gmsh utility. 