add_subdirectory(App)
add_subdirectory(Task)
add_subdirectory(hptimer)

if(UWB_CHIP_TYPE STREQUAL "DW1000")
  add_subdirectory(Dw1000)
elseif(UWB_CHIP_TYPE STREQUAL "CX310")
  add_subdirectory(CX310)
endif()
