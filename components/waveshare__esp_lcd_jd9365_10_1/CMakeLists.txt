idf_component_register(SRCS "esp_lcd_jd9365_10_1.c" INCLUDE_DIRS "include" PRIV_REQUIRES "esp_driver_gpio espressif__i2c_bus" REQUIRES "esp_lcd")

include(package_manager)
cu_pkg_define_version(${CMAKE_CURRENT_LIST_DIR})
