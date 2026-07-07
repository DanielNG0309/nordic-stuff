#pragma once 

#define ID 0
#define PRINT_VERBOSE 0
#define PRINT_TIME 0
/* Median window over recent estimates. Smaller = less lag tracking a moving
 * target (the reflector moves), larger = more outlier/multipath rejection. At
 * ~2 Hz/anchor, 5 samples ≈ ~1-2 s of history. Tune for target speed. */
#define DE_SLIDING_WINDOW_SIZE (5)
#define MAX_AP (CONFIG_BT_RAS_MAX_ANTENNA_PATHS)
#define REFLECTOR_NAME "9999"
#define INITIATOR_NAME "0000"
#define BUILD_INITIATOR CONFIG_CS_BUILD_INITIATOR
#define BUILD_REFLECTOR CONFIG_CS_BUILD_REFLECTOR
#define PROCEDURE_COUNTER_NONE (-1)

#define USE_PSEUDO 0 // Set to 1 to enable pseudo-data, 0 to disable
#define PSEUDO_INJECTIONS_COUNT 1 // Number of pseudo-data injections between real data points

#define LOCAL_PROCEDURE_MEM                                                                        \
	((BT_RAS_MAX_STEPS_PER_PROCEDURE * sizeof(struct bt_le_cs_subevent_step)) +                \
	 (BT_RAS_MAX_STEPS_PER_PROCEDURE * BT_RAS_MAX_STEP_DATA_LEN))

#define TRY_BASE(func, ...) \
    do { \
        int err = func; \
        if (err) { \
            __VA_ARGS__; \
        } \
    } while(0)

#define TRY(func) \
    TRY_BASE(func, printk("Failed to run %s: (error %d)\n", #func, err))

#define TRY_GOTO(func, place) \
    TRY_BASE(func, goto place)

#define TRY_RETURN(func) \
    TRY_BASE(func, \
        printk("Failed to run %s: (error %d)\n", #func, err); \
        return err)

