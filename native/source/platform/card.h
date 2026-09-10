#ifndef MELEE_NATIVE_CARD_H
#define MELEE_NATIVE_CARD_H

/* Deliver completed CARD operations on the game thread after interrupts
 * resume. Async CARD entry points never call this function themselves. */
void NativeCardPump(void);

#endif
