"""Index of T3D snippets shipped with UnrealAI.

`name`           : tool argument the model passes to `get_t3d_snippet`.
`description`    : 1-line summary surfaced by `list_t3d_snippets`.
`exec_in_pin`    : pin name on this snippet's exec input  (None if pure pass-through / event).
`exec_out_pins`  : pin names on this snippet's exec outputs (empty list for pure-data nodes).
`data_pins`     : non-exec output pins worth wiring (for hint text only).
`tags`           : free-form for future filtering.

Every snippet's NodeGuid + every PinId is rewritten with a fresh GUID on each
`get_t3d_snippet` call so consecutive calls return pasteable, non-colliding T3D.
"""

SNIPPETS = [
    {
        "name": "event_begin_play",
        "description": "BeginPlay event node. Use as a graph entry point.",
        "file": "event_begin_play.t3d",
        "exec_in_pin": None,
        "exec_out_pins": ["then"],
        "data_pins": [],
        "tags": ["event", "entry"],
    },
    {
        "name": "event_tick",
        "description": "Tick event with DeltaSeconds output. Runs every frame.",
        "file": "event_tick.t3d",
        "exec_in_pin": None,
        "exec_out_pins": ["then"],
        "data_pins": ["DeltaSeconds (float)"],
        "tags": ["event", "entry"],
    },
    {
        "name": "print_string",
        "description": "PrintString call — prints to screen + log. InString default 'Hello'.",
        "file": "print_string.t3d",
        "exec_in_pin": "execute",
        "exec_out_pins": ["then"],
        "data_pins": [],
        "tags": ["debug", "function_call"],
    },
    {
        "name": "branch",
        "description": "If/Then/Else branch. Drives 'then' if Condition is true, else 'else'.",
        "file": "branch.t3d",
        "exec_in_pin": "execute",
        "exec_out_pins": ["then", "else"],
        "data_pins": [],
        "tags": ["control_flow"],
    },
    {
        "name": "sequence",
        "description": "Execution sequence with two outputs (then_0, then_1). Add more pins manually if needed.",
        "file": "sequence.t3d",
        "exec_in_pin": "execute",
        "exec_out_pins": ["then_0", "then_1"],
        "data_pins": [],
        "tags": ["control_flow"],
    },
    {
        "name": "get_variable",
        "description": "Get a member variable (defaults to 'MyVariable' bool — rename in VariableReference + the output pin name to match the actual variable).",
        "file": "get_variable.t3d",
        "exec_in_pin": None,
        "exec_out_pins": [],
        "data_pins": ["MyVariable"],
        "tags": ["variable"],
    },
    {
        "name": "set_variable",
        "description": "Set a member variable (defaults to 'MyVariable' bool — rename in VariableReference + the input pin name to match the actual variable).",
        "file": "set_variable.t3d",
        "exec_in_pin": "execute",
        "exec_out_pins": ["then"],
        "data_pins": ["Output_Get"],
        "tags": ["variable"],
    },
    {
        "name": "cast",
        "description": "Dynamic cast to /Script/Engine.Actor (rename TargetType + the AsActor output pin name to cast to a different class).",
        "file": "cast.t3d",
        "exec_in_pin": "execute",
        "exec_out_pins": ["then", "CastFailed"],
        "data_pins": ["AsActor", "bSuccess"],
        "tags": ["control_flow", "cast"],
    },
    {
        "name": "call_function",
        "description": "Generic K2Node_CallFunction template (defaults to GetGameTimeInSeconds — change MemberParent + MemberName + ReturnValue pin type for any other function).",
        "file": "call_function.t3d",
        "exec_in_pin": "execute",
        "exec_out_pins": ["then"],
        "data_pins": ["ReturnValue"],
        "tags": ["function_call"],
    },
    {
        "name": "for_loop",
        "description": "ForLoop macro instance (FirstIndex=0, LastIndex=10). Wire LoopBody into the per-iteration body, Completed into the post-loop branch.",
        "file": "for_loop.t3d",
        "exec_in_pin": "Exec",
        "exec_out_pins": ["LoopBody", "Completed"],
        "data_pins": ["Index (int)"],
        "tags": ["control_flow", "loop"],
    },
]
