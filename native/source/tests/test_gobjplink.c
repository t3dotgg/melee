#include <assert.h>
#include <stdio.h>

#include <sysdolphin/baselib/gobj.h>

HSD_GObjList* HSD_GObj_Entities;

int main(void)
{
    HSD_GObjList list = { 0 };
    HSD_GObj fighter = { 0 };
    HSD_GObj item = { 0 };
    HSD_GObj effect = { 0 };

    HSD_GObj_Entities = &list;

    fighter.p_link = 8;
    item.p_link = 9;
    effect.p_link = 63;
    fighter.next = &item;
    item.prev = &fighter;

    *HSD_GObjPLinkSlot(fighter.p_link) = &fighter;
    *HSD_GObjPLinkSlot(effect.p_link) = &effect;

    assert(HSD_GObjPLinkHead(8) == &fighter);
    assert(HSD_GObjPLinkHead(63) == &effect);
    assert(list.fighters == &fighter);
    assert(list.items == NULL);
    assert(list.slots[8] == list.fighters);
    assert(HSD_GObjPLinkSlot(9) == &list.slots[9]);
    assert(HSD_GObjPLinkSlot(63) == &list.slots[63]);

    *HSD_GObjPLinkSlot(item.p_link) = &item;
    assert(list.items == &item);
    assert(HSD_GObjPLinkHead(9)->prev == &fighter);
    puts("Native p_link slot test passed");
    return 0;
}
