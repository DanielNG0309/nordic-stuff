/*
 * Battery level from the XIAO's VBAT divider, reported through the Matter Power Source cluster
 * (lock endpoint).
 */

#pragma once

namespace Battery
{

/* Needs the Matter stack initialized (after Nrf::Matter::PrepareServer). A failure is logged and
 * leaves the buzzer running without battery reports.
 */
int Init();

/* The relay coil is drawing current and sags the battery: measure later. */
void NoteLoad();

} /* namespace Battery */
