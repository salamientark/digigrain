/* SPDX-License-Identifier: GPL-2.0-or-later */
/* digigrain: what the machine's files share. */
#ifndef DIGIGRAIN_H
#define DIGIGRAIN_H

/* GRANULAR's machine id: 4-127 (elekloader docs/ADAPTING.md lists the taken ones).
 * Kits store it, so it is fixed for good. 0.0.x used 6 and other knob slots: its kits are not compatible,
 * no migration. Such a track loads as ONESHOT on a set with no machine at id 6, else as that machine. */
#define DIGIGRAIN_MACHINE_ID 10

#endif
