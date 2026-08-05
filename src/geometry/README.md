This module handles geometry-to-mesh conversion for the simulation suite.

Current behavior:
- Hard-fail meshing flow (no retry or auto-recovery retries).
- No terminal output unless `MeshingOptions.debug=true`.
- Built-in validity checks after meshing:
  - non-manifold triangle edges
  - invalid tagged 1D segments
  - triangle orientation consistency
  - required LXe/GXe physical surface groups
