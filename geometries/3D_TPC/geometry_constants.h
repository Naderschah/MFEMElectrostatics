#ifndef GEOMETRY_CONSTANTS_H
#define GEOMETRY_CONSTANTS_H

// Debug params
constexpr bool debug     = true;
constexpr bool QuickMesh = true;

// Geometry / mesh parameters
constexpr double DriftRegionHeight      = 0.5;
constexpr int    n_wires_anode          = 10;
constexpr int    n_wires_gate           = 11;
constexpr double wire_diameter          = 0.01;
constexpr double inner_radius           = 0.2;
constexpr double outer_radius           = 0.25;
constexpr double ring_thickness         = 0.03;
constexpr double anode_wire_height      = 0.45;
constexpr double gate_wire_height       = 0.35;
constexpr double cathode_wire_height    = 0.05;

// Components to use
const bool USE_ANODE   = true;
const bool USE_GATE    = true;
const bool USE_CATHODE = true;
const bool USE_PTFE    = true;
// TODO Loosing reference to objects if i cut this out
const bool USE_LXe     = false; 
const bool USE_GXe     = false;

#endif
