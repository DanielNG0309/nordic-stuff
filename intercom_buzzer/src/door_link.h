/*
 * "Buzz + door": a signed Thread UDP message that tells the apartment deadbolt to unlock. Both
 * devices are on the same Thread network, so no hub or controller is in the path.
 */

#pragma once

namespace DoorLink
{

/* Needs the Thread stack up (after Nrf::Matter::StartServer). A failure is logged and leaves the
 * plain buzz working.
 */
int Init();

/* Signs the next counter value and sends it, from the Matter thread or the app task. */
void SendUnlock();

} /* namespace DoorLink */
