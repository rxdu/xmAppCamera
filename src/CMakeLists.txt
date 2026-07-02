# Our code builds warning-clean; third_party is not subjected to this.
add_compile_options(${XMCAM_WARNING_FLAGS})

# Backend libraries build with no GUI dependency (core testable without a
# window). GUI layers (ui/app) are added only when XMCAM_BUILD_GUI is ON.
add_subdirectory(core)
add_subdirectory(pipeline)
add_subdirectory(control)
add_subdirectory(export)
add_subdirectory(qualify)

if(XMCAM_BUILD_GUI)
  add_subdirectory(ui)
  add_subdirectory(app)
endif()
# Added in later phases:
#   add_subdirectory(control)    # ControlIntrospector, ControlSet
#   add_subdirectory(export)     # ConfigWriter/Reader, FrameSink
# if(XMCAM_BUILD_GUI)
#   add_subdirectory(ui)
#   add_subdirectory(app)
# endif()
