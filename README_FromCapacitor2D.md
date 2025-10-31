## Simulating a parallel plate capacitor

The geometry is most easily defined in geo files, since I wanted a variable geometry to make vacuum different diffusion coefficient regions etc I made a geo.in file that uses values from constants.h to generate the geo file via CMake 

## Geometry

Most importantly, parse the output, you see anything in any color other than black, the final pink line or the first blue line you need to debug. Things fail silently!

Large Volumes, say the outer simulation domain, must be created first, otherwise smaller contained voumes do not mesh correctly. 

These [[https://gitlab.onelab.info/gmsh/gmsh/-/tree/master/tutorials|tutorials]] are invaluable, I am still not sure what this language is called (I think its only used for .geo files and only readable by gmsh), and this alongside a lot of trial and error got me to get this to actually work. 

Quick help, there is quite a bit in there, calling it complete is objectively incorrect
```
gmsh -help
```

#### Things to always include / Usefull notes

The starter line is 
```
SetFactory("OpenCASCADE");
Mesh.MshFileVersion = 2.2;
Mesh.SaveAll = 0; // writes ONLY Physical groups
```
At the end you generally want to put 
```
Mesh.Optimize = 1;
Mesh.CharacteristicLengthMax = Sqrt(W*W + H*H + ...) / n;  // tune as you like
```
n is the number of elements you want, use a smallish number if you do autorefinement, I am not sure how to do per dimension characteristic lengths. Optimize I may be wrong with, seems smart to include, I have no reason to believe so or to believe otherwise other than the name. 


If needed you can hide models that are not meshed, but in the end I didn't end up using it, probably a good shortcut if you have some annoying mesh element that you cant seem to get rid of. Hidden elements are meshed unless the second line is provided:
```
Hide { Volume{badActor}; }
Mesh.MeshOnlyVisible = 1; 
```

#### 3D

Generating geometry is irritating to say the least. Using the default CAD kernel is insufficient for proper implementation so the OpenCASCADE kernel must be used, which makes us susceptible to the standard CAD problems (overlapping geometries, disconnected surfaces, etc.). 

As is usually the case with CAD getting surfaces from additive processes is easier than for subtractive processes. However, this doesnt work well with electrostatics where some potential provider is embedded in a simulation environment. 

Its best to start with a simulation Volume, lets call this OuterV and place it symmetric about 0 
```
OuterV = newv;
Box(OuterV) = { -W/2, -H/2, -T/2,  W, H, T };  
```
To remove an element from it and retrieve the surface it used to have we cant just use its surface directly, but must merge it with the outers surface and source it from there.
So lets call the element RemoveV
```
RemoveV = newv; // create new outer volume 
Box(RemoveV) = { -hx, -hy, -hz,  hx, hy, hz }; / /define it 
```
To then remove this volume we do
```
Frag[] = BooleanFragments{ Volume{OuterV}; Delete; }{ Volume{RemoveV}; Delete};
BakgroundVolume = Frag[0];
RemoveV = Frag[1]
```
The boolean operations keep both the original and new volumes in scope, so we delete the original outer Volume with its new version with an internal surface in BackgroundVolume. 
RemoveV then contains the inner fragment volume which shares the same surface, allthough for it, it is external. We delete the original RemoveV to avoid lingering pieces (this isnt required but I recommend it for clarity), we coudl also move on with the original RemoveV as the surface appears to now be shared anyway (but this seems error prone).

Once all volumes are removed and you know the Volume from which is subtracted is done we can do 
```
Physical Volume(3) = { BakgroundVolume };
```
Here the attribute (3) corresponds to the way we can access the Volume in code, TODO When do we do this -> Havent done this yet.

Now for Dirichlet boundary conditions we wan't surfaces as the internal volume doesn't have to be modeled. To get them we must define a list which can then be turned into a surface
```
boundary_removed_volume[] = Boundary{ Volume{RemoveV}; };
Physical Surface(1)   = { boundary_removed_volume[] };
```
This surface is registered with attribute (1), this is used directly in MFEM to assign the dirichlet boundary condition, so we can set this now to a fixed potential. 

NOTE: I have not figured out how to do the above with a 3D embedding of a 2D problem, Ie the plate capacitors touching the simulation bounds, this is since BooleanFragments maintains a surface about the entire volume, but if we touch the original volumes bounds a surface will still be generated at the touching point, and at this point no volume exists so it will raise an error or segfault or similar. It might be possible to do this, if you go into gmsh use the visualizer and figure out the surface id and remove it from the list. But I dont quite trust the staticness of these id's across versions of gmsh.

#### 2D

2D is much simpler, if we choose we can simply use the basic kernel, curvature and what not is more complicated to make "accurately". But its really easy to use, even if cumbersome. Using the CAD kernel is much more concise. 

To achieve the 2D version of the above described procedure, we create a new outer surface
```
SOuter = news; // create new outer surface
Rectangle(Outer) = { -W/2, -H/2, 0,  W, H };  // define it 
SRemove = news; // create a new removal surface
Rectangle(SRemove) = { -hx, -hy, 0,  hx, hy };
```
Here we can just do boolean difference ( its the problem of selecting the external surface in 3D vs 2D why we cant \[compactly\] do this in 3D)
```
Slist[] = BooleanDifference{ Surface{SOuter}; }{ Surface{SRemove}; }; // keep originals for tagging
S = Slist[0]; // new background mesh
```
We define our simulation surface
```
Physical Surface(3) = { S }; // Ie the background -> 3  in my code is untagged so you can put any number that is not tagged (same goes for 3D, this is also assuming you access surfaces in your code otherwise it does not matter)
```
We also need boundaries, this can be applied to literally any object pre or post boolean difference it does not matter, in 2D selecting the correct surface is much easier. 
```
BRemoved[] = Boundary{ Surface{SRemove}; }; 
BOuter[] = Boundary{ Surface{SOuter}; }
```
And we define a Curve outlining out dirichlet boundary condition with an actual tagged name 
```
Physical Curve(1)   = { BRemoved[] };
```
Where we dont need this for outer as we already tagged the outer surface (anything tagged is rendered)

#### Language Syntax
TODO: Write a cpp API for creating geometry, the language is cumbersome very repetitive and consequently hard to read

Functions can be defined with Macro (Dont return in an if always return at the end of a funtion):
```
Macro MakeObject(xc, yx, hx, hy, t)
    v = newv
    Box(v) = { xc - hx, yc - hy, 0, 2*hx, 2*hy, t };
    // v is global so it will get overwritten on the next call
    volumes[] += v
    Return // could return a scalar that is local to the function
EndMacro
```
NOTE: Macro's are kind of useless, you get 1, thats it after it complains about "syntax error (Macro)" so write as a macro merge until you minimize If Else and then write it out. 
Loops exist (loop variables are like bash so For i In {0:2} expands to a list [0,1,2] and changing in a loop iteration has no effect on future iterations)
```
For i In {0:10}
  ...
EndFor
While (condition)
  ...
EndWhile
```

If else exists
```
If (a > b && a < 10)
  x = 1;
Else
  x = 0;
EndIf
```
Math 
```
+   addition
-   subtraction
*   multiplication
/   division
^   power  (right associative)
==   equal
!=   not equal
>    greater
<    less
>=   greater or equal
<=   less or equal
&&   logical and
||   logical or
!    logical not
Sqrt() square root (havent tested for more)
```

Commenting 
```
// Single-line comment
/* Multi-line comment */
```

Debugging
```
Printf("Value = %g", x);
```

IMPORTANT: Everything is global, so watch out, scoping is not really possible 

IMPORTANT: I vaguely recall using a few functions in infix, prefix and or postfix notation, I dont think this is global, I am not sure this is intended, stick to infix. 

#### Misc Debugging

```
MFEM abort: (r,c,f) = (0,275,4115)
 ... in function: int mfem::STable3D::operator()(int, int, int) const
 ... in file: /home/felix/mfem/general/stable3d.cpp:112
```
Means there are surfaces not connected to an object.

If you can not figure out which do:
```
awk '/^\$Elements/{flag=1; next} /^\$EndElements/{flag=0} flag' geometry.msh > elements.txt
```
This extracts each element defintion into elements.txt from the mesh file. 
The text will look like:
```
linear_id element_type number-of-tags <tag1> <tag2> ... <tagn> node1 node2 ... nodeN
```
linear_id just increases per row. element type is shown in the table below. number-of-tags provides the integer number of tags that follow immediately. Then the integer tags, and again the same number of integer node IDs (N != n)
| Element type | Object | 
|----------|---------|
| 1 | Line (edge) |
| 2 | Triangle |
| 3 | Quadrangle |
| 4 | Tetrahedron |
| 5 | Hexahedron |
| 6 | Prism |
| 7 | Pyramid |

So an example:
3 2 2 6 75 27 7313 1191 -> The third created item corresponding to a triangle defined by two integer tags which are 6 (Group ID) and 75 (Geometric Entity) and lastly the node numbers for the triangle 27 7313 1191 

Of course each element has a different number of node numbers so this is not a csv in its current format. 

## Simulation 

In build I do 


rm -r * && cmake ../src/ && make && ./capacitor && glvis -m simulation_mesh.mesh -g solution_V.gf -k "AmaagcmRj" 

So clear build dir

Cmake (also generates the geo file)
Run the simulation
Visualize with glvis (type h for it to print controls to terminal, if there is a 3d volume with internal objects, the solution will look wrong, hit i to slice it that might help otherwise spam x or z or just use paraview or meshio, havent tried them yet), the -k flag set keyboard options to run j is make the background black R is center on a plane I fergot the rest at this point. 




## Misc notes


