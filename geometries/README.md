# Geometries

## VTU 

Form Visualization Toolkit, a software aimed at visualizing scientific data. They produced ParaView as well. 

Geomtries can be produced with Salome SHAPER a free to use open source software [website](https://www.salome-platform.org/?page_id=327), its a pain to install so i use docker image feelpp/salome.

Here is my blurb
```
docker run --rm \
  -e DISPLAY=$DISPLAY \
  -e QT_X11_NO_MITSHM=1 \
  -v /tmp/.X11-unix:/tmp/.X11-unix:ro \
  -v /home/felix:/home/felix \
  --shm-size=2g --ipc=host --net=host \
  feelpp/salome:9.8.0-ubuntu-20.04 salome -m SHAPER
```
It comes with a python package that can be used as a side by side scripting engine to generate the geometry. This makes life much easier

## GMSH

To generate a simulation mesh gmsh can be used provided version 2.2 is specified in mesh creation. Gmsh has its own scripting language that was explored in all the .geo.in files where CMake was used to substitute in variable expressions. This language works for simple 2D problems allbeit very rough, the syntax is limited and since there is a Cpp language for it its not worthwhile using. Nonetheless at the bottom are my notes on this language.

Note we can produce never gmsh files and just convert with meshio.

### Logic

Goemetries and solver must share parameters with one another. Mainly relating to surface and volume ID's to assign boundary conditions, to achieve this the solver and indeces are predefined in a config.yaml file. Geometry relevant fields are 
```config.yaml
materials: 
  Name:
    attr_id: int
    epsilon_r: float

boundaries:
  Name:
    bdr_id: int
    type: dirichlet
    value: float
``` 
Start your ID's high the used range incremenets with every CAD operation, so we want to be well outside the range (but also not obscenely large as we have to make a vector the size of max(bdr_id) and one of max(attr_id)).
Each material and boundary is iterated over and its respective parameters are set. Note that the ID is the only reference between the mesh and the solver. 

The only boundary condition currently implemented is dirichlet.


### Geometry in Cpp

The best source on the language [is the header file](https://gitlab.onelab.info/gmsh/gmsh/-/blob/master/api/gmsh.h), its fairly short and there are also tutorials. Avoid Booleans as much as possible but for simplicity it should be fine to fragment objects out of the background volume. Do not use boolean difference but use boolean fragment which merges the surfaces of the fragmented volumes (which is important for the ID assignment). 

It is also easiest to only create points (in 2D) and create lines between them and then finally creating boundary loops external and internal (producing holes) to generate one coherent geometry. This appears favorable over any sort of boolean operation. Overall this is tedious.


### Geo Scripting Language

TODO Include cleaned up bits from README_FromCapacitor2D