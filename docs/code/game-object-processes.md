# Game object processes

A process record binds a callback to an `HSD_GObj`. The implementation is in
[`gobjproc.c`](../../src/sysdolphin/baselib/gobjproc.c). The scheduler in
[`gobj.c`](../../src/sysdolphin/baselib/gobj.c) invokes these callbacks through
`HSD_GObj_80390CFC`.

## Two linked lists

Each process belongs to two lists at once:

| Links | Purpose |
| --- | --- |
| `gobj->proc` and `process->child` | All processes owned by one game object. A new process goes at the head. |
| `process->next` and `process->prev` | Scheduled processes with one `s_link` priority, across game objects. |

`s_link` comes from the `pri` argument to `HSD_GObj_SetupProc`. The scheduler
visits these priorities in ascending order. Within one priority, processes
follow their owners' object-list order, with `p_link` groups in ascending order.

The scheduler uses two tables allocated in
[`gobjinit.c`](../../src/sysdolphin/baselib/gobjinit.c):

- `HSD_GObj_GObjProcHead[s_link]` is the head of a scheduler list.
- `HSD_GObj_ProcList` stores the last process for each `p_link` and `s_link` pair.
  The private `processTailSlot` helper selects this table entry.

The tail-table index is `p_link + s_link * (p_link_max + 1)`.
An empty pair has a null entry.

## Insertion and removal

`HSD_GObjProc_QueueProc` first looks for a predecessor at the same `s_link`
on the owner or preceding objects. If none exists, it checks the tails of lower
`p_link` groups. It inserts after the predecessor, or at the scheduler head
if no predecessor exists. Finally, it adds the process to the owner's list.

The insertion code has separate labels for inserting after a predecessor and
updating the owner links. These replace the old `if (true)` block whose `else`
was entered by a `goto`.

| Function | Work |
| --- | --- |
| `HSD_GObj_SetupProc` | Allocate a process, set its callback and priority, and insert it into both lists. |
| `HSD_GObjProc_UnqueueProc` | Unlink from the scheduler list and update its tail-table entry. Keep the owner list intact. |
| `HSD_GObjProc_UnlinkProcFromGObj` | Unlink from both lists. |
| `HSD_GObjProc_RemoveProc` | Request process removal and return its storage to the object allocator when removal is allowed. |
| `HSD_GObjProc_RemoveAllProcs` | Request removal of every process owned by a game object. |

`HSD_GObjPLink_ChangeGObjPri_Unk` in
[`gobjplink.c`](../../src/sysdolphin/baselib/gobjplink.c) uses scheduler-only
unlinking when it moves an owner between object lists. It then reinserts the
owner's processes. Keep the distinction between scheduler-only removal and
removal from both lists.

## Changes during a callback

The scheduler stores the active process in `HSD_GObj_CurrentInvokedProc`
and the next process in `HSD_GObj_NextInvokedProc`. It stores the active owner in
`HSD_GObj_CurrentInvokedProcGObj`.

A callback can request its own removal. `HSD_GObjProc_RemoveProc` records
this in `HSD_GObj_DelayedProcInfo.delay_remove_proc` and leaves the process
allocated until the callback returns. Owner removal and owner reordering
have similar deferred paths in `gobjplink.c`.

After the callback returns, the scheduler sets
`HSD_GObj_DelayedProcInfo.in_delayed_proc` while it applies the pending changes.
During this phase, process insertion and removal can repair the saved
next-process pointer. Removing that logic can leave the
traversal pointing at a freed process or skip a newly inserted one.

The scheduler also uses `flags_3` with a tag that cycles through zero, one,
and two. It marks a process before checking whether its callback can run.
Owner reordering adjusts these tags when reinserting processes. The tag
comparisons belong to traversal control and must stay with the list updates.

## Matching checks

The typed tail-table helper and explicit insertion labels preserve the
compiled code, data, symbol records, and relocation records. The compiler changes
three generated local names in the ELF string table. Their target sections
and offsets stay identical.

Keep `gproc` and `pri` in their assertion expressions. Those expressions are
stored as strings in the executable. Validate the final executable with the
[build guide](../build-and-run.md) after integrating process changes.
