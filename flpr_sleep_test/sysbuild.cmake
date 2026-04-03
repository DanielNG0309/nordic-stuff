ExternalZephyrProject_Add(
  APPLICATION remote_flpr
  SOURCE_DIR ${APP_DIR}/remote
  BOARD ${SB_CONFIG_BOARD}/${SB_CONFIG_SOC}/cpuflpr
  BOARD_REVISION ${BOARD_REVISION}
)
