#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Include the implementation to test its private sort lists. */
#include <assert.h>

#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/displayfunc.c>

_Static_assert(sizeof(((HSD_JObj*) 0)->id) == sizeof(void*),
               "Joint IDs must retain the full host pointer");
_Static_assert(sizeof(((HSD_AObjDesc*) 0)->obj_id) == sizeof(void*),
               "Animation object IDs must retain the full host pointer");

int main(void)
{
    HSD_ZList nodes[5] = { 0 };
    const float depths[] = { -2.0f, 6.0f, -7.0f, -2.0f, 0.5f };
    const size_t texedge_order[] = { 2, 0, 3, 4, 1 };
    const size_t xlu_order[] = { 0, 3, 4, 1 };
    HSD_ZList* current;
    size_t i;

    for (i = 0; i < 5; i++) {
        nodes[i].pmtx[2][3] = depths[i];
        nodes[i].next = i + 1 < 5 ? &nodes[i + 1] : NULL;
        nodes[i].sort.texedge = nodes[i].next;
    }
    nodes[0].sort.xlu = &nodes[1];
    nodes[1].sort.xlu = &nodes[3];
    nodes[3].sort.xlu = &nodes[4];
    zlist_texedge_top = &nodes[0];
    zlist_texedge_nb = 5;
    zlist_xlu_top = &nodes[0];
    zlist_xlu_nb = 4;
    zsort_sorting = 1;

    _HSD_ZListSort();

    current = zlist_texedge_top;
    for (i = 0; i < 5; i++) {
        assert(current == &nodes[texedge_order[i]]);
        current = current->sort.texedge;
    }
    assert(current == NULL);
    current = zlist_xlu_top;
    for (i = 0; i < 4; i++) {
        assert(current == &nodes[xlu_order[i]]);
        current = current->sort.xlu;
    }
    assert(current == NULL);
    for (i = 0; i < 5; i++) {
        assert(nodes[i].next == (i + 1 < 5 ? &nodes[i + 1] : NULL));
        assert(nodes[i].pmtx[2][3] == depths[i]);
    }

    zlist_texedge_top = NULL;
    zlist_texedge_nb = 0;
    zlist_xlu_top = &nodes[0];
    zlist_xlu_nb = 1;
    _HSD_ZListSort();
    assert(zlist_texedge_top == NULL);
    assert(zlist_xlu_top == &nodes[0]);
    assert(nodes[0].sort.xlu == NULL);
    puts("scene sort tests passed");
    return 0;
}
