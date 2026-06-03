# UnrealAI MCP Tools Reference

Current live MCP surface exposed by `Python/unreal_ai_mcp.py`: 202 tools.

This guide consolidates what the MCP server can do today. The tools are grouped by workflow so it is easy to see both the exact tool names and the kinds of editor actions they unlock.

## Capability Summary

| Area | What you can do | Tools |
|------|-----------------|-------|
| Tool discovery and safety | List inferred tool categories, filter the live MCP catalog by category/query/mutation traits, and rely on shared Python-side validation plus structured `error_code`/`hint` responses for common bad inputs before the TCP bridge is called | `get_unreal_tool_categories`, `list_unreal_tools` |
| Bridge, editor context, world partition inspection, layer membership, layer mutation, selection orchestration, and level basics | Verify the UE bridge, inspect the current level, inspect or mutate the current editor selection, inspect open level viewports, inspect current world-partition state, inspect layer catalog and visibility state, inspect actor-layer membership in either direction, create, rename, or delete editor layers, add or remove one actor or the current selection from one editor layer, select or deselect every actor in one editor layer, set or toggle one existing editor layer visible or hidden, restore all layers visible, find actors, spawn actors, delete actors, and move/rotate/scale actors | `ping`, `get_actors_in_level`, `get_selected_actors`, `get_level_viewport_info`, `get_world_partition_info`, `get_all_layers`, `create_layer`, `rename_layer`, `delete_layer`, `get_actor_layers`, `get_actors_in_layer`, `add_actor_to_layer`, `remove_actor_from_layer`, `add_selected_actors_to_layer`, `remove_selected_actors_from_layer`, `select_actors_in_layer`, `deselect_actors_in_layer`, `set_layer_visibility`, `toggle_layer_visibility`, `make_all_layers_visible`, `select_actors`, `clear_actor_selection`, `find_actors_by_name`, `spawn_actor`, `delete_actor`, `set_actor_transform` |
| Editor save, history, screenshot, and placement helpers | Save the current level, undo or redo editor transactions, capture viewport screenshots to disk, and place repeated actor layouts on grids, circles, spline paths, or scattered bounded areas | `save_level`, `undo_last_action`, `redo_last_action`, `capture_viewport_screenshot`, `place_in_grid`, `place_in_circle`, `place_along_spline`, `scatter_in_area` |
| Blueprint asset authoring | Create Blueprint assets, compile them, add components, create function graphs, and browse project materials | `create_blueprint`, `compile_blueprint`, `add_component_to_blueprint`, `get_available_materials`, `create_blueprint_function` |
| Blueprint inspection | Dump Blueprint structure, inspect graph topology, and inspect variable/function details | `read_blueprint_content`, `analyze_blueprint_graph`, `get_blueprint_variable_details`, `get_blueprint_function_details` |
| Widget Blueprint authoring, inspection, hierarchy mutation, layout mutation, binding mutation, and animation mutation | Create Widget Blueprint assets, optionally seed a child under the root panel for validation, inspect editable widget trees, bindings, animations, named-slot content, and common slot metadata across Canvas, box/content, grid, wrap, stack-box, scale-box, widget-switcher/title-bar, and safe-zone layouts, then add, remove, reparent, or mutate supported slot layout fields, function/property bindings, and source-asset animations inside source trees | `create_widget_blueprint`, `read_widget_blueprint_content`, `add_widget_to_widget_blueprint`, `remove_widget_from_widget_blueprint`, `reparent_widget_in_widget_blueprint`, `set_widget_slot_layout_in_widget_blueprint`, `set_widget_property_binding_in_widget_blueprint`, `remove_widget_property_binding_from_widget_blueprint`, `create_widget_animation_in_widget_blueprint`, `remove_widget_animation_from_widget_blueprint` |
| Level Sequence authoring and inspection | Create Level Sequence assets, inspect MovieScene bindings, tracks, sections, and playback metadata, add camera cuts and other master tracks, bind actors, add bound float tracks and keys, and retime playback or sections | `create_level_sequence`, `read_level_sequence_content`, `add_camera_cut_track_to_level_sequence`, `add_actor_possessable_to_level_sequence`, `add_track_to_binding_in_level_sequence`, `add_float_key_to_binding_track_in_level_sequence`, `set_level_sequence_playback_range`, `add_master_track_to_level_sequence`, `add_section_to_master_track_in_level_sequence`, `set_section_range_in_master_track_in_level_sequence`, `remove_section_from_master_track_in_level_sequence` |
| Blueprint graph authoring | Paste validated T3D graphs, browse snippet library, add/connect/delete nodes, add events, set pin defaults, create or mutate Blueprint variables, and edit function signatures | `paste_blueprint_graph`, `list_t3d_snippets`, `get_t3d_snippet`, `add_blueprint_node`, `connect_blueprint_nodes`, `delete_blueprint_node`, `add_event_node`, `set_node_default`, `create_blueprint_variable`, `set_blueprint_variable_properties`, `add_blueprint_function_input`, `add_blueprint_function_output`, `delete_blueprint_function`, `rename_blueprint_function` |
| Blueprint spawning, mesh, physics, and materials | Spawn Blueprint instances into the level, configure component physics, assign meshes, tint materials, apply materials, and inspect material slots | `spawn_blueprint_actor`, `set_physics_properties`, `set_static_mesh_properties`, `set_mesh_material_color`, `apply_material_to_actor`, `apply_material_to_blueprint`, `get_actor_material_info`, `get_blueprint_material_info` |
| Asset registry and import | Search assets, inspect dependencies/referencers, create material instances, and import files into content folders | `find_assets`, `get_asset_dependencies`, `get_referencers`, `create_material_instance`, `import_asset` |
| DataTable and CurveTable inspection and mutation | Inspect DataTable and CurveTable assets, create blank tables, validate row-import payloads, and apply deterministic row create, replace, rename, delete, duplicate, and ordering changes | `read_data_table_content`, `read_data_table_row`, `read_curve_table_content`, `read_curve_table_row`, `create_curve_table_asset`, `upsert_curve_table_row`, `delete_curve_table_row`, `rename_curve_table_row`, `validate_data_table_row_import`, `validate_curve_table_row_import`, `create_data_table_asset`, `upsert_data_table_row`, `delete_data_table_row`, `rename_data_table_row`, `duplicate_data_table_row`, `move_data_table_row` |
| Material graph inspection and authoring | Create blank materials and material functions, inspect graphs, validate them, update base-material parameter defaults or material-instance overrides, create/delete/bulk-delete/connect/disconnect/replace expressions, bind or disconnect base-material outputs, and run recompile/layout helpers | `create_material_asset`, `create_material_function_asset`, `get_material_expressions`, `get_material_connections`, `get_material_parameters`, `set_material_parameters`, `set_material_instance_parameters`, `validate_material_graph`, `create_material_expression`, `delete_material_expression`, `delete_material_expressions`, `replace_material_expression`, `connect_material_expressions`, `disconnect_material_expressions`, `connect_material_property`, `disconnect_material_property`, `recompile_material`, `layout_material_expressions` |
| Level design helpers | Snap actors to grid, align them, duplicate them, and focus the editor viewport | `snap_actors_to_grid`, `align_actors`, `duplicate_actor`, `focus_viewport` |
| Landscape inspection, weight/height sampling, bounded paint/sculpt mutation, rebuild, creation, and height editing | List landscapes, inspect edit layers and layer info assets, sample world-space height and paint-layer weights at one point, many arbitrary points, a regular grid, a bounded region, a dense height raster, or a dense weight raster for one target layer, then create landscapes, flatten terrain height, paint one layer over a bounded region, sculpt one bounded region to a uniform height, rebuild a landscape, and import heightmaps | `get_landscapes`, `read_landscape_content`, `sample_landscape_point`, `sample_landscape_points`, `sample_landscape_grid`, `sample_landscape_region`, `sample_landscape_height_region`, `sample_landscape_weight_region`, `paint_landscape_layer_region`, `sculpt_landscape_height_region`, `rebuild_landscape`, `create_landscape`, `set_landscape_flat_height`, `import_landscape_heightmap` |
| PCG graph inspection, node catalog, graph editing, and mutation | Inspect PCG graph assets and graph-instance metadata, inspect actor-owned PCG components, list concrete PCG node types, inspect graph nodes plus comment boxes and reroute/subgraph metadata, create PCG graph assets and graph instances, add/delete/move graph nodes, connect or disconnect node pins, mutate generic node settings and state flags, manage comment boxes and named reroutes, reassign subgraph nodes, create/rename/delete graph parameters, set graph or graph-instance parameter values, reset graph-instance overrides, attach PCG components to actors, and create seeded PCG volumes with deterministic readback | `read_pcg_graph_content`, `list_pcg_node_types`, `read_pcg_graph_nodes`, `read_pcg_graph_node`, `create_pcg_graph_asset`, `create_pcg_graph_instance`, `add_pcg_graph_node`, `delete_pcg_graph_node`, `connect_pcg_graph_nodes`, `disconnect_pcg_graph_nodes`, `set_pcg_graph_node_position`, `set_pcg_subgraph_node_asset`, `add_pcg_graph_comment`, `update_pcg_graph_comment`, `delete_pcg_graph_comment`, `add_pcg_graph_reroute`, `update_pcg_graph_node_settings`, `set_pcg_graph_node_state`, `create_pcg_graph_parameter`, `delete_pcg_graph_parameter`, `rename_pcg_graph_parameter`, `set_pcg_graph_parameter`, `reset_pcg_graph_parameter_override`, `read_pcg_component_content`, `add_pcg_component_to_actor`, `create_pcg_volume` |
| Niagara System inspection, mutation, and validation | Inspect Niagara Systems and emitter handles, create systems, add or mutate emitter handles, edit common exposed user parameters, and validate compile plus renderer-binding health | `read_niagara_system_content`, `create_niagara_system_asset`, `read_niagara_system_emitter`, `set_niagara_system_user_parameters`, `validate_niagara_system`, `add_niagara_emitter_to_system`, `duplicate_niagara_system_emitter`, `rename_niagara_system_emitter`, `remove_niagara_system_emitter` |
| Blackboard inspection and mutation | Inspect Blackboard parent chains and key metadata, create Blackboard assets, and apply ordered key add, update, rename, and delete operations | `read_blackboard_content`, `create_blackboard_asset`, `update_blackboard_keys` |
| Behavior Tree inspection, authoring, and validation | Inspect Behavior Tree topology and linked Blackboard state, create trees, add or remove constrained subtree nodes, mutate selector and abort settings plus enabled state, and validate runtime plus graph-health issues | `read_behavior_tree_content`, `create_behavior_tree_asset`, `update_behavior_tree_subtree`, `set_behavior_tree_node_properties`, `validate_behavior_tree` |
| AnimBlueprint inspection, authoring, and validation | Inspect AnimBlueprint assets and state machines, create AnimBlueprints, state machines, states, and transitions, bind or tune asset players, and validate compile plus asset-binding health | `read_anim_blueprint_content`, `validate_anim_blueprint`, `create_anim_blueprint_asset`, `read_anim_state_machine`, `create_anim_state_machine`, `create_anim_state`, `rename_anim_state`, `delete_anim_state`, `set_anim_state_sequence_player`, `set_anim_state_blend_space_player`, `set_anim_state_asset_player_parameters`, `create_anim_transition`, `delete_anim_transition`, `set_anim_transition_rule` |

## Detailed Tool List

### Tool discovery and safety

- `get_unreal_tool_categories`: Summarize the live MCP catalog by inferred workflow category, including total tool counts plus read-only and destructive tool counts per category.
- `list_unreal_tools`: List the live MCP catalog with optional filtering by category, substring query, read-only-only, or destructive-only flags so an agent can discover the right tool family before mutating the editor.

### Bridge, editor context, layer membership, layer mutation, selection orchestration, and level basics

- `ping`: Verify that the Unreal Editor bridge is reachable before attempting editor work.
- `get_actors_in_level`: List all actors in the current level with names, classes, and transforms.
- `get_selected_actors`: List the actors currently selected in the Unreal Editor outliner, plus the current map name and selected-count summary.
- `get_level_viewport_info`: Inspect the open level-editor viewports, including camera location, rotation, FOV, and realtime/perspective flags for each viewport client.
- `get_world_partition_info`: Inspect whether the current editor world uses World Partition, whether the world-partition subsystem is available, whether streaming is complete, and which `UWorldPartition` object is currently attached.
- `get_all_layers`: List every known editor layer with deterministic layer-name ordering, current visibility state, and per-layer actor counts.
- `create_layer`: Create one new empty editor layer by name and return deterministic readback for the new visible layer.
- `rename_layer`: Rename one existing editor layer to a new unique name and return deterministic readback for the renamed layer.
- `delete_layer`: Delete one existing editor layer, remove it from any actors that used it, and return a snapshot of the removed actor membership.
- `get_actor_layers`: List the editor layer names currently assigned to one actor, returning the actor metadata plus a deterministic `layers` array and `layer_count` summary.
- `get_actors_in_layer`: List the actors currently assigned to one named editor layer, returning actor metadata plus an `actor_count` summary and a clear missing-layer error when the layer does not exist.
- `add_actor_to_layer`: Add one actor to one editor layer, creating the named layer automatically when it does not already exist, and return deterministic post-mutation layer readback.
- `remove_actor_from_layer`: Remove one actor from one editor layer and return deterministic post-mutation layer readback, with a clean no-op when the actor is already absent from that layer.
- `add_selected_actors_to_layer`: Add the current editor selection to one editor layer, preserving selection and returning deterministic selected-count plus layer-membership summaries.
- `remove_selected_actors_from_layer`: Remove the current editor selection from one existing editor layer, preserving selection and returning deterministic selected-count plus layer-membership summaries.
- `select_actors_in_layer`: Select every actor assigned to one named editor layer, either replacing the current selection or appending to it, and return deterministic post-mutation selection readback.
- `deselect_actors_in_layer`: Deselect every actor assigned to one named editor layer while preserving any other selected actors, and return deterministic post-mutation selection readback.
- `set_layer_visibility`: Set one existing editor layer visible or hidden and return deterministic pre/post visibility readback plus the updated layer catalog entry.
- `toggle_layer_visibility`: Toggle one existing editor layer between visible and hidden and return deterministic pre/post visibility readback plus the updated layer catalog entry.
- `make_all_layers_visible`: Set every known editor layer visible and return the updated layer catalog summary with hidden-layer counts before and after the restore.
- `select_actors`: Select actors in the Unreal Editor outliner by name or label, either replacing the current selection or appending to it.
- `clear_actor_selection`: Clear the current Unreal Editor actor selection and report how many actors were previously selected.
- `find_actors_by_name`: Find actors by label using wildcard matching.
- `spawn_actor`: Spawn a native actor class into the current level.
- `delete_actor`: Delete an actor from the current level by name.
- `set_actor_transform`: Move, rotate, or scale an existing actor.

### Editor save, history, screenshot, and placement helpers

- `save_level`: Save dirty map packages for the current editor world and return the resolved current level plus package identity.
- `undo_last_action`: Undo the most recent editor transaction and report whether an undo was actually applied.
- `redo_last_action`: Redo the most recently undone editor transaction and report whether a redo was actually applied.
- `capture_viewport_screenshot`: Capture the active level viewport to a PNG file, creating the destination directory when needed and returning the saved path plus image dimensions.
- `place_in_grid`: Spawn supported native actor types in a deterministic indexed grid pattern with shared static-mesh, rotation, and scale options.
- `place_in_circle`: Spawn supported native actor types around a deterministic circle layout with configurable radius and starting angle.
- `place_along_spline`: Spawn supported native actor types at evenly spaced distances along a spline actor's first spline component, with optional yaw-only or full spline orientation.
- `scatter_in_area`: Spawn supported native actor types at deterministic pseudo-random positions inside a boxed or elliptical area using an explicit seed.

### Blueprint asset authoring

- `create_blueprint`: Create a new Blueprint asset from a chosen parent class.
- `compile_blueprint`: Compile a Blueprint after graph or component edits.
- `add_component_to_blueprint`: Add a component such as a mesh, light, or camera to a Blueprint.
- `create_blueprint_function`: Create a new user-defined Blueprint function graph.
- `get_available_materials`: Browse material assets visible to the project.

### Blueprint inspection

- `read_blueprint_content`: Dump the full Blueprint structure including components, variables, functions, and parent class.
- `analyze_blueprint_graph`: List nodes and connections in the event graph so edits can target real node IDs.
- `get_blueprint_variable_details`: Inspect one variable's type, default value, and flags.
- `get_blueprint_function_details`: Inspect one function's inputs, outputs, and graph contents.

### Widget Blueprint authoring and inspection

- `create_widget_blueprint`: Create a Widget Blueprint asset with a chosen parent class and deterministic root panel widget. It can also seed one default child widget under the root panel when you need a real slot instance for inspection or testing.
- `read_widget_blueprint_content`: Inspect the source widget tree, bindings, animations, named-slot content, and common slot metadata for a Widget Blueprint asset, including Canvas, box/content, grid, wrap, stack-box, scale-box, uniform-grid, widget-switcher, window-title-bar-area, and safe-zone slot families where they appear in the source tree.
- `add_widget_to_widget_blueprint`: Add a new widget to a Widget Blueprint source tree under a panel parent or named-slot host.
- `remove_widget_from_widget_blueprint`: Remove a widget and its subtree from a Widget Blueprint source tree.
- `reparent_widget_in_widget_blueprint`: Move an existing widget from one parent to another inside the source tree.
- `set_widget_slot_layout_in_widget_blueprint`: Mutate supported slot layout fields for an existing widget in the source tree. The completed 9c-4 slice covers every slot family currently serialized by `read_widget_blueprint_content`: Canvas slots (`anchors`, `offsets`, `size`, `alignment`, `z_order`), Grid slots (`padding`, `horizontal_alignment`, `vertical_alignment`, `row`, `row_span`, `column`, `column_span`, `layer`, `nudge`), UniformGrid slots (`horizontal_alignment`, `vertical_alignment`, `row`, `column`), HorizontalBox/VerticalBox/ScrollBox/StackBox slots (`padding`, `horizontal_alignment`, `vertical_alignment`, `child_size`), WrapBox slots (`padding`, `horizontal_alignment`, `vertical_alignment`, `fill_empty_space`, `force_new_line`, `fill_span_when_less_than`), SafeZone slots (`padding`, `horizontal_alignment`, `vertical_alignment`, `safe_area_scale`, `is_title_safe`), ScaleBox slots (`horizontal_alignment`, `vertical_alignment`), and BackgroundBlur/Border/Button/Overlay/SizeBox/WidgetSwitcher/WindowTitleBarArea slots (`padding`, `horizontal_alignment`, `vertical_alignment`).
- `set_widget_property_binding_in_widget_blueprint`: Create or replace a widget property or event binding inside a Widget Blueprint source asset. The completed 9c-5 slice supports both function-based bindings and source-property/property-path bindings backed by Blueprint member variables.
- `remove_widget_property_binding_from_widget_blueprint`: Remove an existing widget property or event binding from a Widget Blueprint source asset using the target widget name plus property/event name.
- `create_widget_animation_in_widget_blueprint`: Create a new Widget Blueprint animation with a fresh `MovieScene` under the source asset and return the created animation metadata.
- `remove_widget_animation_from_widget_blueprint`: Remove an existing Widget Blueprint animation from the source asset by name.

### Level Sequence authoring and inspection

- `create_level_sequence`: Create a new Level Sequence asset in a chosen content folder.
- `read_level_sequence_content`: Inspect a Level Sequence's MovieScene summary, including playback metadata, bindings, tracks, and sections.
- `add_camera_cut_track_to_level_sequence`: Add the sequence's camera-cut master track if it does not already exist.
- `add_actor_possessable_to_level_sequence`: Add an actor possessable binding to a sequence if that actor is not already bound.
- `add_track_to_binding_in_level_sequence`: Add one bound track class to an existing sequence binding when it is not already present.
- `add_float_key_to_binding_track_in_level_sequence`: Add or update one float key on a bound float property track in a Level Sequence.
- `set_level_sequence_playback_range`: Set the inclusive-start and exclusive-end playback range of a Level Sequence.
- `add_master_track_to_level_sequence`: Add an unbound master track class to a sequence when it is not already present.
- `add_section_to_master_track_in_level_sequence`: Add one section with an explicit frame range to an existing unbound master track.
- `set_section_range_in_master_track_in_level_sequence`: Update one existing master-track section's frame range by section index.
- `remove_section_from_master_track_in_level_sequence`: Remove one existing master-track section by index.

### Blueprint graph authoring

- `paste_blueprint_graph`: Paste complete T3D graph text with pre-flight validation and post-paste verification.
- `list_t3d_snippets`: List curated reusable T3D snippets.
- `get_t3d_snippet`: Fetch one snippet with fresh GUID rewriting for safe paste operations.
- `add_blueprint_node`: Add a single node to a Blueprint graph and return its identifiers.
- `connect_blueprint_nodes`: Connect one node pin to another.
- `delete_blueprint_node`: Delete a node from a Blueprint graph.
- `add_event_node`: Add an event entry node such as `ReceiveBeginPlay`.
- `set_node_default`: Set the default value of an input pin on an existing node.
- `create_blueprint_variable`: Create a member variable on a Blueprint.
- `set_blueprint_variable_properties`: Update Blueprint variable metadata, flags, ranges, replication settings, and default values.
- `add_blueprint_function_input`: Add one input parameter to an existing Blueprint function.
- `add_blueprint_function_output`: Add one output parameter to an existing Blueprint function.
- `delete_blueprint_function`: Delete a user-defined Blueprint function graph.
- `rename_blueprint_function`: Rename a user-defined Blueprint function graph.

### Blueprint spawning, mesh, physics, and materials

- `spawn_blueprint_actor`: Spawn an instance of a compiled Blueprint into the current editor world.
- `set_physics_properties`: Configure physics settings on a Blueprint component.
- `set_static_mesh_properties`: Assign a static mesh and optional material to a Blueprint mesh component.
- `set_mesh_material_color`: Create or reuse a material path and tint a component by parameter.
- `apply_material_to_actor`: Apply a material asset to a placed actor.
- `apply_material_to_blueprint`: Apply a material asset to a component inside a Blueprint.
- `get_actor_material_info`: Inspect actor material slots.
- `get_blueprint_material_info`: Inspect Blueprint component material slots.

### Asset registry and import

- `find_assets`: Search the asset registry by name, class, path, and recursion settings.
- `get_asset_dependencies`: Return packages referenced by an asset.
- `get_referencers`: Return packages that reference a target asset.
- `create_material_instance`: Create a material instance with scalar/vector parameter overrides.
- `import_asset`: Import a source file such as a mesh or texture into a content folder.

### DataTable and CurveTable inspection and mutation

- `read_data_table_content`: Read a DataTable asset's row struct, column metadata, row names, and exported row data.
- `read_data_table_row`: Read one DataTable row by name, including the exported row payload and resolved key field.
- `read_curve_table_content`: Read a CurveTable asset's curve mode, row names, and exported curve payloads.
- `read_curve_table_row`: Read one CurveTable row by name, including the exported row payload.
- `create_curve_table_asset`: Create a new empty CurveTable asset for an explicit simple or rich curve-table mode.
- `upsert_curve_table_row`: Create or replace one CurveTable row using numeric time/value JSON fields.
- `delete_curve_table_row`: Delete one CurveTable row by name and return deterministic post-delete table state.
- `rename_curve_table_row`: Rename one CurveTable row by name and return deterministic post-rename row state.
- `validate_data_table_row_import`: Validate DataTable row import payload shape without mutating the asset.
- `validate_curve_table_row_import`: Validate CurveTable row import payload shape without mutating the asset.
- `create_data_table_asset`: Create a new empty DataTable asset for a given row struct.
- `upsert_data_table_row`: Create or replace one DataTable row using a JSON-compatible row payload.
- `delete_data_table_row`: Delete one DataTable row by name and return deterministic post-delete table state.
- `rename_data_table_row`: Rename one DataTable row by name and return deterministic post-rename row state.
- `duplicate_data_table_row`: Duplicate one DataTable row and return deterministic readback for the new row.
- `move_data_table_row`: Move one DataTable row up or down and return deterministic post-move table order.

### Material graph inspection and authoring

- `create_material_asset`: Create a new blank base material asset in a content folder.
- `create_material_function_asset`: Create a new blank material function asset in a content folder.
- `get_material_expressions`: List the expressions in a material or material-function graph with class names, GUIDs, editor positions, and pin-name summaries.
- `get_material_connections`: List expression-to-expression links and, for base materials, the material output properties currently fed by the graph.
- `get_material_parameters`: List exposed parameter identities on a material or material instance, including current default values, override state, and common editor metadata when available.
- `set_material_parameters`: Update scalar, vector, texture, or static-switch default values on parameter expressions in a base material graph.
- `set_material_instance_parameters`: Update scalar, vector, texture, or static-switch parameter overrides on an existing material instance constant.
- `validate_material_graph`: Analyze a material or material-function graph for missing outputs, dead-end expressions, and parameter identity collisions where that metadata is available.
- `create_material_expression`: Add a material expression node to a base material or material-function graph.
- `delete_material_expression`: Delete a material expression by GUID or node name.
- `delete_material_expressions`: Delete multiple expressions in one preflighted bulk operation.
- `replace_material_expression`: Replace one material expression with a new class while reconnecting compatible graph edges and base-material outputs.
- `connect_material_expressions`: Connect one expression output to another expression input.
- `disconnect_material_expressions`: Disconnect one expression input from its current source expression.
- `connect_material_property`: Bind an expression output to a base-material output property such as `BaseColor`.
- `disconnect_material_property`: Disconnect the current source expression from a base-material output property such as `BaseColor`.
- `recompile_material`: Recompile a material or update a material function after graph edits.
- `layout_material_expressions`: Auto-layout a material or material-function graph after edits.

### Level design helpers

- `snap_actors_to_grid`: Snap actor locations and optionally rotations to grid increments.
- `align_actors`: Align multiple actors along a chosen world axis.
- `duplicate_actor`: Duplicate an existing actor with optional rename and offset.
- `focus_viewport`: Move editor viewports to an actor or world location.

### Landscape inspection, weight/height sampling, bounded paint/sculpt mutation, rebuild, creation, and height editing

- `get_landscapes`: List landscape actors with size and transform data.
- `read_landscape_content`: Inspect one landscape in detail, including the selected edit-layer index, per-layer visibility/lock/alpha/brush metadata, used paint layers, deduplicated layer info assets, and target layer names derived from used paint layers.
- `sample_landscape_point`: Sample one world location on a named landscape and return the resolved component metadata, current world-space height, and per-layer weights for every deduplicated paint layer on that landscape.
- `sample_landscape_points`: Sample multiple world locations on a named landscape in one request and return one sampled result per location plus shared target-layer metadata.
- `sample_landscape_grid`: Sample a named landscape over a regular world-space grid defined by one origin plus `step_x`, `step_y`, `count_x`, and `count_y`, returning one sampled result per grid point.
- `sample_landscape_region`: Sample a named landscape over a bounded rectangular region defined by `min_corner`, `max_corner`, `step_x`, and `step_y`, returning the resolved inclusive grid plus one sampled result per generated point.
- `sample_landscape_height_region`: Sample a named landscape over a bounded rectangular region defined by `min_corner`, `max_corner`, `step_x`, and `step_y`, returning a dense `height_rows` raster plus min/max height summaries.
- `sample_landscape_weight_region`: Sample one named target layer over a bounded rectangular region defined by `min_corner`, `max_corner`, `step_x`, and `step_y`, returning normalized `weight_rows` plus min/max summaries for that layer.
- `paint_landscape_layer_region`: Paint one named target layer or the built-in `Visibility` layer over a bounded rectangular region with one uniform weight.
- `sculpt_landscape_height_region`: Sculpt one bounded rectangular region on a named landscape to a uniform world-space height.
- `rebuild_landscape`: Force a full landscape-layer rebuild/update pass after paint or sculpt mutations.
- `create_landscape`: Create a new flat landscape with configurable component layout and optional material.
- `set_landscape_flat_height`: Set a landscape to one uniform world-space height.
- `import_landscape_heightmap`: Import a heightmap file into an existing landscape.

### PCG graph inspection, node catalog, graph editing, and mutation

- `read_pcg_graph_content`: Read one PCG graph or graph-instance asset, including template/library flags, tool metadata, comment-box summaries, node count, and user-parameter summaries.
- `list_pcg_node_types`: List concrete PCG node settings classes, including default titles, aliases, settings categories, and default pin metadata.
- `read_pcg_graph_nodes`: Read every node in one PCG graph asset, including titles, pin metadata, settings classes, position, comments, named-reroute links, and subgraph references where present.
- `read_pcg_graph_node`: Read one PCG graph node by node path, object name, or visible title, including overridable settings metadata plus reroute and subgraph details where present.
- `create_pcg_graph_asset`: Create a PCG graph asset, optionally duplicating a template graph and setting initial template or library-facing metadata.
- `create_pcg_graph_instance`: Create a PCG graph-instance asset that points at an existing parent PCG graph or graph instance.
- `add_pcg_graph_node`: Add one default PCG node to a graph asset by settings class, optionally naming it, positioning it, or assigning a subgraph asset.
- `delete_pcg_graph_node`: Delete one non-input and non-output node from a PCG graph asset by node path, object name, or title.
- `connect_pcg_graph_nodes`: Create one directed edge between two PCG graph nodes using stable node identifiers plus pin labels.
- `disconnect_pcg_graph_nodes`: Remove one directed edge between two PCG graph nodes using stable node identifiers plus pin labels.
- `set_pcg_graph_node_position`: Move one resolved PCG graph node to a new editor position.
- `set_pcg_subgraph_node_asset`: Assign or clear one resolved PCG subgraph node's referenced graph asset.
- `add_pcg_graph_comment`: Create one PCG graph comment box with optional text, layout, color, and bubble settings.
- `update_pcg_graph_comment`: Update one existing PCG graph comment box by comment GUID.
- `delete_pcg_graph_comment`: Delete one existing PCG graph comment box by comment GUID.
- `add_pcg_graph_reroute`: Add one regular reroute, named reroute declaration, or named reroute usage node, including declaration linkage for usage nodes.
- `update_pcg_graph_node_settings`: Apply one partial JSON patch to editable scalar or structured settings on a resolved PCG graph node.
- `set_pcg_graph_node_state`: Set one resolved PCG graph node's generic enabled, debug, or inspect flags.
- `create_pcg_graph_parameter`: Create one scalar PCG graph user parameter on a base graph and optionally assign its initial default value.
- `delete_pcg_graph_parameter`: Delete one PCG graph user parameter from a base graph.
- `rename_pcg_graph_parameter`: Rename one PCG graph user parameter on a base graph.
- `set_pcg_graph_parameter`: Set one supported scalar PCG graph parameter on either a base graph or a graph instance override.
- `reset_pcg_graph_parameter_override`: Clear one graph-instance parameter override and restore the inherited parent-graph value.
- `read_pcg_component_content`: Read one actor-owned PCG component, including assigned graph, generation trigger, partitioning, scheduling-policy, and owner transform context.
- `add_pcg_component_to_actor`: Attach or reuse a PCG component on an existing actor and optionally assign its graph, trigger mode, partitioning, activation, and seed.
- `create_pcg_volume`: Create a PCG volume actor, then optionally configure its default PCG component with graph assignment and basic generation settings.

### Niagara System inspection, mutation, and validation

- `read_niagara_system_content`: Read one Niagara System asset's emitter handles, exposed user parameters, renderers, and compile-status metadata.
- `create_niagara_system_asset`: Create a Niagara System asset from an empty baseline or an existing template system.
- `read_niagara_system_emitter`: Read one Niagara System emitter handle by handle ID or emitter name.
- `set_niagara_system_user_parameters`: Set common Niagara System exposed user-parameter defaults for float, bool, vector, color, and object-backed values.
- `validate_niagara_system`: Validate one Niagara System for compile health, object-backed user-parameter references, unresolved renderer user-parameter bindings, and emitter enablement sanity.
- `add_niagara_emitter_to_system`: Add an existing Niagara Emitter asset to a Niagara System and return the created emitter-handle readback.
- `duplicate_niagara_system_emitter`: Duplicate one Niagara System emitter handle by ID or name, optionally renaming the new copy.
- `rename_niagara_system_emitter`: Rename one Niagara System emitter handle by ID or current emitter name.
- `remove_niagara_system_emitter`: Remove one Niagara System emitter handle by ID or emitter name.

### Blackboard inspection and mutation

- `read_blackboard_content`: Read one Blackboard asset's parent chain, key entries, key types, base-class filters, and default metadata.
- `create_blackboard_asset`: Create a Blackboard asset, optionally assigning a parent Blackboard for inherited keys.
- `update_blackboard_keys`: Apply ordered Blackboard key add, update, rename, and delete operations with deterministic readback.

### Behavior Tree inspection, authoring, and validation

- `read_behavior_tree_content`: Read one Behavior Tree asset's linked Blackboard, root composite shape, node topology, and task, decorator, and service summaries.
- `create_behavior_tree_asset`: Create a Behavior Tree asset with a default selector root and optional linked Blackboard assignment.
- `update_behavior_tree_subtree`: Apply ordered add and remove subtree edits for supported Behavior Tree composites, tasks, decorators, and services.
- `set_behavior_tree_node_properties`: Mutate Behavior Tree selector bindings, decorator abort modes, and graph-backed node enabled state by topology path.
- `validate_behavior_tree`: Validate one Behavior Tree asset's runtime structure, selector bindings, abort modes, and graph-health diagnostics.

### AnimBlueprint inspection, authoring, and validation

- `read_anim_blueprint_content`: Read one AnimBlueprint asset's skeleton, preview mesh, layer graphs, and state-machine summaries.
- `validate_anim_blueprint`: Validate one AnimBlueprint for compile and readback health, skeleton compatibility, unresolved player assets, and asset-player binding sanity.
- `create_anim_blueprint_asset`: Create an AnimBlueprint asset for a skeleton and return deterministic inspection readback.
- `read_anim_state_machine`: Read one named state machine from an AnimBlueprint, including state and transition summaries.
- `create_anim_state_machine`: Create a state machine node inside an AnimBlueprint's top-level animation graph.
- `create_anim_state`: Create one state inside a named AnimBlueprint state machine.
- `rename_anim_state`: Rename one existing state inside a named AnimBlueprint state machine.
- `delete_anim_state`: Delete one existing state from a named AnimBlueprint state machine.
- `set_anim_state_sequence_player`: Replace one state's bound graph with a single sequence-player node wired into the state result.
- `set_anim_state_blend_space_player`: Replace one state's bound graph with a single blend-space or aim-offset player wired into the state result.
- `set_anim_state_asset_player_parameters`: Mutate supported settings on a state's existing single asset-player node without rebinding its animation asset.
- `create_anim_transition`: Create one directed transition between two existing states in a named AnimBlueprint state machine.
- `delete_anim_transition`: Delete directed transition(s) between two existing states in a named AnimBlueprint state machine.
- `set_anim_transition_rule`: Apply one supported guard pattern to a named AnimBlueprint transition.

## Practical Workflows Enabled By The Current MCP Surface

1. Build a Blueprint from scratch: create the asset, add components, assign meshes/materials, compile it, and spawn it into the level.
2. Inspect and edit Blueprint logic: read the Blueprint, inspect the graph, then either paste T3D or make surgical node-level changes, including function signatures and variable metadata.
3. Stage a level quickly: inspect or mutate the current editor selection, inspect open viewports, inspect or mutate actor layer membership, then spawn actors, snap and align them, duplicate them, and focus the viewport on the result.
4. Manage project content: search the asset registry, inspect dependency chains, create material instances, and import external assets.
5. Author material graphs and tune instances: create blank materials or material functions, add, bulk-delete, replace, wire, or disconnect expressions and base-material outputs, then update base-material parameter defaults or material-instance overrides, recompile, and auto-layout the result.
6. Inspect and prototype terrain: inspect landscape edit layers and paint-layer metadata, sample world-space heights and layer weights at one point, many arbitrary points, a regular grid, a bounded region, a denser height-only raster, or a target-layer weight raster, then create new landscapes, flatten or reset terrain height, paint one layer over a bounded region, sculpt one bounded region to a target height, rebuild the landscape, and import external heightmaps.
7. Inspect and mutate UMG authoring state: create fresh Widget Blueprint assets, optionally seed a child to force concrete slot instances, dump the editable widget tree, bindings, animations, and common slot layout metadata, then add, remove, reparent, or update supported slot layout fields directly in the source tree.
8. Author cinematics and timing data: create Level Sequence assets, bind actors, add master tracks or camera cuts, key float tracks, retime playback and sections, and inspect the resulting MovieScene structure.
9. Manage structured gameplay data: inspect DataTables and CurveTables, validate import payloads before mutation, create blank assets, and deterministically upsert, rename, duplicate, reorder, or delete rows.
10. Build VFX assets and AI authoring assets: create Niagara Systems, Blackboards, and Behavior Trees, inspect their authored content, mutate emitter handles, user parameters, Blackboard keys, or constrained Behavior Tree subtrees, and validate the results.
11. Author animation state logic: create AnimBlueprints, state machines, states, transitions, and bound asset players, then validate compile health and asset-binding correctness.
12. Bootstrap prompt-driven PCG setups: create PCG graphs or graph instances, inspect their metadata and parameters, attach PCG components to actors, and seed new PCG volumes before deeper graph-authoring waves land.

## Notes

- The tool inventory above reflects the currently exposed MCP tools, not every C++ handler that exists internally.
- This document now tracks the full currently exposed 172-tool MCP surface from `Python/unreal_ai_mcp.py`.
- Both the VS Code MCP path and the in-editor chat panel share the same bridge and the same tool surface.
- The landscape-advanced 9d tranche is now complete: `read_landscape_content`, `sample_landscape_point`, `sample_landscape_points`, `sample_landscape_grid`, `sample_landscape_region`, `sample_landscape_height_region`, `sample_landscape_weight_region`, `paint_landscape_layer_region`, `sculpt_landscape_height_region`, `rebuild_landscape`, `create_landscape`, `set_landscape_flat_height`, and `import_landscape_heightmap`.