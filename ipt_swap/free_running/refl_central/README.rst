ipt_swap - refl_central (CS reflector + BLE central)
####################################################

This app is the **central-reflector** of the role-swapped ``ipt_swap`` reference: it is the BLE
**central** *and* the CS **reflector**. It scans for and connects to the peripheral CS initiators
(``init_peripheral``), drives the full CS setup for each link (including enabling IPT), and runs
the multi-anchor scheduler with auto-reconnect.

Note this is the **opposite** BLE-role arrangement to the stock ``ipt_reflector`` sample (where the
reflector is a peripheral that advertises): here the reflector scans and connects.

See the top-level `ipt_swap README <../README.md>`_ for the full description, build/flash steps,
the round-robin vs free-running variants, measured rates, reliability, and accuracy / offset notes.
