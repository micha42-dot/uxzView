// SPDX-License-Identifier: GPL-3.0-or-later
extern int ix_network_probe(void);
int ix_module_probe(void) { return ix_network_probe(); }
