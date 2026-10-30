/*
 * Message: "BZD1", a big-endian 32-bit counter, then the first 16 bytes of HMAC-SHA256 over those
 * 8 bytes, keyed with CONFIG_DOOR_LINK_KEY. The deadbolt only accepts a counter higher than the
 * last one it saw, so a captured message can't be replayed.
 *
 * It goes to the Realm-Local All Thread Nodes address, the multicast a parent queues for its
 * sleepy children (plain ff03::1 doesn't reach them). No address to look up or keep in step, and
 * the deadbolt receives it at its next poll. Nothing confirms delivery, so it's sent twice.
 */

#include "door_link.h"

#include <crypto/CHIPCryptoPAL.h>
#include <lib/support/CodeUtils.h>
#include <platform/CHIPDeviceLayer.h>

#include <openthread/message.h>
#include <openthread/thread.h>
#include <openthread/udp.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include <cerrno>
#include <cstring>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

using namespace ::chip;
using namespace ::chip::DeviceLayer;

namespace
{
constexpr uint16_t kPort = 49513;
constexpr uint8_t kMagic[4] = { 'B', 'Z', 'D', '1' };
constexpr size_t kKeySize = 32;
constexpr size_t kTagSize = 16;
constexpr char kCounterKey[] = "doorlink/tx";

struct Packet {
	uint8_t mMagic[sizeof(kMagic)];
	uint8_t mCounter[4];
	uint8_t mTag[kTagSize];
} __packed;

BUILD_ASSERT(sizeof(CONFIG_DOOR_LINK_KEY) - 1 == kKeySize * 2, "CONFIG_DOOR_LINK_KEY must be 64 hex digits");

uint8_t sKey[kKeySize];
uint32_t sCounter = CONFIG_DOOR_LINK_COUNTER_MIN;
Packet sPacket;
otUdpSocket sSocket;
bool sReady;
struct k_work_delayable sRepeatWork;

int CounterLoaded(const char *key, size_t len, settings_read_cb read, void *arg, void * /* param */)
{
	uint32_t saved;

	if (key == nullptr && len == sizeof(saved) && read(arg, &saved, sizeof(saved)) == sizeof(saved)) {
		sCounter = MAX(sCounter, saved);
	}

	return 0;
}

/* Nothing is sent to this socket. */
void IgnoreReceive(void *, otMessage *, const otMessageInfo *) {}

void Send()
{
	ThreadStackMgr().LockThreadStack();

	otInstance *instance = ThreadStackMgrImpl().OTInstance();
	otMessage *message = otUdpNewMessage(instance, nullptr);
	otError err = OT_ERROR_NO_BUFS;

	if (message != nullptr) {
		err = otMessageAppend(message, &sPacket, sizeof(sPacket));
		if (err == OT_ERROR_NONE) {
			otMessageInfo info{};

			info.mPeerAddr = *otThreadGetRealmLocalAllThreadNodesMulticastAddress(instance);
			info.mPeerPort = kPort;
			err = otUdpSend(instance, &sSocket, message, &info);
		}
		if (err != OT_ERROR_NONE) {
			otMessageFree(message);
		}
	}

	ThreadStackMgr().UnlockThreadStack();

	if (err != OT_ERROR_NONE) {
		LOG_ERR("Door unlock not sent: %d", err);
	}
}

void RepeatHandler(struct k_work *)
{
	Send();
}
} /* namespace */

namespace DoorLink
{

int Init()
{
	if (hex2bin(CONFIG_DOOR_LINK_KEY, kKeySize * 2, sKey, sizeof(sKey)) != sizeof(sKey)) {
		LOG_ERR("CONFIG_DOOR_LINK_KEY is not hex");
		return -EINVAL;
	}

	settings_load_subtree_direct(kCounterKey, CounterLoaded, nullptr);
	k_work_init_delayable(&sRepeatWork, RepeatHandler);

	otSockAddr addr{};

	ThreadStackMgr().LockThreadStack();
	otInstance *instance = ThreadStackMgrImpl().OTInstance();
	otError err = otUdpOpen(instance, &sSocket, IgnoreReceive, nullptr);
	if (err == OT_ERROR_NONE) {
		err = otUdpBind(instance, &sSocket, &addr, OT_NETIF_THREAD);
	}
	ThreadStackMgr().UnlockThreadStack();

	if (err != OT_ERROR_NONE) {
		LOG_ERR("Door link socket: %d", err);
		return -EIO;
	}

	sReady = true;
	LOG_INF("Door link ready, counter %u", sCounter);
	return 0;
}

void SendUnlock()
{
	VerifyOrReturn(sReady);

	/* Saved first: a counter must never go out twice with different meanings after a reset. */
	const uint32_t counter = sCounter + 1;
	const int ret = settings_save_one(kCounterKey, &counter, sizeof(counter));

	if (ret) {
		LOG_ERR("Door link counter not saved: %d", ret);
		return;
	}
	sCounter = counter;

	uint8_t tag[Crypto::kSHA256_Hash_Length];
	Crypto::HMAC_sha hmac;

	memcpy(sPacket.mMagic, kMagic, sizeof(kMagic));
	sys_put_be32(counter, sPacket.mCounter);

	const CHIP_ERROR err = hmac.HMAC_SHA256(sKey, sizeof(sKey), reinterpret_cast<const uint8_t *>(&sPacket),
						offsetof(Packet, mTag), tag, sizeof(tag));

	if (err != CHIP_NO_ERROR) {
		LOG_ERR("Door link HMAC: %" CHIP_ERROR_FORMAT, err.Format());
		return;
	}
	memcpy(sPacket.mTag, tag, sizeof(sPacket.mTag));

	LOG_INF("Door unlock %u", counter);
	Send();
	k_work_schedule(&sRepeatWork, K_SECONDS(1));
}

} /* namespace DoorLink */
