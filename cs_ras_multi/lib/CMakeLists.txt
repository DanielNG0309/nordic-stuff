find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# Channel Sounding
add_library(cs cs/cs.c)
target_include_directories(cs PUBLIC cs ${ZEPHYR_BASE}/include)
target_link_libraries(cs PUBLIC zephyr_interface calc)

# Bluetooth
add_library(ble ble/ble.c)
target_include_directories(ble PUBLIC ble ${ZEPHYR_BASE}/include)
target_link_libraries(ble PUBLIC zephyr_interface cs sync)

# Distance calculation 
add_library(calc calc/calc.c)
target_include_directories(calc PUBLIC calc ${ZEPHYR_BASE}/include)
target_link_libraries(calc PUBLIC zephyr_interface)

# Synchronization
add_library(sync sync/sync.c)
target_include_directories(sync PUBLIC sync ${ZEPHYR_BASE}/include)
target_link_libraries(sync PUBLIC zephyr_interface)