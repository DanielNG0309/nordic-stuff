ipt_swap — init_peripheral (CS initiator + BLE peripheral)
##########################################################

This app is an **anchor** of the role-swapped ``ipt_swap`` reference: it is the BLE **peripheral**
*and* the CS **initiator**. It advertises as ``Nordic CS IPT Initiator``; the central-reflector
(``refl_central``) connects to it and drives the CS setup. This app keeps CS enabled, computes its
own IFFT distance locally from its subevent data (no RAS/GATT transfer), and prints a parser line:

   ``DIST:<metres>,AP:0,SAMPLES:<high-quality-tone-count>``

Note this is the **opposite** BLE-role arrangement to the stock sample (where the
initiator is the central that scans): here the initiator advertises and is connected to.

Distance processing:

* Tones are filtered by the **Tone Quality Indicator** — only HIGH-quality tones feed the IFFT, and
  a procedure is dropped unless at least ``TONE_QI_OK_TONE_COUNT_THRESHOLD`` (15) HIGH tones are
  present (no-op at close, clean range where all tones are HIGH).
* A configurable offset (``CONFIG_APP_CS_DISTANCE_OFFSET_MM``) is subtracted from each estimate to
  compensate the inherent ``cs_de`` near-field bias, then a sliding-window median filter is applied.

See the top-level `ipt_swap README <../README.md>`_ for the full description, build/flash steps,
the multi-anchor variants, measured rates, reliability, and accuracy / offset calibration.
