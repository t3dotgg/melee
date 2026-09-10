# Game object lifecycle

[`gobjplink.c`](../../src/sysdolphin/baselib/gobjplink.c) creates, moves, and
removes `HSD_GObj` records. Each record can own an HSD object, user data,
process callbacks, and a render callback.

## Object list order

`p_link` selects an object list. `next` and `prev` link its members.
`HSD_GObj_Entities` holds the heads, and `plinklow_gobjs` holds the tails.
`HSD_GObjPLinkHead` reads a head, and `HSD_GObjPLinkSlot` returns its address.
The GameCube build casts the declared `HSD_GObjList` view to an array of
`HSD_GObj*` entries indexed by `p_link`. The native build uses its `slots`
array, which has 64 entries.

`CreateGObj` allocates a record and chooses its position with `where`:

| Mode | Placement |
| --- | --- |
| 0 | Search backward from the tail. Insert after the last object with a priority less than or equal to the new priority. |
| 1 | Search forward from the head. Insert before the first object with a priority greater than or equal to the new priority. |
| 2 | Insert after `position`. A null position selects the head. |
| 3 | Insert before `position`, which must be non-null. |

Modes 0 and 1 keep priorities in ascending order when the list is sorted.
They differ in where they place objects with equal priorities.
`GObj_Create` uses mode 0, so a new object follows existing objects with the
same priority. The private helpers name this choice explicitly.

Modes 2 and 3 use the supplied position without a priority check. The caller
must choose a position in the destination list and maintain any required
priority order. `GObj_PReorder` performs only the insertion step. It does not
unlink an existing object first.

## Attachments

The HSD object and user data have separate pointers, kind fields, and removal
paths. A kind value of `0xFF` marks an empty attachment. Setting up one kind
of attachment does not set up the other.

| Function | Work |
| --- | --- |
| `HSD_GObjObject_80390A3C` | Find the first object with a given classifier in one `p_link` list. |
| `HSD_GObjObject_80390A70` | Attach an HSD object to an empty slot. |
| `HSD_GObjObject_80390ADC` | Detach the HSD object and return its pointer. Do not call its registered remover. |
| `HSD_GObjObject_80390B0C` | Call the remover selected by `obj_kind`, then clear the attachment. |
| `GObj_InitUserData` | Attach user data and its removal callback to an empty slot. |
| `GObj_RemoveUserData` | Call the stored callback, then clear the user-data pointer and kind. |

See [`gobjobject.c`](../../src/sysdolphin/baselib/gobjobject.c) and
[`gobjuserdata.c`](../../src/sysdolphin/baselib/gobjuserdata.c).
The user-data removal callback stays stored after removal. The empty kind
prevents a second call, and the next setup replaces the callback.

In [`sobjlib.c`](../../src/sysdolphin/baselib/sobjlib.c),
`HSD_SObjLib_803A44D4` detaches and reattaches the head of an HSD object list
while it changes the list order. Calling the old attachment's remover at that point
could release objects still in use. In
[`tyfigupon.c`](../../src/melee/ty/tyfigupon.c), `_tyFigupon_803152BC` attaches
state when its user-data pointer is null, then removes the state when its
countdown expires. The HSD object remains attached through this operation.

## Moving and removing an owner

`HSD_GObjPLink_ChangeGObjPri_Unk` moves an existing owner using the same
placement modes as `CreateGObj`. It removes the owner's processes from the
scheduler while reversing their `child` list. It then unlinks and reinserts the owner.
Reinserting each process restores the original `child` order and places its
scheduler links according to the owner's new position. See the
[process guide](game-object-processes.md) for the two process lists.

The scheduler in [`gobj.c`](../../src/sysdolphin/baselib/gobj.c) cycles its
traversal tag through 0, 1, and 2. It skips a process whose `flags_3` already
matches the current tag. Owner reordering preserves the current tag, so it
does not cause a process to run twice in the current pass. It replaces tags
that would match the next pass with the previous tag, so the moved processes
do not skip that pass because of an old tag.

`HSD_GObjFree` removes attachments in this order: user data, HSD object,
processes, then render link. It then unlinks the owner and returns
its storage to `gobj_alloc_data`. The private `unlinkObject` helper updates
both neighbors and the head or tail when needed.

A process callback can request removal or movement of its active owner.
These requests set fields in `HSD_GObj_DelayedProcInfo`. The scheduler applies
them after the callback returns. Removal takes precedence over movement and
individual process removal. Keep this order and the process traversal
updates when changing lifecycle code.

## Matching constraints

The allocation wrapper is required. Calling `HSD_ObjAlloc` directly in
`CreateGObj` changes register allocation. The placement switches stay in
both creation and movement because another helper changes which calls the
compiler inlines. The shared unlink helper preserves the compiled code.

The cleanup preserves code, data, symbol records, and relocation records in
the object file. Only generated local names in the ELF string table change.
Use the [build guide](../build-and-run.md) to verify the complete executable
after combining this code with other changes.
