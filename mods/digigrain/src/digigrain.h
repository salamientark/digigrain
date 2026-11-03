/* SPDX-License-Identifier: GPL-2.0-or-later */
/* digigrain: what the machine's files share. */
#ifndef DIGIGRAIN_H
#define DIGIGRAIN_H

/* GRANULAR's machine id: 4-127, claimed as machine:10 (elekloader docs/ADAPTING.md lists the taken ones).
 * Kits store it, so it is fixed for good. 0.0.x used 6, which Digi Poly's POLY also takes; a kit saved
 * with a 0.0.x GRANULAR track loads it as ONESHOT. */
#define DIGIGRAIN_MACHINE_ID 10

#endif
