# Backend libraries build with no GUI dependency (core testable without a
# window). GUI layers (ui/app) are added only when XMCAM_BUILD_GUI is ON.
add_subdirectory(core)
add_subdirectory(pipeline)
add_subdirectory(control)
add_subdirectory(export)
# Added in later phases:
#   add_subdirectory(control)    # ControlIntrospector, ControlSet
#   add_subdirectory(export)     # ConfigWriter/Reader, FrameSink
# if(XMCAM_BUILD_GUI)
#   add_subdirectory(ui)
#   add_subdirectory(app)
# endif()
