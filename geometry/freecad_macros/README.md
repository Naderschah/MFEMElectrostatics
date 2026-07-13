## FreeCad Macros

These are the macros I use to make life easier in preparing the meshes and files. All is vibe coded. 

For the meshing workflows all to be merged components are placed inside their own App:Part containers if it is a singular body that is logically distinct it is also placed in its own App:Part, for highly repetitive objects (think field shaping rings) I apste these alongside the input naming and App:Part naming convention I want.

The relevant scripts are:

`destroyLocalCoordinates.FCMacro` Its purpose is to create a copy of every component without nested coordinate frames, this makes future operations easier, it is not necessarily required, most scripts resolve this automatically at this point. But it saves a lot of hastle if coordinate systems are not carried correctly.

`ExportSingleBrep.FCMacro` Exports a selected App:Part in brep representation, this will be ingested by brep to stl then Alpha Wrap etc. Mainly here in case one component needs reexport

`ExportAppPartAsBrep.FCMacro` Same as the previous but it does all App:Part's

`MakePartFromEach.FCMacro` Have a lot of similar components that should all have the same name but be individual App:Parts, this creates them with a base name and a numeric extension nnn. Note order may not be preserved so if the index is important target explicitly with a custom script, note if each thing has two components it will create one Part for each of these components.

`MakePartWithThis.FCMacro` Highlight all the components that should be a part, run this script and they will be moved to a new part with a specified name. 
