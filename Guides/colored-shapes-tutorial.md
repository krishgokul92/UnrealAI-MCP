# 🎨 Colored Shapes Tutorial

Complete step-by-step guide for creating colored geometric shapes in Unreal Engine using MCP tools.

## 🎯 Overview

This tutorial shows how to create custom colored shapes (spheres, cubes, cylinders) by:
1. Creating Blueprint classes
2. Adding mesh components  
3. Configuring materials and colors
4. Spawning colored actors in your scene

## 📋 Prerequisites

- Unreal Engine 5.5+ with UnrealMCP plugin enabled
- Python MCP server running and connected
- Basic understanding of Unreal's coordinate system

## 🟢 Step-by-Step Process

### Step 1: Create the Blueprint Class
```bash
create_blueprint(name="BP_ColoredSphere", parent_class="Actor")
```

**Parameters Explained:**
- `name`: Use descriptive names like "BP_RedBall", "BP_BlueSquare"
- `parent_class`: Almost always "Actor" for basic objects

### Step 2: Add Static Mesh Component
```bash
add_component_to_blueprint(
    blueprint_name="BP_ColoredSphere",
    component_name="SphereMesh", 
    component_type="StaticMeshComponent"
)
```

**Naming Convention:**
- Use `[Shape]Mesh` format: "SphereMesh", "CubeMesh", "CylinderMesh"
- This component will hold the 3D geometry

### Step 3: Set the Mesh Geometry
```bash
set_static_mesh_properties(
    blueprint_name="BP_ColoredSphere",
    component_name="SphereMesh",
    static_mesh="/Engine/BasicShapes/Sphere.Sphere"
)
```

**Available Basic Shapes:**
| Shape | Asset Path |
|-------|------------|
| **Sphere** | `/Engine/BasicShapes/Sphere.Sphere` |
| **Cube** | `/Engine/BasicShapes/Cube.Cube` |
| **Cylinder** | `/Engine/BasicShapes/Cylinder.Cylinder` |
| **Plane** | `/Engine/BasicShapes/Plane.Plane` |

### Step 4: Apply Base Color
```bash
set_mesh_material_color(
    blueprint_name="BP_ColoredSphere",
    component_name="SphereMesh",
    color=[1.0, 0.0, 0.0, 1.0],  # Red color
    material_path="/Engine/BasicShapes/BasicShapeMaterial",
    parameter_name="BaseColor"
)
```

### Step 5: Apply Backup Color Parameter
```bash
set_mesh_material_color(
    blueprint_name="BP_ColoredSphere", 
    component_name="SphereMesh",
    color=[1.0, 0.0, 0.0, 1.0],  # Same red color
    material_path="/Engine/BasicShapes/BasicShapeMaterial",
    parameter_name="Color"
)
```

**Why Two Color Steps?**
Different material versions use different parameter names. This ensures compatibility.

### Step 6: Compile the Blueprint
```bash
compile_blueprint(blueprint_name="BP_ColoredSphere")
```

**Critical:** Always compile before spawning actors. This applies all changes to the Blueprint.

### Step 7: Spawn the Colored Actor
```bash
spawn_blueprint_actor(
    actor_name="RedSphereInstance",
    blueprint_name="BP_ColoredSphere", 
    location=[0, 0, 200]
)
```

**Location Notes:**
- Use Z=200 or higher to spawn above ground
- Separate objects by 300+ units to avoid overlap

## 🎨 Color Reference

### Standard Colors (RGBA Format)
```bash
Red:    [1.0, 0.0, 0.0, 1.0]
Green:  [0.0, 1.0, 0.0, 1.0] 
Blue:   [0.0, 0.0, 1.0, 1.0]
Yellow: [1.0, 1.0, 0.0, 1.0]
Purple: [1.0, 0.0, 1.0, 1.0]
Cyan:   [0.0, 1.0, 1.0, 1.0]
Orange: [1.0, 0.5, 0.0, 1.0]
Pink:   [1.0, 0.5, 0.5, 1.0]
White:  [1.0, 1.0, 1.0, 1.0]
Black:  [0.0, 0.0, 0.0, 1.0]
Gray:   [0.5, 0.5, 0.5, 1.0]
```

### Custom Colors
- Values range from 0.0 to 1.0
- Format: [Red, Green, Blue, Alpha]
- Alpha should usually be 1.0 for solid objects

## 🎯 Complete Examples

### Example 1: Red Sphere
```bash
# Create Blueprint
create_blueprint(name="BP_RedBall", parent_class="Actor")

# Add mesh component
add_component_to_blueprint(
    blueprint_name="BP_RedBall",
    component_name="BallMesh", 
    component_type="StaticMeshComponent"
)

# Set sphere geometry
set_static_mesh_properties(
    blueprint_name="BP_RedBall",
    component_name="BallMesh",
    static_mesh="/Engine/BasicShapes/Sphere.Sphere"
)

# Apply red color (BaseColor parameter)
set_mesh_material_color(
    blueprint_name="BP_RedBall",
    component_name="BallMesh", 
    color=[1.0, 0.0, 0.0, 1.0],
    material_path="/Engine/BasicShapes/BasicShapeMaterial",
    parameter_name="BaseColor"
)

# Apply red color (Color parameter backup)
set_mesh_material_color(
    blueprint_name="BP_RedBall",
    component_name="BallMesh",
    color=[1.0, 0.0, 0.0, 1.0], 
    material_path="/Engine/BasicShapes/BasicShapeMaterial",
    parameter_name="Color"
)

# Compile Blueprint
compile_blueprint(blueprint_name="BP_RedBall")

# Spawn the red ball
spawn_blueprint_actor(
    actor_name="RedBallInstance",
    blueprint_name="BP_RedBall",
    location=[0, 0, 200]
)
```

### Example 2: Blue Cube
```bash
# Create Blueprint
create_blueprint(name="BP_BlueBox", parent_class="Actor")

# Add mesh component
add_component_to_blueprint(
    blueprint_name="BP_BlueBox",
    component_name="BoxMesh",
    component_type="StaticMeshComponent"
)

# Set cube geometry  
set_static_mesh_properties(
    blueprint_name="BP_BlueBox",
    component_name="BoxMesh",
    static_mesh="/Engine/BasicShapes/Cube.Cube"
)

# Apply blue color (both parameters)
set_mesh_material_color(
    blueprint_name="BP_BlueBox",
    component_name="BoxMesh",
    color=[0.0, 0.0, 1.0, 1.0],
    material_path="/Engine/BasicShapes/BasicShapeMaterial", 
    parameter_name="BaseColor"
)

set_mesh_material_color(
    blueprint_name="BP_BlueBox",
    component_name="BoxMesh", 
    color=[0.0, 0.0, 1.0, 1.0],
    material_path="/Engine/BasicShapes/BasicShapeMaterial",
    parameter_name="Color"
)

# Compile and spawn
compile_blueprint(blueprint_name="BP_BlueBox")
spawn_blueprint_actor(
    actor_name="BlueBoxInstance",
    blueprint_name="BP_BlueBox",
    location=[300, 0, 200]
)
```

### Example 3: Green Cylinder
```bash
# Create and configure green cylinder
create_blueprint(name="BP_GreenCylinder", parent_class="Actor")

add_component_to_blueprint(
    blueprint_name="BP_GreenCylinder",
    component_name="CylinderMesh",
    component_type="StaticMeshComponent"
)

set_static_mesh_properties(
    blueprint_name="BP_GreenCylinder", 
    component_name="CylinderMesh",
    static_mesh="/Engine/BasicShapes/Cylinder.Cylinder"
)

# Green color application
set_mesh_material_color(
    blueprint_name="BP_GreenCylinder",
    component_name="CylinderMesh",
    color=[0.0, 1.0, 0.0, 1.0],
    material_path="/Engine/BasicShapes/BasicShapeMaterial",
    parameter_name="BaseColor"
)

set_mesh_material_color(
    blueprint_name="BP_GreenCylinder",
    component_name="CylinderMesh", 
    color=[0.0, 1.0, 0.0, 1.0],
    material_path="/Engine/BasicShapes/BasicShapeMaterial",
    parameter_name="Color" 
)

compile_blueprint(blueprint_name="BP_GreenCylinder")
spawn_blueprint_actor(
    actor_name="GreenCylinderInstance",
    blueprint_name="BP_GreenCylinder",
    location=[-300, 0, 200]
)
```

## 🔧 Advanced Techniques

### Creating Multiple Colors of the Same Shape
```bash
# Create base Blueprint once
create_blueprint(name="BP_GenericSphere", parent_class="Actor")
add_component_to_blueprint(
    blueprint_name="BP_GenericSphere",
    component_name="SphereMesh",
    component_type="StaticMeshComponent"
)
set_static_mesh_properties(
    blueprint_name="BP_GenericSphere",
    component_name="SphereMesh", 
    static_mesh="/Engine/BasicShapes/Sphere.Sphere"
)

# Apply different colors and spawn multiple instances
colors = [
    ([1.0, 0.0, 0.0, 1.0], [0, 0, 200]),      # Red at origin
    ([0.0, 1.0, 0.0, 1.0], [300, 0, 200]),    # Green to the right
    ([0.0, 0.0, 1.0, 1.0], [600, 0, 200]),    # Blue further right
]

for i, (color, location) in enumerate(colors):
    # Set color for this instance
    set_mesh_material_color(
        blueprint_name="BP_GenericSphere",
        component_name="SphereMesh",
        color=color,
        material_path="/Engine/BasicShapes/BasicShapeMaterial",
        parameter_name="BaseColor"
    )
    
    compile_blueprint(blueprint_name="BP_GenericSphere") 
    
    spawn_blueprint_actor(
        actor_name=f"Sphere_{i}",
        blueprint_name="BP_GenericSphere",
        location=location
    )
```

### Scaling and Positioning
```bash
# Spawn actor first
spawn_blueprint_actor(
    actor_name="CustomShape",
    blueprint_name="BP_RedBall",
    location=[0, 0, 200]
)

# Then adjust transform for custom size/position
set_actor_transform(
    name="CustomShape",
    scale=[2.0, 2.0, 0.5],  # Wide, flat ellipsoid
    location=[500, 500, 100]
)
```

## 🌐 Coordinate System Reference

### Unreal Engine Coordinates
- **X-axis (Red)**: Forward/Backward (positive = forward)
- **Y-axis (Green)**: Left/Right (positive = right)  
- **Z-axis (Blue)**: Up/Down (positive = up)
- **Units**: Centimeters (100 = 1 meter)

### Safe Positioning Guidelines
- **Ground Level**: Z = 0
- **Above Ground**: Z = 200+ (2 meters up)
- **Horizontal Spacing**: 300+ units between objects
- **Large Objects**: 500+ units spacing

## ❓ Troubleshooting

### Common Issues

**Problem:** Actor spawns but has no color
**Solution:** Ensure both "BaseColor" and "Color" material parameters are set

**Problem:** Blueprint compilation fails  
**Solution:** Check that all component names are unique and valid

**Problem:** Objects spawn underground
**Solution:** Use Z coordinates of 200 or higher

**Problem:** Objects overlap or clip
**Solution:** Increase spacing between spawn locations

### Debugging Tips
1. Use `get_actors_in_level()` to verify spawned actors
2. Use `find_actors_by_name()` to locate specific objects
3. Check the Unreal Editor's Blueprint compiler for error messages
4. Verify material paths match Unreal's asset structure

## 🎊 Creative Applications

### Rainbow Display
Create a spectrum of colored spheres:
```bash
# Create seven colored spheres in rainbow order
colors = [
    [1.0, 0.0, 0.0, 1.0],  # Red
    [1.0, 0.5, 0.0, 1.0],  # Orange  
    [1.0, 1.0, 0.0, 1.0],  # Yellow
    [0.0, 1.0, 0.0, 1.0],  # Green
    [0.0, 0.0, 1.0, 1.0],  # Blue
    [0.5, 0.0, 1.0, 1.0],  # Indigo
    [1.0, 0.0, 1.0, 1.0],  # Violet
]

for i, color in enumerate(colors):
    # Create unique Blueprint for each color
    create_blueprint(name=f"BP_Rainbow_{i}", parent_class="Actor")
    # ... (rest of setup process)
    spawn_blueprint_actor(
        actor_name=f"RainbowSphere_{i}",
        blueprint_name=f"BP_Rainbow_{i}",
        location=[i * 300, 0, 200]
    )
```

### Traffic Light System
```bash
# Red light (top)
# ... create red sphere at [0, 0, 400]

# Yellow light (middle)  
# ... create yellow sphere at [0, 0, 200]

# Green light (bottom)
# ... create green sphere at [0, 0, 0]
```

---